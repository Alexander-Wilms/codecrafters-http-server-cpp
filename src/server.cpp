#include "../include/compression.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#define CHAR_ARRAY_LENGTH 2048
int NUM_THREADS = 5;

char files_dir[CHAR_ARRAY_LENGTH];

struct arg_struct {
	int server_fd;
	sockaddr_in client_addr;
	socklen_t client_addr_len;
} args;

// https://developer.mozilla.org/en-US/docs/Web/HTTP/Messages
struct http_request_struct {
	char request_line_method[CHAR_ARRAY_LENGTH] = "";
	char request_line_target[CHAR_ARRAY_LENGTH] = "";

	char headers_host[CHAR_ARRAY_LENGTH] = "";
	char headers_user_agent[CHAR_ARRAY_LENGTH] = "";
	char headers_accept[CHAR_ARRAY_LENGTH] = "";
	char headers_accept_encoding[CHAR_ARRAY_LENGTH] = "";
	char headers_content_type[CHAR_ARRAY_LENGTH] = "";
	char headers_content_length[CHAR_ARRAY_LENGTH] = "";

	char body[CHAR_ARRAY_LENGTH] = "";
};

struct http_response_struct {
	char status_line_status_code[CHAR_ARRAY_LENGTH] = "";
	char status_line_status_text[CHAR_ARRAY_LENGTH] = "";

	char headers_content_encoding[CHAR_ARRAY_LENGTH] = "";
	char headers_content_type[CHAR_ARRAY_LENGTH] = "";
	char headers_content_length[CHAR_ARRAY_LENGTH] = "";

	char body[CHAR_ARRAY_LENGTH] = "";
};

void http_response_struct_to_string(const http_response_struct response, char *response_string, const int content_length) {
	std::string response_std_string = std::string("HTTP/1.1 ") + response.status_line_status_code + " " + response.status_line_status_text + "\r\n" +
									  "Content-Type: " + response.headers_content_type + "\r\n";
	if (strcmp("", response.headers_content_encoding) != 0) {
		response_std_string = response_std_string + "Content-Encoding: " + response.headers_content_encoding + "\r\n";
	}
	response_std_string = response_std_string + "Content-Length:" + std::to_string(content_length) + "\r\n\r\n" +
						  response.body;

	strcpy(response_string, response_std_string.c_str());
}

void send_response(const int client_fd, http_response_struct response, const int status_code, const char *body = "", const char *content_type = "text/plain") {
	printf("send_response()\n");
	// https://pubs.opengroup.org/onlinepubs/007904875/functions/send.html
	char reason_phrase[CHAR_ARRAY_LENGTH];

	switch (status_code) {
	case 200:
		strcpy(reason_phrase, "OK");
		break;
	case 201:
		strcpy(reason_phrase, "Created");
		break;
	case 404:
		strcpy(reason_phrase, "Not Found");
		break;
	default:
		strcpy(reason_phrase, "Unknown HTTP status code");
	}

	strcpy(response.status_line_status_code, std::to_string(status_code).c_str());
	strcpy(response.status_line_status_text, reason_phrase);
	strcpy(response.headers_content_type, content_type);

	Bytef compressed_body[CHAR_ARRAY_LENGTH];
	int body_length;
	if (strcmp("gzip", response.headers_content_encoding) == 0) {
		std::cout << "About to compress body '" << body << "'" << std::endl;

		gzip_compress(body, compressed_body, &body_length);

	} else {
		strcpy(response.body, body);
		body_length = strlen(body);
	}

	char response_string[CHAR_ARRAY_LENGTH];
	http_response_struct_to_string(response, response_string, body_length);

	int response_length = strlen(response_string);
	std::cout << "Intending to send " << response_length << " bytes in total" << std::endl;

	if (strcmp("gzip", response.headers_content_encoding) == 0) {
		memcpy(response_string + response_length, compressed_body, body_length);
	}

	int bytes_sent = send(client_fd, response_string, response_length + body_length, 0);

	std::cout << "Intending to send " << body_length << " bytes as body" << std::endl;
	for (int i = 0; i < body_length; i++) {
		printf("Sending byte %x\n", compressed_body[i]);
		send(client_fd, (void *)compressed_body[i], 1, 0);
	}

	std::cout << "Response sent:\n↓\n"
			  << response_string << "\n↑" << std::endl;

	std::cout << bytes_sent << " bytes sent" << std::endl;
}

http_request_struct extract_request_info(const char *buffer) {
	printf("extract_request_info()\n");
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
	strcpy(request.request_line_method, strtok(copy_of_buffer, " "));
	strcpy(request.request_line_target, strtok(nullptr, " "));

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

	char *header;
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
				strcpy(request.headers_host, value);
			} else if (strcmp("User-Agent", field) == 0) {
				strcpy(request.headers_user_agent, value);
			} else if (strcmp("Accept", field) == 0) {
				strcpy(request.headers_accept, value);
			} else if (strcmp("Accept-Encoding", field) == 0) {
				strcpy(request.headers_accept_encoding, value);
			} else if (strcmp("Content-Type", field) == 0) {
				strcpy(request.headers_content_type, value);
			} else if (strcmp("Content-Length", field) == 0) {
				strcpy(request.headers_content_length, value);
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
		printf("No body\n");
	}
	strcpy(request.body, body);

	return request;
}

void endpoints(const int client_fd, const char *original_buffer) {
	printf("Endpoints()\n");
	http_request_struct request = extract_request_info(original_buffer);
	http_response_struct response;

	if (strcmp("", request.headers_accept_encoding) != 0) {
		// client sent Accepted-Encoding header
		char *encoding_token;
		bool first_call_to_strtok = true;

		bool done = false;
		while (encoding_token && !done) {
			if (first_call_to_strtok) {
				encoding_token = strtok(request.headers_accept_encoding, ", ");
				first_call_to_strtok = false;
			} else {
				encoding_token = strtok(nullptr, ", ");
			}

			if (encoding_token == nullptr) {
				break;
			}
			std::cout << "Encoding token: " << encoding_token << std::endl;

			if (strcmp("gzip", encoding_token) == 0) {
				strcpy(response.headers_content_encoding, encoding_token);
				std::cout << "Client requested valid encoding: " << encoding_token << std::endl;
				break;
			} else {
				std::cout << "Client requested invalid encoding: " << encoding_token << std::endl;
			}
		}
	}

	printf("Request line target: %s\n", request.request_line_target);
	// return value 0 means the strings are equal
	// https://cplusplus.com/reference/cstring/strcmp/
	if (strcmp("/", request.request_line_target) == 0) {
		printf("Endpoint: /\n");
		send_response(client_fd, response, 200);
	} else if (memcmp("/echo/", request.request_line_target, 6) == 0) {
		printf("Endpoint: /echo/\n");
		char parameter[CHAR_ARRAY_LENGTH];
		int param_len = strlen(request.request_line_target) - 6;
		strncpy(parameter, request.request_line_target + 6, param_len);
		parameter[param_len] = 0;
		std::cout << "Parameter: " << parameter << std::endl;
		send_response(client_fd, response, 200, parameter);
	} else if (memcmp("/files/", request.request_line_target, 7) == 0) {
		printf("Endpoint: /files/\n");
		char filename[CHAR_ARRAY_LENGTH];
		int param_len = strlen(request.request_line_target) - 6;
		strncpy(filename, request.request_line_target + 7, param_len);
		filename[param_len] = 0;
		char absolute_path[CHAR_ARRAY_LENGTH];
		strcpy(absolute_path, files_dir);
		strcat(absolute_path, filename);
		std::cout << "Path of requested file: " << absolute_path << std::endl;

		if (strcmp("GET", request.request_line_method) == 0) {
			FILE *requested_file_fd = fopen(absolute_path, "r");
			if (requested_file_fd != nullptr) {
				// file exists
				char file_contents[CHAR_ARRAY_LENGTH];
				fgets(file_contents, CHAR_ARRAY_LENGTH, requested_file_fd);
				std::cout << "File contents:\n↓\n"
						  << file_contents << "\n↑" << std::endl;
				file_contents[strlen(file_contents) + 1] = 0;
				char content_type[1024];
				// setting correct Content-Type for .html files, so they're displayed instead of downloaded by Firefox
				// https://stackoverflow.com/a/28006229/2278742
				printf("Checking if '%s' contains '.html'\n", filename);
				if (strstr(filename, ".html") != nullptr) {
					printf("It does\n");
					strcpy(content_type, "text/html");
				} else {
					printf("It doesn't\n");
					strcpy(content_type, "application/octet-stream");
				}
				send_response(client_fd, response, 200, file_contents, content_type);
				fclose(requested_file_fd);
			} else {
				// file doesn't exist
				send_response(client_fd, response, 404);
			}
		} else if (strcmp("POST", request.request_line_method) == 0) {
			std::cout << "Writing file '" << absolute_path << "'" << std::endl;
			FILE *file_to_write_fd = fopen(absolute_path, "w");
			fputs(request.body, file_to_write_fd);
			fclose(file_to_write_fd);
			send_response(client_fd, response, 201);
		}
	} else if (memcmp("/user-agent", request.request_line_target, 11) == 0) {
		printf("Endpoint: /user-agent\n");
		char user_agent_copy_of_buffer[CHAR_ARRAY_LENGTH];
		strcpy(user_agent_copy_of_buffer, original_buffer);
		char user_agent[CHAR_ARRAY_LENGTH];
		strcpy(user_agent, strtok(user_agent_copy_of_buffer, "\r\n"));
		std::cout << "Request line: " << user_agent << std::endl;
		strcpy(user_agent, std::strtok(nullptr, "\r\n"));
		std::cout << "Header 'Host': " << user_agent << std::endl;
		strcpy(user_agent, std::strtok(nullptr, "\r\n"));
		std::cout << "Header 'User agent': " << user_agent << std::endl;

		char just_the_user_agent[CHAR_ARRAY_LENGTH];
		int param_len = strlen(user_agent) - 12;
		strncpy(just_the_user_agent, user_agent + 12, param_len);
		send_response(client_fd, response, 200, just_the_user_agent);
	} else {
		printf("Endpoint: None\n");
		send_response(client_fd, response, 404);
	}
}

// https://www.ibm.com/docs/en/zos/2.4.0?topic=functions-pthread-create-create-thread
// https://hpc-tutorials.llnl.gov/posix/passing_args/
void *thread(void *arg) {
	char *ret;
	printf("thread() entered\n");

	int server_fd = args.server_fd;
	sockaddr_in client_addr = args.client_addr;
	socklen_t client_addr_len = args.client_addr_len;

	if ((ret = (char *)malloc(20)) == nullptr) {
		perror("malloc() error");
		exit(2);
	}
	strcpy(ret, "This is a test");

	std::cout << "Waiting for a client to connect...\n";

	// The accept() function shall extract the first connection on the queue of pending connections, create a new socket with the same socket type protocol and address family as the specified socket, and allocate a new file descriptor for that socket.
	int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
	std::cout << "Client connected\n";

	char request_buffer[CHAR_ARRAY_LENGTH];
	// https://pubs.opengroup.org/onlinepubs/009695399/functions/recvfrom.html
	recvfrom(client_fd, (void *)request_buffer, CHAR_ARRAY_LENGTH, 0,
			 (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
	std::cout << "Message received:\n↓\n"
			  << request_buffer << "\n↑" << std::endl;

	endpoints(client_fd, request_buffer);

	close(client_fd);

	pthread_exit(ret);
}

int main(int argc, char *argv[]) {
	for (int i = 0; i < argc; i++) {
		std::cout << "argv: " << argv[i] << std::endl;
	}

	if (argc == 3) {
		strcpy(files_dir, argv[2]);
	} else {
		strcpy(files_dir, "./");
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
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
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

	int connection_backlog = NUM_THREADS;
	if (listen(server_fd, connection_backlog) != 0) {
		std::cerr << "listen failed\n";
		return 1;
	}

	struct sockaddr_in client_addr;
	int client_addr_len = sizeof(client_addr);

	// create as many threads as there are places in the queue created by listen()
	// https://www.ibm.com/docs/en/zos/2.4.0?topic=functions-pthread-create-create-thread
	// https://hpc-tutorials.llnl.gov/posix/passing_args/
	pthread_t thread_ids[NUM_THREADS];

	// pass arguments as struct
	args.server_fd = server_fd;
	args.client_addr = client_addr;
	args.client_addr_len = client_addr_len;

	// create 5 concurrent threads listen()ing to connections
	// when all 5 threads ahave terminated, create 5 new ones
	while (true) {
		for (int i = 0; i < NUM_THREADS; i++) {
			if (pthread_create(&thread_ids[i], nullptr, thread, (void *)&args) != 0) {
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
