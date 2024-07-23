#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void send_response(const int client_fd, const int status_code, const char *body = "") {
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
						   "Content-Type: text/plain\r\nContent-Length: " + std::to_string(strlen(body)) + "\r\n\r\n" + body;
	int bytes_sent = send(client_fd, response.c_str(), response.length(), 0);

	std::cout << "Response sent:\n↓\n"
			  << response << "\n↑" << std::endl;

	std::cout << bytes_sent << " bytes sent" << std::endl;
}

int main(int argc, char **argv) {
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

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		std::cerr << "listen failed\n";
		return 1;
	}

	struct sockaddr_in client_addr;
	int client_addr_len = sizeof(client_addr);

	std::cout << "Waiting for a client to connect...\n";

	int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
	std::cout << "Client connected\n";

	char buffer[1024];
	// https://pubs.opengroup.org/onlinepubs/009695399/functions/recvfrom.html
	recvfrom(client_fd, (void *)buffer, 1024, 0,
			 (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
	std::cout << "Message received:\n↓\n"
			  << buffer << "\n↑" << std::endl;

	// get request target
	char request_target[1024];

	// On a first call, the function expects a C string as argument for str,
	// whose first character is used as the starting location to scan for
	// tokens. In subsequent calls, the function expects a null pointer and uses
	// the position right after the end of the last token as the new starting
	// location for scanning.
	// https://cplusplus.com/reference/cstring/strtok/
	strcpy(request_target, strtok(buffer, " "));
	strcpy(request_target, strtok(nullptr, " "));

	std::cout << "Request target: " << request_target << std::endl;

	// return value 0 means the strings are equal
	// https://cplusplus.com/reference/cstring/strcmp/
	if (strcmp("/", request_target) == 0) {
		send_response(client_fd, 200);
	} else if (memcmp("/echo/", request_target, 6) == 0) {
		char parameter[1024];
		strncpy(parameter, request_target + 6, strlen(request_target - 7));
		send_response(client_fd, 200, parameter);
	} else {
		send_response(client_fd, 404);
	}

	close(client_fd);
	close(server_fd);

	return 0;
}
