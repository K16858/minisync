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

    char buf[MAX_LINE_LEN];
    char *msg = "TEST";
    int stdo = 1;
    char hostname_buf[MAX_LINE_LEN];
    char port_buf[MAX_LINE_LEN];
    char *hostname = hostname_buf;
    char *port = port_buf;

    if(argc > 1) {
        hostname = argv[1];
    } else {
        hostname = "localhost";
    }

    if(argc > 2) {
        port = argv[2];
    } else {
        port = "61001";
    }

    printf("HostName: %s\nPort: %s\n", hostname, port);

    if (getaddrinfo(hostname, port, &hints, &res)) {
        printf("Error\n");
        return 1;
    } else {
        printf("GetAddrInfo\n");
    }

    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if(connect(s, res->ai_addr, res->ai_addrlen)) {
        printf("Error\n");
        return 1;        
    } else {
        printf("Connected\n");
    }

    if(send(s, msg, 20, 0) < 0) {
        printf("Error\n");
        return 1;
    } else {
        printf("Send\n");
    }

    int count = recv(s, buf, MAX_LINE_LEN, 0);
    if (count > 0) {
        printf("Recv: %s\n", buf);
    } else {
        printf("Faild");
    }

    char line[MAX_LINE_LEN + 1];
    memset(line, 0, MAX_LINE_LEN + 1);
    
    while (1){
        memset(line, 0, MAX_LINE_LEN + 1);
        get_line(line,stdin);

        // if (request_file_op(s, line, TYPE_PUSH_FILE)) {
        //     send_file(s, line);
        // }
        if (request_file_op(s, line, TYPE_PULL_FILE)) {
            recv_file(s, line);
        }

        // send_file(s, line);

        // send_content(s, line);
        // send_end_message(s);

        int connection_closed = 0;
        int should_quit = 0;

        while (1) {
            int length;
            Content content_type;
            recv(s, &content_type, sizeof(content_type), MSG_WAITALL);

            int bytes = recv(s, &length, sizeof(length), MSG_WAITALL);
            if (bytes <= 0) {
                printf("Connection closed\n");
                connection_closed = 1;
                break;
            }

            if (length == 0) {
                should_quit = 1;
                break;
            }

            char *buffer = malloc(length + 1);
            recv(s, buffer, length, MSG_WAITALL);
            buffer[length] = '\0';

            printf("%s\n", buffer);
            free(buffer);
        }

        if (connection_closed || should_quit) {
            break;
        }
    }

    freeaddrinfo(res);

    return 0;
}
