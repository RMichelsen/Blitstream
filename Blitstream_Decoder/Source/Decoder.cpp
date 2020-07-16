#include "Decoder.h"
#include <cassert>
#include <cstdio>
#include <cudaD3D11.h>
#include <nppi.h>

#ifdef _DEBUG
#define WIN_CHECK(x) { \
HRESULT ret = x; \
if(ret != S_OK) printf("HRESULT: %s is 0x%08x in %s at line %d\n", #x, x, __FILE__, __LINE__); \
}
#define CU_CHECK(x) { \
CUresult ret = x; \
if(ret != CUDA_SUCCESS) printf("CUDA_API: %s is %i in %s at line %d\n", #x, x, __FILE__, __LINE__); \
}
#define AV_CHECK(x) { \
int ret = x; \
if(ret != 0) printf("FFMPEG_API: %s is 0x%08x in %s at line %d\n", #x, AVERROR(x), __FILE__, __LINE__); \
}
#define NPP_CHECK(x) { \
int ret = x; \
if(ret != NPP_NO_ERROR) printf("NPP_API: %s is 0x%08x in %s at line %d\n", #x, x, __FILE__, __LINE__); \
}
#else
#define WIN_CHECK
#define CU_CHECK
#define AV_CHECK
#define NPP_CHECK
#endif

struct BufferData {
	uint8_t *ptr;
	int size;
};

static int CUDAAPI HandleSequenceCallback(void *user_data, 
										  CUVIDEOFORMAT *video_format) {
	return reinterpret_cast<Decoder *>(user_data)->SequenceCallback(video_format);
}

static int CUDAAPI HandleDecodeCallback(void *user_data,
										CUVIDPICPARAMS *pic_params) {
	return reinterpret_cast<Decoder *>(user_data)->DecodeCallback(pic_params);
}

static int CUDAAPI HandleDisplayCallback(void *user_data,
										 CUVIDPARSERDISPINFO *display_info) {
	return reinterpret_cast<Decoder *>(user_data)->DisplayCallback(display_info);
}

static void CalculateTargetDimensions(uint32_t width, uint32_t height,
									  OutputDimensions &dimensions) {
	// Ensure width and height is aligned to 2
	dimensions.target_width = width % 2 == 0 ? width : width - 1;
	dimensions.target_height = height % 2 == 0 ? height : height - 1;

	float target_aspect_ratio = 16.0f / 9.0f;
	float aspect_ratio = static_cast<float>(dimensions.target_width) / dimensions.target_height;
	if(aspect_ratio == target_aspect_ratio) {
		dimensions.target_rect_left = 0;
		dimensions.target_rect_top = 0;
		dimensions.target_rect_right = dimensions.target_width;
		dimensions.target_rect_bottom = dimensions.target_height;
	}
	else if(aspect_ratio > 1.7777f) {
		short desired_width = static_cast<short>(target_aspect_ratio * dimensions.target_height);
		short half_delta = (dimensions.target_width - desired_width) / 2;
		dimensions.target_rect_left = half_delta;
		dimensions.target_rect_top = 0;
		dimensions.target_rect_right = dimensions.target_width - half_delta;
		dimensions.target_rect_bottom = dimensions.target_height;
	}
	else {
		short desired_height = static_cast<short>(dimensions.target_width / target_aspect_ratio);
		short half_delta = (dimensions.target_height - desired_height) / 2;
		dimensions.target_rect_left = 0;
		dimensions.target_rect_top = half_delta;
		dimensions.target_rect_right = dimensions.target_width;
		dimensions.target_rect_bottom = dimensions.target_height - half_delta;
	}
}

// HEVC level 6.2, assuming 8Kx4K resolution
constexpr int NUMBER_OF_DECODE_SURFACES = 16;

void Decoder::Initialize(HWND hwnd) {
	RECT client;
	GetClientRect(hwnd, &client);
	CalculateTargetDimensions(client.right - client.left, 
							  client.bottom - client.top,
							  dimensions);

	CU_CHECK(cuInit(0));
	int number_of_gpus = 0;
	CU_CHECK(cuDeviceGetCount(&number_of_gpus));
	assert(number_of_gpus > 0 && "No GPUs found");
	CU_CHECK(cuDeviceGet(&cu_device, 0));
	CU_CHECK(cuCtxCreate(&cu_context, 0, cu_device));


	CUVIDDECODECAPS decode_capabilities {
		.eCodecType = cudaVideoCodec_HEVC,
		.eChromaFormat = cudaVideoChromaFormat_420,
		.nBitDepthMinus8 = 0
	};
	CU_CHECK(cuCtxPushCurrent(cu_context));
	CU_CHECK(cuvidGetDecoderCaps(&decode_capabilities));
	CU_CHECK(cuCtxPopCurrent(NULL));
	assert(decode_capabilities.bIsSupported && "Could not find GPU capable of decoding HEVC stream");

	CUVIDPARSERPARAMS parser_params {
		.CodecType = cudaVideoCodec_HEVC,
		.ulMaxNumDecodeSurfaces = 1,
		.ulMaxDisplayDelay = 0, // TODO: Maybe set to 0 for low latency
		.pUserData = this,
		.pfnSequenceCallback = HandleSequenceCallback,
		.pfnDecodePicture = HandleDecodeCallback,
		.pfnDisplayPicture = HandleDisplayCallback
	};
	CU_CHECK(cuvidCreateVideoParser(&cu_parser, &parser_params));

	DXGI_SWAP_CHAIN_DESC swapchain_desc = { 
		.BufferDesc = DXGI_MODE_DESC {
			.Width = dimensions.target_width,
			.Height = dimensions.target_height,
			.RefreshRate = DXGI_RATIONAL {
				.Numerator = 60,
				.Denominator = 1
			},
			.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
		},
		.SampleDesc = DXGI_SAMPLE_DESC {
			.Count = 1,
			.Quality = 0
		},
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = 1,
		.OutputWindow = hwnd,
		.Windowed = TRUE
	};
	WIN_CHECK(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, 0 /*D3D11_CREATE_DEVICE_DEBUG*/, nullptr, 0, D3D11_SDK_VERSION,
											&swapchain_desc, &d3d11_swapchain, &d3d11_device, nullptr, &d3d11_context));
	WIN_CHECK(d3d11_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID *)&d3d11_backbuffer));

	CU_CHECK(cuCtxPushCurrent(cu_context));
	CU_CHECK(cuGraphicsD3D11RegisterResource(&cu_graphics_resource, d3d11_backbuffer, CU_GRAPHICS_REGISTER_FLAGS_NONE));
	CU_CHECK(cuGraphicsResourceSetMapFlags(cu_graphics_resource, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD));
	CU_CHECK(cuCtxPopCurrent(nullptr));

	CU_CHECK(cuMemAlloc(&device_ptr_converted_intermediate, 
						static_cast<uint64_t>(dimensions.target_width) * static_cast<uint64_t>(dimensions.target_height) * 3));
	CU_CHECK(cuMemAlloc(&device_ptr_converted_result, 
						static_cast<uint64_t>(dimensions.target_width) * static_cast<uint64_t>(dimensions.target_height) * 4));
}

void Decoder::Resize(uint32_t width, uint32_t height) {
	if(!d3d11_backbuffer) {
		return;
	}

	CalculateTargetDimensions(width, height, dimensions);
	
	// Reconfigure decoder
	CUVIDRECONFIGUREDECODERINFO reconfigure_params = {
		.ulWidth = 3840,
		.ulHeight = 2160,
		.ulTargetWidth = dimensions.target_width,
		.ulTargetHeight = dimensions.target_height,
		.ulNumDecodeSurfaces = NUMBER_OF_DECODE_SURFACES,
		.target_rect = {
			.left = dimensions.target_rect_left,
			.top = dimensions.target_rect_top,
			.right = dimensions.target_rect_right,
			.bottom = dimensions.target_rect_bottom
		}
	};
	CU_CHECK(cuCtxPushCurrent(cu_context));
	CU_CHECK(cuvidReconfigureDecoder(cu_decoder, &reconfigure_params));

	// Release all reference counted instances of the backbuffer
	CU_CHECK(cuGraphicsUnregisterResource(cu_graphics_resource));
	d3d11_backbuffer->Release();

	// Resize swapchain and create new render target view from back buffer
	WIN_CHECK(d3d11_swapchain->ResizeBuffers(0, dimensions.target_width, dimensions.target_height, DXGI_FORMAT_UNKNOWN, 0));
	WIN_CHECK(d3d11_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&d3d11_backbuffer)));

	CU_CHECK(cuGraphicsD3D11RegisterResource(&cu_graphics_resource, d3d11_backbuffer, CU_GRAPHICS_REGISTER_FLAGS_NONE));
	CU_CHECK(cuGraphicsResourceSetMapFlags(cu_graphics_resource, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD));
	CU_CHECK(cuCtxPopCurrent(nullptr));

	// Reallocate CUDA buffers
	CU_CHECK(cuMemFree(device_ptr_converted_intermediate));
	CU_CHECK(cuMemFree(device_ptr_converted_result));
	CU_CHECK(cuMemAlloc(&device_ptr_converted_intermediate,
						static_cast<uint64_t>(dimensions.target_width) * static_cast<uint64_t>(dimensions.target_height) * 3));
	CU_CHECK(cuMemAlloc(&device_ptr_converted_result,
						static_cast<uint64_t>(dimensions.target_width) * static_cast<uint64_t>(dimensions.target_height) * 4));

}

void Decoder::Decode(void *ptr, uint32_t size) {
	CUVIDSOURCEDATAPACKET data_packet {
		.payload_size = size,
		.payload = reinterpret_cast<uint8_t *>(ptr)
	};
	CU_CHECK(cuvidParseVideoData(cu_parser, &data_packet));
	WIN_CHECK(d3d11_swapchain->Present(0, 0));
}

//  0: fail, 
//  1: driver should not override ulMaxNumDecodeSurfaces
// >1: driver should override ulMaxNumDecodeSurfaces with returned value
int Decoder::SequenceCallback(CUVIDEOFORMAT *video_format) {

	CUVIDDECODECREATEINFO video_decode_info {
		.ulWidth = 3840,
		.ulHeight = 2160,
		.ulNumDecodeSurfaces = NUMBER_OF_DECODE_SURFACES,
		.CodecType = cudaVideoCodec_HEVC,
		.ChromaFormat = cudaVideoChromaFormat_420,
		.ulCreationFlags = cudaVideoCreate_PreferCUVID,
		.bitDepthMinus8 = 0,
		.ulMaxWidth = 3840,
		.ulMaxHeight = 2160,
		.OutputFormat = cudaVideoSurfaceFormat_NV12,
		.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave,
		.ulTargetWidth = dimensions.target_width,
		.ulTargetHeight = dimensions.target_height,
		.ulNumOutputSurfaces = 2,
		.vidLock = nullptr,
		.target_rect = {
			.left = dimensions.target_rect_left,
			.top = dimensions.target_rect_top,
			.right = dimensions.target_rect_right,
			.bottom = dimensions.target_rect_bottom
		}
	};
	
	CU_CHECK(cuCtxPushCurrent(cu_context));
	CU_CHECK(cuvidCreateDecoder(&cu_decoder, &video_decode_info));
	CU_CHECK(cuCtxPopCurrent(nullptr));

	return NUMBER_OF_DECODE_SURFACES;
}

// 0: fail
// 1: succeed
int Decoder::DecodeCallback(CUVIDPICPARAMS *pic_params) {

	CU_CHECK(cuvidDecodePicture(cu_decoder, pic_params));
	return 1;
}

// 0: fail
// 1: succeed
int Decoder::DisplayCallback(CUVIDPARSERDISPINFO *display_info) {
	CUVIDPROCPARAMS video_processing_params {
		.progressive_frame = display_info->progressive_frame,
		.second_field = display_info->repeat_first_field + 1,
		.top_field_first = display_info->top_field_first,
		.unpaired_field = display_info->repeat_first_field < 0
	};

	CUdeviceptr device_ptr_source_frame = 0;
	uint32_t source_pitch = 0;
	CU_CHECK(cuvidMapVideoFrame(cu_decoder, display_info->picture_index, 
								&device_ptr_source_frame, &source_pitch, 
								&video_processing_params));

	CUVIDGETDECODESTATUS decode_status {};
	CU_CHECK(cuvidGetDecodeStatus(cu_decoder, display_info->picture_index,
								  &decode_status));
	// Skip frame if decode is still in progress
	if(decode_status.decodeStatus == cuvidDecodeStatus_InProgress) {
		return 1;
	}
	assert(decode_status.decodeStatus == cuvidDecodeStatus_Success && "Decoding was unsuccessful");
	
	NppiSize size {
		.width = static_cast<int>(dimensions.target_width),
		.height = static_cast<int>(dimensions.target_height)
	};

	// Convert NV12 -> BGR
	// The first part of the NV12 encoded image holds the Y part of the YUV,
	// the second part holds the U and V parts interleaved
	Npp8u *dst[] = {
		reinterpret_cast<uint8_t *>(device_ptr_source_frame),
		reinterpret_cast<uint8_t *>(device_ptr_source_frame + (static_cast<uint64_t>(source_pitch) * size.height))
	};

	NPP_CHECK(nppiNV12ToBGR_8u_P2C3R(dst, source_pitch, 
									 reinterpret_cast<uint8_t *>(device_ptr_converted_intermediate), 
									 size.width * 3, size));

	// Convert BGR -> BGRA
	int order[] = { 0, 1, 2, 3 };
	NPP_CHECK(nppiSwapChannels_8u_C3C4R(reinterpret_cast<uint8_t *>(device_ptr_converted_intermediate), 
										size.width * 3,
									    reinterpret_cast<uint8_t *>(device_ptr_converted_result), 
										size.width * 4,
									    size, order, 0xFF));

	// Map and copy decoded image to backbuffer
	CU_CHECK(cuCtxPushCurrent(cu_context));
	CU_CHECK(cuGraphicsMapResources(1, &cu_graphics_resource, 0));
	CUarray mapped_array;
	CU_CHECK(cuGraphicsSubResourceGetMappedArray(&mapped_array, cu_graphics_resource, 0, 0));

	CUDA_MEMCPY2D memcpy_2d {
		.srcMemoryType = CU_MEMORYTYPE_DEVICE,
		.srcDevice = device_ptr_converted_result,
		.srcPitch = dimensions.target_width * sizeof(uint32_t),
		.dstMemoryType = CU_MEMORYTYPE_ARRAY,
		.dstArray = mapped_array,
		.WidthInBytes = dimensions.target_width * sizeof(uint32_t),
		.Height = dimensions.target_height
	};

	printf("Executing memcpy: %d:%d\n", dimensions.target_width, dimensions.target_height);
	CU_CHECK(cuMemcpy2D(&memcpy_2d));
	CU_CHECK(cuGraphicsUnmapResources(1, &cu_graphics_resource, 0));
	CU_CHECK(cuCtxPopCurrent(nullptr));

	CU_CHECK(cuvidUnmapVideoFrame(cu_decoder, device_ptr_source_frame));
	return 1;
}

void Decoder::Shutdown() {
	//ID3D11Debug *debug_interface;
	//d3d11_device->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void **>(&debug_interface));
	//debug_interface->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);

	CUVIDSOURCEDATAPACKET end_of_stream_packet {
		.flags = CUVID_PKT_ENDOFSTREAM,
		.payload_size = 0,
		.payload = nullptr
	};
	CU_CHECK(cuvidParseVideoData(cu_parser, &end_of_stream_packet));

	cuvidDestroyVideoParser(cu_parser);

	CU_CHECK(cuMemFree(device_ptr_converted_intermediate));
	CU_CHECK(cuMemFree(device_ptr_converted_result));
}
