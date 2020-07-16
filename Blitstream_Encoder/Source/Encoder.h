#pragma once
#include <cstdint>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <nvEncodeAPI.h>

constexpr uint32_t NUM_IO_BUFFERS = 4;

struct EncodedData {
	void *ptr;
	uint32_t size;
};

struct Encoder {
	uint32_t width;
	uint32_t height;
	
	ID3D11Device *d3d11_device;
	ID3D11DeviceContext *d3d11_context;
	IDXGIOutputDuplication *d3d11_output_duplication;
	IDXGIResource *d3d11_resource;
	ID3D11Texture2D *d3d11_texture;

	NV_ENCODE_API_FUNCTION_LIST nvenc_api;
	void *nvenc_encoder;
	GUID nvenc_encode_guid;
	GUID nvenc_preset_guid;
	GUID nvenc_profile_guid;

	uint32_t current_buffer_index;
	NV_ENC_OUTPUT_PTR nvenc_output_buffers[NUM_IO_BUFFERS];

	void Initialize();

	void CreateDisplayDuplication();
	void CreateEncoder();

	EncodedData Encode();

	EncodedData StartCapture();
	void EndCapture();

	void Shutdown();
};