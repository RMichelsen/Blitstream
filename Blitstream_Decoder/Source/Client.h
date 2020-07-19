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

enum class EncodedDataResult : uint32_t {
	Success,
	Duplicate,
	Abort
};

struct EncodedData {
	EncodedDataResult result;
	void *ptr;
	uint32_t size;
};

struct Client {
	WSAData wsa_data;
	SOCKET connection_socket;

	void *data_buffer;

	InitMessage Initialize(const char *ip_address);
	EncodedData ReceiveData();
	void Shutdown();
};