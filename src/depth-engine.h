#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

/*
 * Monocular depth estimation wrapper around ONNX Runtime.
 *
 * Designed for Depth Anything V2 (small / vits) but works with any model that
 * takes a 1x3xHxW float tensor (ImageNet normalised) and returns a single
 * channel inverse-depth map.
 *
 * All calls are made from the inference worker thread, never from the OBS
 * graphics thread.
 */

struct DepthEngineImpl;

class DepthEngine {
public:
	DepthEngine();
	~DepthEngine();

	DepthEngine(const DepthEngine &) = delete;
	DepthEngine &operator=(const DepthEngine &) = delete;

	/* model_path: absolute path to the .onnx file.
	 * infer_size: square input edge, must be a multiple of 14 for
	 *             Depth Anything (252, 336, 392, 518 ...).
	 * use_gpu:    try DirectML (Windows) / CoreML (macOS), fall back to CPU. */
	bool load(const std::string &model_path, int infer_size, bool use_gpu);

	bool loaded() const;

	/* Runs one inference.
	 *
	 * bgra      - tightly packed BGRA8 frame captured from OBS
	 * in_w/in_h - dimensions of that frame
	 * out       - resized to infer_size*infer_size, values normalised to
	 *             0..1 where 1 = nearest to camera
	 *
	 * Returns false if the model is not loaded or inference threw. */
	bool infer(const uint8_t *bgra, int in_w, int in_h,
		   std::vector<float> &out);

	int infer_size() const;

	/* Human readable name of the execution provider actually in use,
	 * e.g. "DmlExecutionProvider" or "CPUExecutionProvider". */
	const char *provider() const;

private:
	std::unique_ptr<DepthEngineImpl> d;
};
