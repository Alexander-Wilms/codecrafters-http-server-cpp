#include <cstring>
#include <stdio.h>
#include <zlib.h>

int gzip_compress(const char *input, Bytef *output, int *compressed_length) {
	printf("Data to compress:\n%s\n", input);
	z_stream strm;
	int ret;

	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;

	ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16,
					   8, Z_DEFAULT_STRATEGY);
	if (ret != Z_OK) {
		fprintf(stderr, "deflateInit2 failed: %d\n", ret);
		return 1;
	}

	// Set input data
	strm.next_in = (Bytef *)input;
	strm.avail_in = strlen(input);

	// Output buffer
	Bytef compressed_data[1024];
	strm.next_out = compressed_data;
	strm.avail_out = 1024;

	ret = deflate(&strm, Z_FINISH);
	if (ret != Z_STREAM_END) {
		fprintf(stderr, "deflate failed: %d\n", ret);
		return 1;
	}

	// Print compressed data

	// Matches the output of the following command
	//
	// echo -n abc | gzip | hexdump -C
	// 00000000  1f 8b 08 00 00 00 00 00  00 03 4b 4c 4a 06 00 c2  |..........KLJ...|
	// 00000010  41 24 35 03 00 00 00                              |A$5....|
	// 00000017

	printf("Compressed data:\n");
	for (int i = 0; i < strm.total_out; i++) {
		printf("%x ", compressed_data[i]);
	}
	printf("\n");

	deflateEnd(&strm);

	memcpy(output, compressed_data, strm.total_out);
	*compressed_length = strm.total_out;

	return 0;
}