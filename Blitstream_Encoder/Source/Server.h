#pragma once
#include <cstdint>
#include <Winsock2.h>
#include <Ws2tcpip.h>

struct DataHeader {
	uint32_t MAGIC;
	uint32_t size;
};

struct Server {
	WSAData wsa_data;
	SOCKET listen_socket;
	SOCKET client_socket;

	void Initialize();
	bool SendData(void *ptr, uint32_t size);
	void Shutdown();
};