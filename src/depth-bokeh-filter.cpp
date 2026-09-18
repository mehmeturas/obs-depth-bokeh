#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

#include "depth-engine.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#define S_MODEL          "model_file"
#define S_INFER_SIZE     "infer_size"
#define S_USE_GPU        "use_gpu"
#define S_INFER_FPS      "infer_fps"
#define S_MAX_RADIUS     "max_radius"
#define S_FOCUS          "focus_depth"
#define S_FOCUS_RANGE    "focus_range"
#define S_TEMPORAL       "temporal"
#define S_EDGE           "edge_sensitivity"
#define S_MIX            "mix"
#define S_SHOW_DEPTH     "show_depth"

#define T_(x) obs_module_text(x)

struct depth_bokeh_filter {
	obs_source_t *context = nullptr;

	/* --- graphics --- */
	gs_effect_t *effect = nullptr;
	gs_eparam_t *p_depth = nullptr;
	gs_eparam_t *p_texel = nullptr;
	gs_eparam_t *p_max_radius = nullptr;
	gs_eparam_t *p_focus = nullptr;
	gs_eparam_t *p_focus_range = nullptr;
	gs_eparam_t *p_edge = nullptr;
	gs_eparam_t *p_mix = nullptr;

	gs_texrender_t *capture = nullptr;   /* downscaled source frame */
	gs_stagesurf_t *stage = nullptr;     /* GPU -> CPU readback */
	gs_texture_t *depth_tex = nullptr;   /* R32F depth map */

	uint32_t cap_w = 0, cap_h = 0;
	uint32_t width = 0, height = 0;

	/* --- inference --- */
	DepthEngine engine;
	std::thread worker;
	std::atomic<bool> running{false};
	std::atomic<bool> reload_model{false};

	std::mutex frame_mtx;
	std::condition_variable frame_cv;
	std::vector<uint8_t> frame_bgra;
	bool frame_ready = false;

	std::mutex depth_mtx;
	std::vector<float> depth_raw;      /* freshest inference result */
	std::vector<float> depth_smooth;   /* EMA accumulator */
	bool depth_dirty = false;
	int depth_size = 0;

	/* --- settings --- */
	std::string model_path;
	int infer_size = 252;
	bool use_gpu = true;
	int infer_fps = 15;
	float max_radius = 18.0f;
	float focus_depth = 0.85f;
	float focus_range = 0.12f;
	float temporal = 0.35f;
	float edge = 6.0f;
	float mix = 1.0f;
	bool show_depth = false;

	uint64_t last_capture_ns = 0;
};

/* ------------------------------------------------------------------ */
/* worker thread                                                       */
/* ------------------------------------------------------------------ */

static void worker_loop(depth_bokeh_filter *f)
{
	std::vector<uint8_t> local;
	std::vector<float> result;

	while (f->running.load()) {
		{
			std::unique_lock<std::mutex> lk(f->frame_mtx);
			f->frame_cv.wait_for(lk, std::chrono::milliseconds(100),
					     [f] {
						     return f->frame_ready ||
							    !f->running.load();
					     });
			if (!f->running.load())
				break;
			if (!f->frame_ready)
				continue;
			local = f->frame_bgra;
			f->frame_ready = false;
		}

		if (f->reload_model.exchange(false)) {
			if (!f->model_path.empty())
				f->engine.load(f->model_path, f->infer_size,
					       f->use_gpu);
		}

		if (!f->engine.loaded())
			continue;

		if (!f->engine.infer(local.data(), (int)f->cap_w,
				     (int)f->cap_h, result))
			continue;

		const int S = f->engine.infer_size();
		const size_t N = (size_t)S * S;

		std::lock_guard<std::mutex> lk(f->depth_mtx);

		/* Temporal EMA. This is what kills the frame-to-frame shimmer
		 * around hair and glasses that single-frame models produce. */
		if (f->depth_smooth.size() != N || f->depth_size != S) {
			f->depth_smooth = result;
			f->depth_size = S;
		} else {
			const float a = std::clamp(1.0f - f->temporal, 0.05f,
						   1.0f);
			for (size_t i = 0; i < N; i++)
				f->depth_smooth[i] =
					a * result[i] +
					(1.0f - a) * f->depth_smooth[i];
		}
		f->depth_raw = f->depth_smooth;
		f->depth_dirty = true;
	}
}

/* ------------------------------------------------------------------ */
/* obs plumbing                                                        */
/* ------------------------------------------------------------------ */

static const char *filter_name(void *)
{
	return T_("DepthBokeh.Name");
}

static void filter_update(void *data, obs_data_t *settings)
{
	auto *f = (depth_bokeh_filter *)data;

	const char *mp = obs_data_get_string(settings, S_MODEL);
	std::string new_path = mp ? mp : "";
	int new_size = (int)obs_data_get_int(settings, S_INFER_SIZE);
	bool new_gpu = obs_data_get_bool(settings, S_USE_GPU);

	if (new_path != f->model_path || new_size != f->infer_size ||
	    new_gpu != f->use_gpu) {
		f->model_path = new_path;
		f->infer_size = new_size;
		f->use_gpu = new_gpu;
		f->reload_model.store(true);
	}

	f->infer_fps = (int)obs_data_get_int(settings, S_INFER_FPS);
	f->max_radius = (float)obs_data_get_double(settings, S_MAX_RADIUS);
	f->focus_depth = (float)obs_data_get_double(settings, S_FOCUS);
	f->focus_range = (float)obs_data_get_double(settings, S_FOCUS_RANGE);
	f->temporal = (float)obs_data_get_double(settings, S_TEMPORAL);
	f->edge = (float)obs_data_get_double(settings, S_EDGE);
	f->mix = (float)obs_data_get_double(settings, S_MIX);
	f->show_depth = obs_data_get_bool(settings, S_SHOW_DEPTH);
}

static void filter_destroy(void *data)
{
	auto *f = (depth_bokeh_filter *)data;
	if (!f)
		return;

	f->running.store(false);
	f->frame_cv.notify_all();
	if (f->worker.joinable())
		f->worker.join();

	obs_enter_graphics();
	if (f->effect)
		gs_effect_destroy(f->effect);
	if (f->capture)
		gs_texrender_destroy(f->capture);
	if (f->stage)
		gs_stagesurface_destroy(f->stage);
	if (f->depth_tex)
		gs_texture_destroy(f->depth_tex);
	obs_leave_graphics();

	delete f;
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new depth_bokeh_filter();
	f->context = source;

	char *path = obs_module_file("shaders/depth-bokeh.effect");
	obs_enter_graphics();
	char *err = nullptr;
	f->effect = gs_effect_create_from_file(path, &err);
	if (f->effect) {
		f->p_depth = gs_effect_get_param_by_name(f->effect, "depth_map");
		f->p_texel = gs_effect_get_param_by_name(f->effect, "texel");
		f->p_max_radius =
			gs_effect_get_param_by_name(f->effect, "max_radius");
		f->p_focus =
			gs_effect_get_param_by_name(f->effect, "focus_depth");
		f->p_focus_range =
			gs_effect_get_param_by_name(f->effect, "focus_range");
		f->p_edge = gs_effect_get_param_by_name(f->effect,
							"edge_sensitivity");
		f->p_mix = gs_effect_get_param_by_name(f->effect, "mix_amount");
	} else {
		blog(LOG_ERROR, "[depth-bokeh] shader failed: %s",
		     err ? err : "unknown");
	}
	bfree(err);
	obs_leave_graphics();
	bfree(path);

	if (!f->effect) {
		delete f;
		return nullptr;
	}

	filter_update(f, settings);
	f->reload_model.store(true);

	f->running.store(true);
	f->worker = std::thread(worker_loop, f);

	return f;
}

static void filter_defaults(obs_data_t *s)
{
	/* The installer drops the model into the plugin's own data folder, so
	 * a fresh user gets a working filter without ever opening a file
	 * dialog. Falls back to empty when the plugin was built by hand. */
	char *bundled = obs_module_file("models/depth_anything_v2_small.onnx");
	obs_data_set_default_string(s, S_MODEL, bundled ? bundled : "");
	bfree(bundled);

	obs_data_set_default_int(s, S_INFER_SIZE, 252);
	obs_data_set_default_bool(s, S_USE_GPU, true);
	obs_data_set_default_int(s, S_INFER_FPS, 15);
	obs_data_set_default_double(s, S_MAX_RADIUS, 18.0);
	obs_data_set_default_double(s, S_FOCUS, 0.85);
	obs_data_set_default_double(s, S_FOCUS_RANGE, 0.12);
	obs_data_set_default_double(s, S_TEMPORAL, 0.35);
	obs_data_set_default_double(s, S_EDGE, 6.0);
	obs_data_set_default_double(s, S_MIX, 1.0);
	obs_data_set_default_bool(s, S_SHOW_DEPTH, false);
}

static obs_properties_t *filter_properties(void *)
{
	obs_properties_t *p = obs_properties_create();

	obs_properties_add_path(p, S_MODEL, T_("DepthBokeh.Model"),
				OBS_PATH_FILE, "ONNX (*.onnx)", nullptr);

	obs_property_t *sz = obs_properties_add_list(
		p, S_INFER_SIZE, T_("DepthBokeh.InferSize"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(sz, "126 (fastest)", 126);
	obs_property_list_add_int(sz, "252 (balanced)", 252);
	obs_property_list_add_int(sz, "336 (sharp)", 336);
	obs_property_list_add_int(sz, "518 (max quality)", 518);

	obs_properties_add_bool(p, S_USE_GPU, T_("DepthBokeh.UseGPU"));
	obs_properties_add_int_slider(p, S_INFER_FPS, T_("DepthBokeh.InferFPS"),
				      5, 60, 1);

	obs_properties_add_float_slider(p, S_MAX_RADIUS,
					T_("DepthBokeh.MaxRadius"), 0.0, 60.0,
					0.5);
	obs_properties_add_float_slider(p, S_FOCUS, T_("DepthBokeh.Focus"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(p, S_FOCUS_RANGE,
					T_("DepthBokeh.FocusRange"), 0.0, 0.6,
					0.01);
	obs_properties_add_float_slider(p, S_TEMPORAL, T_("DepthBokeh.Temporal"),
					0.0, 0.95, 0.01);
	obs_properties_add_float_slider(p, S_EDGE, T_("DepthBokeh.Edge"), 0.0,
					20.0, 0.1);
	obs_properties_add_float_slider(p, S_MIX, T_("DepthBokeh.Mix"), 0.0, 1.0,
					0.01);
	obs_properties_add_bool(p, S_SHOW_DEPTH, T_("DepthBokeh.ShowDepth"));

	return p;
}

/* Render the source once at low resolution and pull it back to the CPU so
 * the worker thread has something to run inference on. Capped to infer_fps
 * because depth changes far slower than the video does. */
static void capture_for_inference(depth_bokeh_filter *f, obs_source_t *target,
				  uint32_t w, uint32_t h)
{
	const uint64_t now = os_gettime_ns();
	const uint64_t interval = 1000000000ULL / (uint64_t)std::max(1, f->infer_fps);
	if (now - f->last_capture_ns < interval)
		return;
	f->last_capture_ns = now;

	const uint32_t cw = 320;
	const uint32_t ch = std::max(1u, (uint32_t)((float)cw * h / (float)w));

	if (!f->capture)
		f->capture = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	if (!f->stage || f->cap_w != cw || f->cap_h != ch) {
		if (f->stage)
			gs_stagesurface_destroy(f->stage);
		f->stage = gs_stagesurface_create(cw, ch, GS_BGRA);
		f->cap_w = cw;
		f->cap_h = ch;
	}

	gs_texrender_reset(f->capture);
	if (!gs_texrender_begin(f->capture, cw, ch))
		return;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
	obs_source_video_render(target);
	gs_texrender_end(f->capture);

	gs_stage_texture(f->stage, gs_texrender_get_texture(f->capture));

	uint8_t *ptr = nullptr;
	uint32_t linesize = 0;
	if (gs_stagesurface_map(f->stage, &ptr, &linesize)) {
		std::lock_guard<std::mutex> lk(f->frame_mtx);
		f->frame_bgra.resize((size_t)cw * ch * 4);
		for (uint32_t y = 0; y < ch; y++)
			memcpy(f->frame_bgra.data() + (size_t)y * cw * 4,
			       ptr + (size_t)y * linesize, (size_t)cw * 4);
		f->frame_ready = true;
		gs_stagesurface_unmap(f->stage);
		f->frame_cv.notify_one();
	}
}

static void upload_depth(depth_bokeh_filter *f)
{
	std::lock_guard<std::mutex> lk(f->depth_mtx);
	if (!f->depth_dirty || f->depth_raw.empty())
		return;

	const uint32_t S = (uint32_t)f->depth_size;

	if (!f->depth_tex || gs_texture_get_width(f->depth_tex) != S) {
		if (f->depth_tex)
			gs_texture_destroy(f->depth_tex);
		const uint8_t *init = (const uint8_t *)f->depth_raw.data();
		f->depth_tex = gs_texture_create(S, S, GS_R32F, 1, &init,
						 GS_DYNAMIC);
	} else {
		gs_texture_set_image(f->depth_tex,
				     (const uint8_t *)f->depth_raw.data(),
				     S * sizeof(float), false);
	}
	f->depth_dirty = false;
}

static void filter_render(void *data, gs_effect_t *)
{
	auto *f = (depth_bokeh_filter *)data;
	obs_source_t *target = obs_filter_get_target(f->context);
	obs_source_t *parent = obs_filter_get_parent(f->context);

	if (!target || !parent) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	f->width = obs_source_get_base_width(target);
	f->height = obs_source_get_base_height(target);
	if (!f->width || !f->height) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	capture_for_inference(f, target, f->width, f->height);
	upload_depth(f);

	if (!f->depth_tex) {
		/* No depth yet -> pass the image through untouched rather
		 * than showing a black frame. */
		obs_source_skip_video_filter(f->context);
		return;
	}

	if (!obs_source_process_filter_begin(f->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;

	struct vec2 texel;
	vec2_set(&texel, 1.0f / (float)f->width, 1.0f / (float)f->height);

	gs_effect_set_texture(f->p_depth, f->depth_tex);
	gs_effect_set_vec2(f->p_texel, &texel);
	gs_effect_set_float(f->p_max_radius, f->max_radius);
	gs_effect_set_float(f->p_focus, f->focus_depth);
	gs_effect_set_float(f->p_focus_range, f->focus_range);
	gs_effect_set_float(f->p_edge, f->edge);
	gs_effect_set_float(f->p_mix, f->mix);

	const char *tech = f->show_depth ? "DrawDepth" : "Draw";
	obs_source_process_filter_tech_end(f->context, f->effect, f->width,
					   f->height, tech);
}

struct obs_source_info depth_bokeh_filter_info = {
	.id = "depth_bokeh_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_name,
	.create = filter_create,
	.destroy = filter_destroy,
	.get_defaults = filter_defaults,
	.get_properties = filter_properties,
	.update = filter_update,
	.video_render = filter_render,
};
