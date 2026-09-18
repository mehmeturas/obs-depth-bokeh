#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-depth-bokeh", "en-US")

extern struct obs_source_info depth_bokeh_filter_info;

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Depth-aware background bokeh for webcams";
}

bool obs_module_load(void)
{
	obs_register_source(&depth_bokeh_filter_info);
	blog(LOG_INFO, "[obs-depth-bokeh] loaded");
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[obs-depth-bokeh] unloaded");
}
