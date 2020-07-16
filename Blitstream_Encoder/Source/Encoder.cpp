#include "Encoder.h"
#include <cassert>
#include <cstdio>

#ifdef _DEBUG
#define WIN_CHECK(x) { \
HRESULT ret = x; \
if(ret != S_OK) printf("HRESULT: %s is 0x%08x in %s at line %d\n", #x, x, __FILE__, __LINE__); \
}
#define NVENC_CHECK(x) { \
NVENCSTATUS ret = x; \
if(ret != NV_ENC_SUCCESS) printf("NVENC_API: %s is 0x%08x in %s at line %d\n", #x, x, __FILE__, __LINE__); \
}
#else
#define WIN_CHECK
#define NVENC_CHECK
#endif

void Encoder::Initialize() {
	uint32_t deviceFlags = 0;
#ifdef _DEBUG
	deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL feature_levels[] { D3D_FEATURE_LEVEL_11_1 };
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_1;
	WIN_CHECK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
								feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION,
								&d3d11_device, &feature_level, &d3d11_context));

	CreateDisplayDuplication();
	CreateEncoder();
}

void Encoder::CreateDisplayDuplication() {
	IDXGIDevice2 *temp_device;
	IDXGIAdapter *temp_adapter;
	IDXGIOutput *temp_output;
	IDXGIOutput6 *temp_output6;

	WIN_CHECK(d3d11_device->QueryInterface(__uuidof(IDXGIDevice2), reinterpret_cast<void **>(&temp_device)));
	WIN_CHECK(temp_device->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void **>(&temp_adapter)));
	WIN_CHECK(temp_adapter->EnumOutputs(0, &temp_output));
	WIN_CHECK(temp_output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void **>(&temp_output6)));
	WIN_CHECK(temp_output6->DuplicateOutput(temp_device, &d3d11_output_duplication));

	temp_device->Release();
	temp_adapter->Release();
	temp_output->Release();
	temp_output6->Release();

	DXGI_OUTDUPL_DESC desc {};
	d3d11_output_duplication->GetDesc(&desc);
	width = desc.ModeDesc.Width;
	height = desc.ModeDesc.Height;
}

void Encoder::CreateEncoder() {
	// Load the API
	uint32_t version = 0;
	uint32_t currentVersion = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
	NVENC_CHECK(NvEncodeAPIGetMaxSupportedVersion(&version));
	assert(currentVersion <= version && "Current Driver Version does not support this NvEncodeAPI version, please upgrade driver");

	nvenc_api = NV_ENCODE_API_FUNCTION_LIST {
		.version = NV_ENCODE_API_FUNCTION_LIST_VER
	};
	NVENC_CHECK(NvEncodeAPICreateInstance(&nvenc_api));

	// Start an encoding session
	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS encode_session_params = {
		.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER,
		.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX,
		.device = d3d11_device,
		.apiVersion = NVENCAPI_VERSION
	};
	NVENC_CHECK(nvenc_api.nvEncOpenEncodeSessionEx(&encode_session_params, &nvenc_encoder));

	// Iterate and choose an encoding GUID 
	uint32_t codec_guid_count;
	nvenc_api.nvEncGetEncodeGUIDCount(nvenc_encoder, &codec_guid_count);
	GUID *codec_guids = reinterpret_cast<GUID *>(malloc(codec_guid_count * sizeof(GUID)));
	nvenc_api.nvEncGetEncodeGUIDs(nvenc_encoder, codec_guids, codec_guid_count, &codec_guid_count);
	for(uint32_t i = 0; i < codec_guid_count; ++i) {
		if(codec_guids[i] == NV_ENC_CODEC_HEVC_GUID) {
			nvenc_encode_guid = codec_guids[i];
			break;
		}
		if(codec_guids[i] == NV_ENC_CODEC_H264_GUID) {
			nvenc_encode_guid = codec_guids[i];
		}
	}
	assert(nvenc_encode_guid != GUID {} && "Couldn't find appropriate codec for encoding");

	// Iterate and choose a preset GUID
	uint32_t preset_guid_count;
	nvenc_api.nvEncGetEncodePresetCount(nvenc_encoder, nvenc_encode_guid, &preset_guid_count);
	GUID *preset_guids = reinterpret_cast<GUID *>(malloc(preset_guid_count * sizeof(GUID)));
	nvenc_api.nvEncGetEncodePresetGUIDs(nvenc_encoder, nvenc_encode_guid, preset_guids, preset_guid_count, &preset_guid_count);
	for(uint32_t i = 0; i < preset_guid_count; ++i) {
		if(preset_guids[i] == NV_ENC_PRESET_P7_GUID) {
			nvenc_preset_guid = preset_guids[i];
			break;
		}
		if(preset_guids[i] == NV_ENC_PRESET_P6_GUID) {
			nvenc_preset_guid = preset_guids[i];
		}
	}
	assert(nvenc_preset_guid != GUID {} && "Couldn't find appropriate profile for encoding");

	// Iterate and choose a profile GUID
	uint32_t profile_guid_count;
	nvenc_api.nvEncGetEncodeProfileGUIDCount(nvenc_encoder, nvenc_encode_guid, &profile_guid_count);
	GUID *profile_guids = reinterpret_cast<GUID *>(malloc(profile_guid_count * sizeof(GUID)));
	nvenc_api.nvEncGetEncodeProfileGUIDs(nvenc_encoder, nvenc_encode_guid, profile_guids, profile_guid_count, &profile_guid_count);
	for(uint32_t i = 0; i < profile_guid_count; ++i) {
		if(profile_guids[i] == NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID) {
			nvenc_profile_guid = profile_guids[i];
			break;
		}
	}
	assert(nvenc_profile_guid != GUID {} && "Couldn't find appropriate profile for encoding");

	free(codec_guids);
	free(preset_guids);
	free(profile_guids);

	// Get encoding config from preset
	NV_ENC_PRESET_CONFIG preset_config {
		.version = NV_ENC_PRESET_CONFIG_VER,
		.presetCfg = NV_ENC_CONFIG {
			.version = NV_ENC_CONFIG_VER
		}
	};
	NVENC_CHECK(nvenc_api.nvEncGetEncodePresetConfig(nvenc_encoder, nvenc_encode_guid, 
													 nvenc_preset_guid, &preset_config));
	NV_ENC_CONFIG encode_config = preset_config.presetCfg;


	NV_ENC_INITIALIZE_PARAMS encoder_init_params {
		.version = NV_ENC_INITIALIZE_PARAMS_VER,
		.encodeGUID = nvenc_encode_guid,
		.presetGUID = nvenc_preset_guid,
		.encodeWidth = 3840,
		.encodeHeight = 2160,
		.darWidth = 3840,
		.darHeight = 2160,
		.frameRateNum = 60,
		.frameRateDen = 1,
		.enableEncodeAsync = 0, // TODO: Unsure
		.enablePTD = 1,
		.encodeConfig = &encode_config,
		.maxEncodeWidth = 3840,
		.maxEncodeHeight = 2160,
		.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY
	};

	NVENC_CHECK(nvenc_api.nvEncInitializeEncoder(nvenc_encoder, &encoder_init_params));

	// Output buffers
	for(int i = 0; i < NUM_IO_BUFFERS; i++) {
		NV_ENC_CREATE_BITSTREAM_BUFFER create_bitstream_buffer {
			.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER
		};
		NVENC_CHECK(nvenc_api.nvEncCreateBitstreamBuffer(nvenc_encoder, &create_bitstream_buffer));
		nvenc_output_buffers[i] = create_bitstream_buffer.bitstreamBuffer;
	}
}

EncodedData Encoder::Encode() {
	int index = current_buffer_index % NUM_IO_BUFFERS;

	NV_ENC_REGISTER_RESOURCE register_resource {
		.version = NV_ENC_REGISTER_RESOURCE_VER,
		.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX,
		.width = 3840,
		.height = 2160,
		.pitch = 0,
		.subResourceIndex = 0,
		.resourceToRegister = d3d11_texture,
		.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB,
		.bufferUsage = NV_ENC_INPUT_IMAGE
	};

	NVENC_CHECK(nvenc_api.nvEncRegisterResource(nvenc_encoder, &register_resource));

	NV_ENC_MAP_INPUT_RESOURCE input_resource {
		.version = NV_ENC_MAP_INPUT_RESOURCE_VER,
		.registeredResource = register_resource.registeredResource
	};
	NVENC_CHECK(nvenc_api.nvEncMapInputResource(nvenc_encoder, &input_resource));

	NV_ENC_PIC_PARAMS pic_params = {
		.version = NV_ENC_PIC_PARAMS_VER,
		.inputWidth = 3840,
		.inputHeight = 2160,
		.inputBuffer = input_resource.mappedResource,
		.outputBitstream = nvenc_output_buffers[index],
		.bufferFmt = input_resource.mappedBufferFmt,
		.pictureStruct = NV_ENC_PIC_STRUCT_FRAME
	};
	NVENC_CHECK(nvenc_api.nvEncEncodePicture(nvenc_encoder, &pic_params));

	NV_ENC_LOCK_BITSTREAM lock_bitstream {
		.version = NV_ENC_LOCK_BITSTREAM_VER,
		.outputBitstream = nvenc_output_buffers[index]
	};
	NVENC_CHECK(nvenc_api.nvEncLockBitstream(nvenc_encoder, &lock_bitstream));

	return EncodedData {
		.ptr = lock_bitstream.bitstreamBufferPtr,
		.size = lock_bitstream.bitstreamSizeInBytes
	};
}


EncodedData Encoder::StartCapture() {
	printf("RESOURCE: %p\n", d3d11_resource);

	if(d3d11_resource) {
		d3d11_output_duplication->ReleaseFrame();
		d3d11_resource->Release();
		d3d11_resource = nullptr;
	}

	DXGI_OUTDUPL_FRAME_INFO frame_info {};
	HRESULT dxgi_result = d3d11_output_duplication->AcquireNextFrame(1, &frame_info, &d3d11_resource);
	printf("DXGI_RESULT1: %x\n", dxgi_result);
	if(dxgi_result == DXGI_ERROR_WAIT_TIMEOUT) {
		return {};
	}
	if(dxgi_result == DXGI_ERROR_ACCESS_LOST || !d3d11_resource) {
		printf("Recr\n");
		CreateDisplayDuplication();
		dxgi_result = d3d11_output_duplication->AcquireNextFrame(1, &frame_info, &d3d11_resource);
		printf("DXGI_RESULT2: %x\n", dxgi_result);
	}

	WIN_CHECK(d3d11_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&d3d11_texture)));

	return Encode();
}

void Encoder::EndCapture() {
	int index = current_buffer_index % NUM_IO_BUFFERS;
	NVENC_CHECK(nvenc_api.nvEncUnlockBitstream(nvenc_encoder, nvenc_output_buffers[index]));
	++current_buffer_index;
}

void Encoder::Shutdown() {
	d3d11_device->Release();
	d3d11_context->Release();
	d3d11_output_duplication->Release();

	for(int i = 0; i < NUM_IO_BUFFERS; ++i) {
		NVENC_CHECK(nvenc_api.nvEncDestroyBitstreamBuffer(nvenc_encoder, nvenc_output_buffers[i]));
	}

	// Signal end of stream
	NV_ENC_PIC_PARAMS pic_params_eos {
		.version = NV_ENC_PIC_PARAMS_VER,
		.encodePicFlags = NV_ENC_PIC_FLAG_EOS
	};
	NVENC_CHECK(nvenc_api.nvEncEncodePicture(nvenc_encoder, &pic_params_eos));

	NVENC_CHECK(nvenc_api.nvEncDestroyEncoder(nvenc_encoder));
}
