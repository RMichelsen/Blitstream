#pragma once
#include <cstdint>
#include <nvcuvid.h>
#include <d3d11.h>

struct OutputDimensions {
	uint32_t target_width;
	uint32_t target_height;
	short target_rect_left;
	short target_rect_top;
	short target_rect_right;
	short target_rect_bottom;
};

struct Decoder {
	OutputDimensions dimensions;

	ID3D11Device *d3d11_device;
	IDXGISwapChain *d3d11_swapchain;
	ID3D11DeviceContext *d3d11_context;
	ID3D11Texture2D *d3d11_backbuffer;

	CUdevice cu_device;
	CUcontext cu_context;
	CUgraphicsResource cu_graphics_resource;
	CUvideoparser cu_parser;
	CUvideodecoder cu_decoder;
	CUdeviceptr device_ptr_converted_intermediate = 0;
	CUdeviceptr device_ptr_converted_result = 0;

	void Initialize(HWND hwnd);

	void Resize(uint32_t width, uint32_t height);
	void Decode(void *ptr, uint32_t size);

	int SequenceCallback(CUVIDEOFORMAT *video_format);
	int DecodeCallback(CUVIDPICPARAMS *pic_params);
	int DisplayCallback(CUVIDPARSERDISPINFO *display_info);

	void Shutdown();
};