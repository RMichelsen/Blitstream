#include "Server.h"
#include <cassert>
#include <cstdio>

#define WSA_CHECK(x) { \
int ret = x; \
if(ret != 0) printf("WSA Error: %s is 0x%08x in %s at line %d\n", #x, x, __FILE__, __LINE__); \
}
constexpr const char *PORT = "4646";

void Server::Initialize(uint32_t width, uint32_t height) {
	WSA_CHECK(WSAStartup(MAKEWORD(2, 2), &wsa_data));

	addrinfo hints {
		.ai_flags = AI_PASSIVE,
		.ai_family = PF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP
	};

	addrinfo *result;
	WSA_CHECK(getaddrinfo(nullptr, PORT, &hints, &result));

	listen_socket = socket(result->ai_family,
						   result->ai_socktype,
						   result->ai_protocol);
	assert(listen_socket != INVALID_SOCKET && "Failed to create listen socket");

	WSA_CHECK(bind(listen_socket, result->ai_addr,
				   static_cast<int>(result->ai_addrlen)));

	freeaddrinfo(result);

	WSA_CHECK(listen(listen_socket, SOMAXCONN));

	printf("Waiting for connections on port %s\n", PORT);

	sockaddr_in client_addr;
	int client_addrlen = sizeof(client_addr);
	char ipv4_address[INET_ADDRSTRLEN];
	client_socket = accept(listen_socket, reinterpret_cast<sockaddr *>(&client_addr), &client_addrlen);
	assert(client_socket != INVALID_SOCKET && "Failed to create client socket");
	inet_ntop(AF_INET, &(client_addr.sin_addr), ipv4_address, INET_ADDRSTRLEN);
	printf("Connection established with IP: %s\n", ipv4_address);

	closesocket(listen_socket);

	// Send init packet
	InitMessage init_message {
		.MAGIC = 0x4646,
		.encoded_width = width,
		.encoded_height = height
	};
	int init_message_result = send(client_socket, reinterpret_cast<char *>(&init_message), sizeof(InitMessage), 0);
	assert(init_message_result != SOCKET_ERROR && "Failed to send initial message");
}

bool Server::SendData(void *ptr, uint32_t size) {
	static DataHeader header {
		.MAGIC = 0x4646,
		.size = 0
	};
	header.size = size;

	// Send header
	int result = send(client_socket, reinterpret_cast<char *>(&header), sizeof(DataHeader), 0);
	if(result == SOCKET_ERROR) return false;

	// Send encoded data if present
	if(header.size != 0) {
		result = send(client_socket, static_cast<char *>(ptr), static_cast<int>(size), 0);
		if(result == SOCKET_ERROR) return false;
	}
	// Otherwise the header will suffice to tell the 
	// client that it should simply duplicate the current frame

	return true;
}

void Server::Shutdown() {
	closesocket(client_socket);
}
