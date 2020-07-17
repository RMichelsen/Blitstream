#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>

#include "Decoder.h"
#include "Client.h"

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	Decoder *decoder = reinterpret_cast<Decoder *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if(msg == WM_CREATE) {
		LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA,
						 reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));

		return 0;
	}

	switch(msg) {
	case WM_SIZE:
	{
		RECT client;
		GetClientRect(hwnd, &client);
		decoder->Resize(client.right - client.left, client.bottom - client.top);
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR p_cmd_line, int n_cmd_show) {
	// Hacky way of getting argv[1]...
	char *command_line_args = GetCommandLine();
	char *ip_address = strchr(command_line_args, 0x20) + 1;

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

	const char *window_class_name = "Blitstream_Class";
	const char *window_title = "Blitstream";

	WNDCLASSEX window_class {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = instance,
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.hbrBackground = nullptr,
		.lpszClassName = window_class_name
	};

	if(!RegisterClassEx(&window_class)) {
		return 1;
	}

	Decoder decoder {};

	HWND hwnd = CreateWindow(
		window_class_name,
		window_title,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		nullptr,
		nullptr,
		instance,
		&decoder
	);
	if(hwnd == NULL) return 1;
	ShowWindow(hwnd, n_cmd_show);

	decoder.Initialize(hwnd);

	Client client {};

	InitMessage init_message = client.Initialize(ip_address);
	
	char title[128];
	sprintf_s(title, sizeof(title), "Connected to %s", ip_address);
	SetWindowText(hwnd, title);

	decoder.encoded_width = init_message.encoded_width;
	decoder.encoded_height = init_message.encoded_height;

	while(true) {
		MSG msg;
		while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if(msg.message == WM_QUIT) {
				UnregisterClass(window_class_name, instance);
				decoder.Shutdown();
				return 0;
			}
		}

		EncodedData data = client.ReceiveData();

		if(data.result == EncodedDataResult::Success) {
			decoder.Decode(data.ptr, data.size);
		}
		else if(data.result == EncodedDataResult::Abort) {
			break;
		}
	}

	UnregisterClass(window_class_name, instance);
	decoder.Shutdown();
	return 0;
}
