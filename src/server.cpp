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

int NUM_THREADS = 5;
char files_dir[1024];

void send_response(const int client_fd, const int status_code, const char *body = "", const char *content_type = "text/plain") {
	// https://pubs.opengroup.org/onlinepubs/007904875/functions/send.html
	char reason_phrase[1024];

	switch (status_code) {
	case 200:
		strcpy(reason_phrase, "OK");
		break;
	case 404:
		strcpy(reason_phrase, "Not Found");
		break;
	default:
		strcpy(reason_phrase, "Unknown HTTP status code");
	}
	std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " + reason_phrase + "\r\n" +
						   "Content-Type: " + content_type + "\r\nContent-Length: " + std::to_string(strlen(body)) + "\r\n\r\n" + body;
	int bytes_sent = send(client_fd, response.c_str(), response.length(), 0);

	std::cout << "Response sent:\n↓\n"
			  << response << "\n↑" << std::endl;

	std::cout << bytes_sent << " bytes sent" << std::endl;
}

struct arg_struct {
	int server_fd;
	sockaddr_in client_addr;
	socklen_t client_addr_len;
} args;

struct request_struct {
	char method[1024];
	char target[1024];

	char header_host[1024];
	char header_user_agent[1024];
	char header_accept[1024];
	char header_content_type[1024];
	char header_content_length[1024];

	char body[1024];
};

request_struct extract_request_info(const char *buffer) {
	// get request target
	char copy_of_buffer[1024];
	char copy_of_buffer_2[1024];
	char copy_of_buffer_3[1024];
	strcpy(copy_of_buffer, buffer);
	strcpy(copy_of_buffer_2, buffer);
	strcpy(copy_of_buffer_3, buffer);

	request_struct request;

	// On a first call, the function expects a C string as argument for str,
	// whose first character is used as the starting location to scan for
	// tokens. In subsequent calls, the function expects a null pointer and uses
	// the position right after the end of the last token as the new starting
	// location for scanning.
	// https://cplusplus.com/reference/cstring/strtok/
	strcpy(request.method, strtok(copy_of_buffer, " "));
	strcpy(request.target, strtok(nullptr, " "));

	// char headers[1024];

	const char *p_end_of_request_line = strstr(buffer, "\r\n");
	const char *p_end_of_headers = strstr(p_end_of_request_line, "\r\n\r\n");
	const char *c_headers = p_end_of_request_line + 2;
	char headers[1024];
	strcpy(headers, c_headers);

	headers[strlen(headers) - 8] = 0;

	std::cout << "Just the headers:\n↓\n"
			  << headers << "\n↑" << std::endl;

	char *header;
	char field[1024];
	char value[1024];
	char header_copy[1024];
	char *header_ptr;
	char *field_ptr = field;

	header_ptr = header_copy;
	char *headers_ptr = headers;

	// user strtok_r() since we're tokenizing two strings at the same time
	std::cout << "Just the headers:\n↓" << std::endl;
	bool done = false;
	while (!done) {
		header = strtok_r(headers_ptr, "\r\n", &headers_ptr);
		done = !header;
		if (!done) {
			std::cout << "Header: " << header << std::endl;
		}
	}
	std::cout << "↑" << std::endl;

	const char *body = p_end_of_headers + 4;
	std::cout << "Just the body:\n↓\n"
			  << body << "\n↑" << std::endl;

	strcpy(request.body, body);

	return request;
}

void endpoints(int client_fd, char *original_buffer) {
	request_struct request = extract_request_info(original_buffer);

	// return value 0 means the strings are equal
	// https://cplusplus.com/reference/cstring/strcmp/
	if (strcmp("/", request.target) == 0) {
		send_response(client_fd, 200);
	} else if (memcmp("/echo/", request.target, 6) == 0) {
		char parameter[1024];
		int param_len = strlen(request.target) - 6;
		strncpy(parameter, request.target + 6, param_len);
		parameter[param_len] = 0;
		std::cout << "Parameter: " << parameter << std::endl;
		send_response(client_fd, 200, parameter);
	} else if (memcmp("/files/", request.target, 7) == 0) {
		char filename[1024];
		int param_len = strlen(request.target) - 6;
		strncpy(filename, request.target + 7, param_len);
		filename[param_len] = 0;
		char absolute_path[1024];
		strcpy(absolute_path, files_dir);
		strcat(absolute_path, filename);
		std::cout << "Path of requested file: " << absolute_path << std::endl;

		if (strcmp("GET", request.method) == 0) {
			FILE *requested_file_fd = fopen(absolute_path, "r");
			if (requested_file_fd != 0) {
				// file exists
				char file_contents[1024];
				fgets(file_contents, 1024, requested_file_fd);
				std::cout << "File contents:\n↓\n"
						  << file_contents << "\n↑" << std::endl;
				file_contents[strlen(file_contents) + 1] = 0;
				send_response(client_fd, 200, file_contents, "application/octet-stream");
				fclose(requested_file_fd);
			} else {
				// file doesn't exist
				send_response(client_fd, 404);
			}
		} else if (strcmp("POST", request.method) == 0) {
			FILE *file_to_write_fd = fopen(absolute_path, "w");
			fputs(request.body, file_to_write_fd);
			fclose(file_to_write_fd);
		}
	} else if (memcmp("/user-agent", request.target, 11) == 0) {
		char user_agent_copy_of_buffer[1024];
		strcpy(user_agent_copy_of_buffer, original_buffer);
		char user_agent[1024];
		strcpy(user_agent, strtok(user_agent_copy_of_buffer, "\r\n"));
		std::cout << "Request line: " << user_agent << std::endl;
		strcpy(user_agent, std::strtok(nullptr, "\r\n"));
		std::cout << "Header 'Host': " << user_agent << std::endl;
		strcpy(user_agent, std::strtok(nullptr, "\r\n"));
		std::cout << "Header 'User agent': " << user_agent << std::endl;

		char just_the_user_agent[1024];
		int param_len = strlen(user_agent) - 12;
		strncpy(just_the_user_agent, user_agent + 12, param_len);
		send_response(client_fd, 200, just_the_user_agent);
	} else {
		send_response(client_fd, 404);
	}
}

void *thread(void *arg) {
	char *ret;
	printf("thread() entered\n");

	int server_fd = args.server_fd;
	sockaddr_in client_addr = args.client_addr;
	socklen_t client_addr_len = args.client_addr_len;

	if ((ret = (char *)malloc(20)) == NULL) {
		perror("malloc() error");
		exit(2);
	}
	strcpy(ret, "This is a test");

	std::cout << "Waiting for a client to connect...\n";

	// The accept() function shall extract the first connection on the queue of pending connections, create a new socket with the same socket type protocol and address family as the specified socket, and allocate a new file descriptor for that socket.
	int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
	std::cout << "Client connected\n";

	char request_buffer[1024];
	// https://pubs.opengroup.org/onlinepubs/009695399/functions/recvfrom.html
	recvfrom(client_fd, (void *)request_buffer, 1024, 0,
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

	strcpy(files_dir, argv[2]);

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

	for (int i = 0; i < NUM_THREADS; i++) {
		if (pthread_create(&thread_ids[i], NULL, thread, (void *)&args) != 0) {
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
	close(server_fd);

	return 0;
}
