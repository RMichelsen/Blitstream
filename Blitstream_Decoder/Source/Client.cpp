#include "Client.h"
#include <cassert>
#include <cstdio>
#define WSA_CHECK(x) { \
int ret = x; \
if(ret != 0) printf("WSA Error: %s is 0x%08x in %s at line %d\n", #x, x, __FILE__, __LINE__); \
}
constexpr const char *PORT = "4646";

void Client::Initialize() {
	WSA_CHECK(WSAStartup(MAKEWORD(2, 2), &wsa_data));

    addrinfo hints {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };

    addrinfo *result;
    WSA_CHECK(getaddrinfo("127.0.0.1", PORT, &hints, &result));

    connection_socket = socket(result->ai_family,
                               result->ai_socktype,
                               result->ai_protocol);
    assert(connection_socket != INVALID_SOCKET && "Failed to create connection socket");

    WSA_CHECK(connect(connection_socket, result->ai_addr, 
                      static_cast<int>(result->ai_addrlen)));
    freeaddrinfo(result);

    data_buffer = VirtualAlloc(nullptr, 1024u * 1024u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

EncodedData Client::ReceiveData() {
    DataHeader header {};

    int result = recv(connection_socket, reinterpret_cast<char *>(&header), sizeof(DataHeader), 0);
    assert(result > 0 && "Failed to receive header");
    assert(header.MAGIC == 0x4646 && "Unrecognized header");

    // Return early if duplicate frame request
    if(header.size == 0) {
        return {};
    }

    uint32_t bytes_left = header.size;

    while(bytes_left > 0) {
        result = recv(connection_socket, reinterpret_cast<char *>(data_buffer) + header.size - bytes_left, bytes_left, 0);
        assert(result > 0 && "Failed to receive encoded data");
        bytes_left -= result;
    }
    assert(bytes_left == 0 && "Failed to receive encoded data");

    return EncodedData {
        .ptr = data_buffer,
        .size = header.size
    };
}

void Client::Shutdown() {
    VirtualFree(data_buffer, 0, MEM_RELEASE);
}

