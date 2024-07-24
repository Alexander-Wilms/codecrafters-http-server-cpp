// Compile with
// clang src/zlib_example.cpp -lz

#include "compression.h"
#include <cstring>
#include <zlib.h>

int main() {
	char input[] = "abc";
	Bytef compressed_data[1024];
	int compressed_length;

	gzip_compress(input, compressed_data, &compressed_length);

	return 0;
}
