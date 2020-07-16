#pragma once
#include <cstdint>
#include <Winsock2.h>
#include <Ws2tcpip.h>

struct DataHeader {
	uint32_t MAGIC;
	uint32_t size;
};

struct EncodedData {
	void *ptr;
	uint32_t size;
};

struct Client {
	WSAData wsa_data;
	SOCKET connection_socket;

	void *data_buffer;

	void Initialize();
	EncodedData ReceiveData();
	void Shutdown();
};