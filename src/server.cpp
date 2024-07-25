#include "../include/compression.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <pthread.h>
#include <sstream>
#include <stdio.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

const int CHAR_ARRAY_LENGTH = 2048;
const int NUM_THREADS = 5;

struct arg_struct {
	int server_fd;
	sockaddr_in client_addr;
	socklen_t client_addr_len;
	std::string files_dir;
};

// https://developer.mozilla.org/en-US/docs/Web/HTTP/Messages
struct http_request_struct {
	std::string request_line_method;
	std::string request_line_target;

	std::string headers_host;
	std::string headers_user_agent;
	std::string headers_accept;
	std::string headers_accept_encoding;
	std::string headers_content_type;
	std::string headers_content_length;

	std::string body;
};

struct http_response_struct {
	std::string status_line_status_code;
	std::string status_line_status_text;

	std::string headers_content_encoding;
	std::string headers_content_type;
	std::string headers_content_length;

	std::string body;
};

void http_response_struct_to_string(const http_response_struct &response, std::string &response_string, const int content_length) {
	response_string = "HTTP/1.1 " + response.status_line_status_code + " " + response.status_line_status_text + "\r\n";
	response_string += "Content-Type: " + response.headers_content_type + "\r\n";
	std::printf("http_response_struct_to_string >>> %s", response.headers_content_encoding.c_str());
	if (!response.headers_content_encoding.empty()) {
		response_string += "Content-Encoding: " + response.headers_content_encoding + "\r\n";
	}
	response_string += "Content-Length: " + std::to_string(content_length) + "\r\n\r\n";
	response_string += response.body;
}

void send_response(const int client_fd, http_response_struct response, const int status_code, std::string_view body = "", const std::string &content_type = "text/plain") {
	std::printf("send_response()\n");

	std::string reason_phrase;
	switch (status_code) {
	case 200:
		reason_phrase = "OK";
		break;
	case 201:
		reason_phrase = "Created";
		break;
	case 404:
		reason_phrase = "Not Found";
		break;
	default:
		reason_phrase = "Unknown HTTP status code";
	}

	response.status_line_status_code = std::to_string(status_code);
	response.status_line_status_text = reason_phrase;
	response.headers_content_type = content_type;

	std::string response_string;
	Bytef compressed_body[CHAR_ARRAY_LENGTH];
	int body_length = 0;
	if (response.headers_content_encoding == "gzip") {
		std::cout << "About to compress body '" << body << "'" << std::endl;
		gzip_compress(body.data(), compressed_body, &body_length);
	} else {
		std::cout << "Not compressing body '" << body << "'" << std::endl;
		response.body = body;
		body_length = body.length();
	}

	http_response_struct_to_string(response, response_string, body_length);

	int response_length = response_string.length();
	std::cout << "Intending to send " << response_length << " bytes in total" << std::endl;

	if (response.headers_content_encoding == "gzip") {
		response_string.append(reinterpret_cast<const char *>(compressed_body), body_length);
	}

	int bytes_sent = send(client_fd, response_string.c_str(), response_length + body_length, 0);

	std::cout << "Intending to send " << body_length << " bytes as body" << std::endl;
	for (int i = 0; i < body_length; i++) {
		std::printf("Sending byte %x\n", compressed_body[i]);
		send(client_fd, (void *)&compressed_body[i], 1, 0);
	}

	std::cout << "Response sent:\n↓\n"
			  << response_string << "\n↑" << std::endl;
	std::cout << bytes_sent << " bytes sent" << std::endl;
}

void trim(std::string &str) {
	str.erase(str.begin(), std::ranges::find_if(str.begin(), str.end(), [](unsigned char ch) {
				  return !std::isspace(ch);
			  }));
	str.erase(std::ranges::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
				  return !std::isspace(ch);
			  }).base(),
			  str.end());
}

http_request_struct extract_request_info(const char *buffer) {
	std::printf("extract_request_info()\n");
	// get request target
	char copy_of_buffer[CHAR_ARRAY_LENGTH];
	strcpy(copy_of_buffer, buffer);

	http_request_struct request;

	// On a first call, the function expects a C string as argument for str,
	// whose first character is used as the starting location to scan for
	// tokens. In subsequent calls, the function expects a null pointer and uses
	// the position right after the end of the last token as the new starting
	// location for scanning.
	// https://cplusplus.com/reference/cstring/strtok/
	request.request_line_method = std::string(strtok(copy_of_buffer, " "));
	request.request_line_target = std::string(strtok(nullptr, " "));

	const char *p_end_of_request_line = strstr(buffer, "\r\n");
	const char *p_end_of_headers = strstr(p_end_of_request_line, "\r\n\r\n");
	const char *c_headers = p_end_of_request_line + 2;
	char headers[CHAR_ARRAY_LENGTH];
	strcpy(headers, c_headers);

	if (p_end_of_headers != nullptr) {
		headers[p_end_of_headers - p_end_of_request_line] = 0;
	}

	std::cout << "Just the headers:\n↓\n"
			  << headers << "\n↑" << std::endl;

	const char *header;
	char field[CHAR_ARRAY_LENGTH];
	char value[CHAR_ARRAY_LENGTH];
	char headers_copy[CHAR_ARRAY_LENGTH];
	char *headers_ptr = headers;

	// user strtok_r() since we're tokenizing two strings at the same time
	std::cout << "Individual headers:\n↓" << std::endl;
	bool done = false;
	while (!done) {
		header = strtok_r(headers_ptr, "\r\n", &headers_ptr);
		done = !header;
		if (!done) {
			std::cout << "Header: " << header << std::endl;

			strcpy(headers_copy, header);
			strcpy(field, strtok(headers_copy, ":"));
			std::cout << "\tField: " << field << std::endl;
			strcpy(value, header + strlen(field) + strlen(": "));
			std::cout << "\tValue: " << value << std::endl;

			if (strcmp("Host", field) == 0) {
				request.headers_host = std::string(value);
			} else if (strcmp("User-Agent", field) == 0) {
				request.headers_user_agent = std::string(value);
			} else if (strcmp("Accept", field) == 0) {
				request.headers_accept = std::string(value);
			} else if (strcmp("Accept-Encoding", field) == 0) {
				request.headers_accept_encoding = std::string(value);
			} else if (strcmp("Content-Type", field) == 0) {
				request.headers_content_type = std::string(value);
			} else if (strcmp("Content-Length", field) == 0) {
				request.headers_content_length = std::string(value);
			}
		}
	}
	std::cout << "↑" << std::endl;

	char body[CHAR_ARRAY_LENGTH];
	if (p_end_of_headers != nullptr) {
		strcpy(body, p_end_of_headers + 4);
		std::cout << "Just the body:\n↓\n"
				  << body << "\n↑" << std::endl;
	} else {
		strcpy(body, "");
		std::printf("No body\n");
	}
	request.body = std::string(body);

	return request;
}

void filename_to_content_type(const std::string &filename, std::string &content_type) {
	std::printf("Checking if '%s' contains '.html'\n", filename.c_str());
	if (filename.find(".html") != std::string::npos) {
		std::printf("It does\n");
		content_type = "text/html";
	} else {
		std::printf("It doesn't\n");
		content_type = "application/octet-stream";
	}
}

void endpoints(const int client_fd, const std::string &original_buffer, const std::string &files_dir) {
	std::printf("Endpoints()\n");
	http_request_struct request = extract_request_info(original_buffer.c_str());
	http_response_struct response;

	std::printf("endpoints > headers_content_encoding >>> '%s'\n", request.headers_accept_encoding.c_str());

	if (!request.headers_accept_encoding.empty()) {
		// client sent Accepted-Encoding header
		std::istringstream headers_accept_encoding_iss(request.headers_accept_encoding);
		std::string encoding_token;
		bool done = false;
		while (std::getline(headers_accept_encoding_iss, encoding_token, ',')) {
			trim(encoding_token);
			std::cout << "Encoding token: " << encoding_token << std::endl;
			if (encoding_token == "gzip") {
				response.headers_content_encoding = encoding_token;
				std::cout << "Client requested valid encoding: " << encoding_token << std::endl;
				break;
			} else {
				std::cout << "Client requested invalid encoding: " << encoding_token << std::endl;
			}
		}
	}

	std::printf("endpoints >>> %s", response.headers_content_encoding.c_str());

	std::printf("Request line target: %s\n", request.request_line_target.c_str());
	if (request.request_line_target == "/") {
		std::printf("Endpoint: /\n");
		send_response(client_fd, response, 200);
	} else if (request.request_line_target.substr(0, 6) == "/echo/") {
		std::printf("Endpoint: /echo/\n");
		std::string parameter = request.request_line_target.substr(6);
		std::cout << "Parameter: " << parameter << std::endl;
		send_response(client_fd, response, 200, parameter);
	} else if (request.request_line_target.substr(0, 7) == "/files/") {
		std::printf("Endpoint: /files/\n");
		std::string filename = request.request_line_target.substr(7);
		std::string absolute_path = files_dir + filename;
		std::cout << "Path of requested file: " << absolute_path << std::endl;

		if (request.request_line_method == "GET") {
			FILE *requested_file_fd = fopen(absolute_path.c_str(), "r");
			if (requested_file_fd != nullptr) {
				// file exists
				char file_contents[CHAR_ARRAY_LENGTH];
				fgets(file_contents, CHAR_ARRAY_LENGTH, requested_file_fd);
				std::cout << "File contents:\n↓\n"
						  << file_contents << "\n↑" << std::endl;
				file_contents[strlen(file_contents) + 1] = 0;
				std::string content_type;
				// setting correct Content-Type for .html files, so they're displayed instead of downloaded by Firefox
				// https://stackoverflow.com/a/28006229/2278742
				filename_to_content_type(filename, content_type);
				send_response(client_fd, response, 200, file_contents, content_type);
				fclose(requested_file_fd);
			} else {
				// file doesn't exist
				send_response(client_fd, response, 404);
			}
		} else if (request.request_line_method == "POST") {
			std::cout << "Writing file '" << absolute_path << "'" << std::endl;
			if (FILE *file_to_write_fd = fopen(absolute_path.c_str(), "w"); file_to_write_fd != nullptr) {
				fputs(request.body.c_str(), file_to_write_fd);
				fclose(file_to_write_fd);
			}

			send_response(client_fd, response, 201);
		}
	} else if (request.request_line_target.substr(0, 11) == "/user-agent") {
		std::printf("Endpoint: /user-agent\n");
		send_response(client_fd, response, 200, request.headers_user_agent);
	} else {
		std::printf("Endpoint: None\n");
		send_response(client_fd, response, 404);
	}
}

// https://www.ibm.com/docs/en/zos/2.4.0?topic=functions-pthread-create-create-thread
// https://hpc-tutorials.llnl.gov/posix/passing_args/
void *thread(void *arg) {
	std::printf("thread() entered\n");

	arg_struct *arg_cast_to_struct = (arg_struct *)arg;
	int server_fd = arg_cast_to_struct->server_fd;
	sockaddr_in client_addr = arg_cast_to_struct->client_addr;
	socklen_t client_addr_len = arg_cast_to_struct->client_addr_len;
	std::string files_dir = arg_cast_to_struct->files_dir;

	std::cout << "Waiting for a client to connect...\n";

	// The accept() function shall extract the first connection on the queue of pending connections, create a new socket with the same socket type protocol and address family as the specified socket, and allocate a new file descriptor for that socket.
	int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	std::cout << "Client connected\n";

	std::string request_buffer;
	request_buffer.resize(CHAR_ARRAY_LENGTH);
	// https://pubs.opengroup.org/onlinepubs/009695399/functions/recvfrom.html
	recvfrom(client_fd, request_buffer.data(), request_buffer.size(), 0, (struct sockaddr *)&client_addr, &client_addr_len);
	std::cout << "Message received:\n↓\n"
			  << request_buffer << "\n↑" << std::endl;

	endpoints(client_fd, request_buffer, files_dir);

	close(client_fd);

	pthread_exit(nullptr); // Don't use memory allocated in the thread for return value
}

int main(int argc, char *argv[]) {
	for (int i = 0; i < argc; i++) {
		std::cout << "argv: " << argv[i] << std::endl;
	}

	std::string files_dir;
	if (argc == 3) {
		files_dir = argv[2];
	} else {
		files_dir = "./";
	}

	// Flush after every std::cout / std::cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		std::cerr << "Failed to create server socket\n";
		return 1;
	}

	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors

	if (int reuse = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		std::cerr << "setsockopt failed\n";
		return 1;
	}

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(4221);

	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
		std::cerr << "Failed to bind to port 4221\n";
		return 1;
	}

	if (int connection_backlog = NUM_THREADS; listen(server_fd, connection_backlog) != 0) {
		std::cerr << "listen failed\n";
		return 1;
	}

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	// create as many threads as there are places in the queue created by listen()
	// https://www.ibm.com/docs/en/zos/2.4.0?topic=functions-pthread-create-create-thread
	// https://hpc-tutorials.llnl.gov/posix/passing_args/
	pthread_t thread_ids[NUM_THREADS];

	// pass arguments as struct
	arg_struct thread_args;
	thread_args.server_fd = server_fd;
	thread_args.client_addr = client_addr;
	thread_args.client_addr_len = client_addr_len;
	thread_args.files_dir = files_dir;

	// create 5 concurrent threads listen()ing to connections
	// when all 5 threads have terminated, create 5 new ones
	while (true) {
		for (int i = 0; i < NUM_THREADS; i++) {
			if (pthread_create(&thread_ids[i], nullptr, thread, (void *)&thread_args) != 0) {
				perror("pthread_create() failed");
				exit(1);
			}
		}

		void *ret;
		// wait for all threads to terminate
		for (int i = 0; i < NUM_THREADS; i++) {
			if (pthread_join(thread_ids[i], &ret) != 0) {
				perror("pthread_join() error");
				exit(3);
			}
		}
	}
}
