#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "protocol.h"
#include "utils.h"

#define MAX_LINE_LEN 1024
#define MAX_DATA 500
struct addrinfo hints, *res;

int main(int argc, char *argv[]) {
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM; /* TCP */
    hints.ai_family = AF_INET; /* IPv4 */

    char arg_buf[MAX_LINE_LEN];
    char *arg = arg_buf;
    char file_path_buf[MAX_LINE_LEN];
    char *file_path = file_path_buf;
    char hostname_buf[MAX_LINE_LEN];
    char port_buf[MAX_LINE_LEN];
    char *hostname = hostname_buf;
    char *port = port_buf;

    if(argc > 1) {
        arg = argv[1];
    } else {
        arg = "-v";
    }

    if (argc > 2) {
        file_path = argv[2];
    } else {
        file_path = NULL;
    }

    // if(argc > 1) {
    //     hostname = argv[1];
    // } else {
    //     hostname = "localhost";
    // }

    // if(argc > 2) {
    //     port = argv[2];
    // } else {
    //     port = "61001";
    // }

    hostname = "localhost";
    port = "61001";

    printf("HostName: %s\nPort: %s\n", hostname, port);

    int gai_err = getaddrinfo(hostname, port, &hints, &res);
    if (gai_err) {
        printf("GetAddrInfo Error: %s\n", gai_strerror(gai_err));
        return 1;
    } else {
        printf("GetAddrInfo\n");
    }

    int connected_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connected_socket < 0) {
        printf("Socket Error\n");
        freeaddrinfo(res);
        return 1;
    }

    if(connect(connected_socket, res->ai_addr, res->ai_addrlen)) {
        printf("Error\n");
        close(connected_socket);
        freeaddrinfo(res);
        return 1;        
    } else {
        printf("Connected\n");
    }

    if (send_hello(connected_socket) < 0) {
        printf("Hello send error\n");
        close(connected_socket);
        freeaddrinfo(res);
        return 1;
    }
    if (!recv_hello_ack(connected_socket)) {
        printf("Hello ack error\n");
        close(connected_socket);
        freeaddrinfo(res);
        return 1;
    }

    char line[MAX_LINE_LEN + 1];
    memset(line, 0, MAX_LINE_LEN + 1);

    int connection_closed = 0;
    int should_quit = 0;

    // get_line(line,stdin);

    if (strncmp(arg, "-v", 3) == 0) {
        printf("MiniSync Version 0.1.0\n");
    } else if (strncmp(arg, "pull", 5) == 0) {
        if (file_path == NULL) {
            printf("File path required for pull\n");
            close(connected_socket);
            freeaddrinfo(res);
            return 1;
        }
        if (request_file_op(connected_socket, file_path, TYPE_PULL_FILE)) {
            long long expected_size = -1;
            if (!recv_meta_size(connected_socket, &expected_size)) {
                printf("Meta size missing\n");
            } else {
                long long received_size = recv_file(connected_socket, file_path);
                if (received_size >= 0 && expected_size >= 0 && received_size != expected_size) {
                    printf("Size mismatch: expected %lld received %lld\n", expected_size, received_size);
                }
            }
        }
    } else if (strncmp(arg, "push", 5) == 0) {
        if (file_path == NULL) {
            printf("File path required for push\n");
            close(connected_socket);
            freeaddrinfo(res);
            return 1;
        }
        if (request_file_op(connected_socket, file_path, TYPE_PUSH_FILE)) {
            printf("push file: %s\n", file_path);
            long long size = get_file_size(file_path);
            if (size < 0) {
                printf("No such file: %s\n", file_path);
                close(connected_socket);
                freeaddrinfo(res);
                return 1;
            }
            send_meta_size(connected_socket, size);
            send_file(connected_socket, file_path);
        }
    } else {
        printf("Unknown argument\n");
    }

    while (1) {
        int length;
        Content content_type;
        recv(connected_socket, &content_type, sizeof(content_type), MSG_WAITALL);

        int bytes = recv(connected_socket, &length, sizeof(length), MSG_WAITALL);
        if (bytes <= 0) {
            printf("Connection closed\n");
            connection_closed = 1;
            break;
        }

        if (content_type == TYPE_DONE || length == 0) {
            should_quit = 1;
            break;
        }

        char *buffer = malloc(length + 1);
        recv(connected_socket, buffer, length, MSG_WAITALL);
        buffer[length] = '\0';

        printf("%s\n", buffer);
        free(buffer);

        if (connection_closed || should_quit) {
            break;
        }
    }

    close(connected_socket);
    freeaddrinfo(res);

    return 0;
}
