#pragma once
#include <cstdint>
#include <Winsock2.h>
#include <Ws2tcpip.h>

struct InitMessage {
	uint32_t MAGIC;
	uint32_t encoded_width;
	uint32_t encoded_height;
};

struct DataHeader {
	uint32_t MAGIC;
	uint32_t size;
};

struct Server {
	WSAData wsa_data;
	SOCKET listen_socket;
	SOCKET client_socket;

	void Initialize(uint32_t width, uint32_t height);
	bool SendData(void *ptr, uint32_t size);
	void Shutdown();
};