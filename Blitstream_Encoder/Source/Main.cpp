#define WIN32_LEAN_AND_MEAN

#include <cstdio>
#include <chrono>
#include <cassert>

#include "Encoder.h"
#include "Server.h"

int main(int argc, char **argv) {
	Encoder encoder {};
	encoder.Initialize();

	Server server {};
	server.Initialize();

	using namespace std::chrono;
	auto start = high_resolution_clock::now();
	auto end = high_resolution_clock::now();

	for(;;) {
		if(duration_cast<microseconds>(end - start).count() > 16666) {
			start = high_resolution_clock::now();
			EncodedData data = encoder.StartCapture();

			// Send data
			bool success = server.SendData(data.ptr, data.size);

			if(!success) {
				encoder.EndCapture();
				server.Shutdown();
				encoder.Shutdown();
				memset(&server, 0, sizeof(Server));
				memset(&encoder, 0, sizeof(Encoder));
				encoder.Initialize();
				server.Initialize();

				continue;
			}

			encoder.EndCapture();
		}
		end = high_resolution_clock::now();
	}
}
