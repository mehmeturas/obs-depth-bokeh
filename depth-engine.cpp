#include "depth-engine.h"

#include <onnxruntime_cxx_api.h>

#ifdef _WIN32
#include <dml_provider_factory.h>
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

/* ImageNet normalisation constants used by Depth Anything V2. */
static const float kMean[3] = {0.485f, 0.456f, 0.406f};
static const float kStd[3] = {0.229f, 0.224f, 0.225f};

struct DepthEngineImpl {
	Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "obs-depth-bokeh"};
	Ort::SessionOptions opts;
	std::unique_ptr<Ort::Session> session;
	Ort::AllocatorWithDefaultOptions alloc;

	std::string input_name;
	std::string output_name;
	std::string provider_name = "CPUExecutionProvider";

	int size = 252;
	bool ok = false;

	/* Scratch buffers, reused every frame so we never allocate in the hot
	 * path. */
	std::vector<float> input_tensor;  /* 1*3*size*size, CHW */
	std::vector<float> raw_depth;     /* size*size */
};

DepthEngine::DepthEngine() : d(std::make_unique<DepthEngineImpl>()) {}
DepthEngine::~DepthEngine() = default;

int DepthEngine::infer_size() const
{
	return d->size;
}

bool DepthEngine::loaded() const
{
	return d->ok;
}

const char *DepthEngine::provider() const
{
	return d->provider_name.c_str();
}

bool DepthEngine::load(const std::string &model_path, int infer_size,
		       bool use_gpu)
{
	d->ok = false;

	/* Depth Anything needs a multiple of 14. Snap to the nearest valid
	 * value instead of failing, so a bad setting cannot brick the filter. */
	infer_size = std::max(126, std::min(518, infer_size));
	infer_size = (infer_size / 14) * 14;
	d->size = infer_size;

	try {
		d->opts = Ort::SessionOptions();
		d->opts.SetIntraOpNumThreads(2);
		d->opts.SetGraphOptimizationLevel(
			GraphOptimizationLevel::ORT_ENABLE_ALL);

		if (use_gpu) {
#ifdef _WIN32
			try {
				d->opts.DisableMemPattern();
				d->opts.SetExecutionMode(ORT_SEQUENTIAL);
				OrtSessionOptionsAppendExecutionProvider_DML(
					d->opts, 0);
				d->provider_name = "DmlExecutionProvider";
			} catch (const std::exception &) {
				d->provider_name = "CPUExecutionProvider";
			}
#elif defined(__APPLE__)
			try {
				d->opts.AppendExecutionProvider(
					"CoreML", {{"ModelFormat", "MLProgram"}});
				d->provider_name = "CoreMLExecutionProvider";
			} catch (const std::exception &) {
				d->provider_name = "CPUExecutionProvider";
			}
#endif
		}

#ifdef _WIN32
		std::wstring wpath(model_path.begin(), model_path.end());
		d->session = std::make_unique<Ort::Session>(d->env, wpath.c_str(),
							   d->opts);
#else
		d->session = std::make_unique<Ort::Session>(
			d->env, model_path.c_str(), d->opts);
#endif

		auto in = d->session->GetInputNameAllocated(0, d->alloc);
		auto out = d->session->GetOutputNameAllocated(0, d->alloc);
		d->input_name = in.get();
		d->output_name = out.get();

		d->input_tensor.assign((size_t)3 * d->size * d->size, 0.0f);
		d->raw_depth.assign((size_t)d->size * d->size, 0.0f);

		d->ok = true;
	} catch (const std::exception &) {
		d->session.reset();
		d->ok = false;
	}

	return d->ok;
}

/* Bilinear resize straight from BGRA into the normalised CHW float tensor.
 * Doing both in one pass avoids an intermediate RGB buffer. */
static void preprocess(const uint8_t *bgra, int in_w, int in_h, int size,
		       std::vector<float> &dst)
{
	const float sx = (float)in_w / (float)size;
	const float sy = (float)in_h / (float)size;
	const size_t plane = (size_t)size * size;

	for (int y = 0; y < size; y++) {
		float fy = (y + 0.5f) * sy - 0.5f;
		int y0 = (int)std::floor(fy);
		float wy = fy - y0;
		int y1 = std::min(std::max(y0 + 1, 0), in_h - 1);
		y0 = std::min(std::max(y0, 0), in_h - 1);

		for (int x = 0; x < size; x++) {
			float fx = (x + 0.5f) * sx - 0.5f;
			int x0 = (int)std::floor(fx);
			float wx = fx - x0;
			int x1 = std::min(std::max(x0 + 1, 0), in_w - 1);
			x0 = std::min(std::max(x0, 0), in_w - 1);

			const uint8_t *p00 = bgra + ((size_t)y0 * in_w + x0) * 4;
			const uint8_t *p01 = bgra + ((size_t)y0 * in_w + x1) * 4;
			const uint8_t *p10 = bgra + ((size_t)y1 * in_w + x0) * 4;
			const uint8_t *p11 = bgra + ((size_t)y1 * in_w + x1) * 4;

			const size_t idx = (size_t)y * size + x;

			/* BGRA -> RGB, channel 0 = R */
			for (int c = 0; c < 3; c++) {
				const int b = 2 - c; /* R=byte2, G=byte1, B=byte0 */
				float top = p00[b] * (1.0f - wx) + p01[b] * wx;
				float bot = p10[b] * (1.0f - wx) + p11[b] * wx;
				float v = (top * (1.0f - wy) + bot * wy) / 255.0f;
				dst[c * plane + idx] = (v - kMean[c]) / kStd[c];
			}
		}
	}
}

bool DepthEngine::infer(const uint8_t *bgra, int in_w, int in_h,
			std::vector<float> &out)
{
	if (!d->ok || !bgra || in_w <= 0 || in_h <= 0)
		return false;

	const int S = d->size;
	const size_t N = (size_t)S * S;

	try {
		preprocess(bgra, in_w, in_h, S, d->input_tensor);

		const int64_t shape[4] = {1, 3, S, S};
		auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,
						      OrtMemTypeDefault);
		Ort::Value input = Ort::Value::CreateTensor<float>(
			mem, d->input_tensor.data(), d->input_tensor.size(),
			shape, 4);

		const char *in_names[] = {d->input_name.c_str()};
		const char *out_names[] = {d->output_name.c_str()};

		auto result = d->session->Run(Ort::RunOptions{nullptr}, in_names,
					      &input, 1, out_names, 1);

		const float *raw = result[0].GetTensorData<float>();
		auto info = result[0].GetTensorTypeAndShapeInfo();
		const size_t count = info.GetElementCount();
		if (count < N)
			return false;

		/* Normalise inverse depth to 0..1. Depth Anything outputs
		 * larger values for closer surfaces, which is exactly the
		 * convention the shader expects. */
		float mn = std::numeric_limits<float>::max();
		float mx = -std::numeric_limits<float>::max();
		for (size_t i = 0; i < N; i++) {
			mn = std::min(mn, raw[i]);
			mx = std::max(mx, raw[i]);
		}
		const float range = (mx - mn) > 1e-6f ? (mx - mn) : 1.0f;

		out.resize(N);
		for (size_t i = 0; i < N; i++)
			out[i] = (raw[i] - mn) / range;

		return true;
	} catch (const std::exception &) {
		return false;
	}
}
