#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include "protocol.h"
#include "utils.h"

#define MAX_LINE_LEN 1024
#define DISCOVER_PORT 61002
struct addrinfo hints, *res;

int start_server(int port) {
    struct sockaddr_in sa;
    memset((char *)&sa, 0, sizeof(sa));

    int s = socket(AF_INET, SOCK_STREAM, 0);

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sa.sin_family = AF_INET; // IPv4
    sa.sin_port = htons(port); // 待ち受けポート番号
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(s, (struct sockaddr*)&sa, sizeof(sa));

    listen(s, 1);
    printf("Start... port=%d\n", port);

    return s;
}

int recv_connection(int socket) {
    int connect_s = accept(socket, NULL, NULL);
    if (connect_s >= 0) {
        printf("Connected client\n");
    }
    return connect_s;
}

static int start_discover_server() {
    struct sockaddr_in sa;
    memset((char *)&sa, 0, sizeof(sa));

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("discover socket");
        return -1;
    }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sa.sin_family = AF_INET;
    sa.sin_port = htons(DISCOVER_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        perror("discover bind");
        close(s);
        return -1;
    }

    printf("Discover listener on UDP %d\n", DISCOVER_PORT);
    return s;
}

int main(void) {
    struct global_config gcfg;
    if (load_global_config(&gcfg) < 0) {
        printf("Failed to load global config\n");
        return 1;
    }

    printf("=== Global Configuration ===\n");
    printf("Registered spaces: %d\n", gcfg.space_count);
    for (int i = 0; i < gcfg.space_count; i++) {
        printf("  [%d] %s (%s) - %s\n",
               i, gcfg.spaces[i].name, gcfg.spaces[i].id,
               gcfg.spaces[i].path);
    }
    printf("\n");

    if (gcfg.space_count == 0) {
        printf("No spaces registered. Use 'msync init' first.\n");
        free_global_config(&gcfg);
        return 1;
    }

    struct space_entry *space = &gcfg.spaces[0];
    printf("Starting server (multi-space support)\n");
    printf("Listening on port: 61001\n\n");

    int socket = start_server(61001);

    int discover_socket = start_discover_server();
    if (discover_socket >= 0) {
        int dpid = fork();
        if (dpid == 0) {
            // Discover responder
            while (1) {
                struct sockaddr_in client_addr;
                socklen_t addrlen = sizeof(client_addr);
                char buf[MAX_LINE_LEN + 1];
                int n = recvfrom(discover_socket, buf, MAX_LINE_LEN, 0, (struct sockaddr*)&client_addr, &addrlen);
                if (n <= 0) {
                    continue;
                }
                buf[n] = '\0';

                printf("Discover recv: %s\n", buf);

                if (strncmp(buf, "MSYNC_DISCOVER", 14) != 0) {
                    continue;
                }

                char hostname[64] = "localhost";
                gethostname(hostname, sizeof(hostname));
                hostname[sizeof(hostname) - 1] = '\0';
                
                for (int i = 0; i < gcfg.space_count; i++) {
                    char reply[MAX_LINE_LEN + 1];
                    snprintf(reply, sizeof(reply), "MSYNC_HERE %s %s %s %d",
                             gcfg.spaces[i].id, gcfg.spaces[i].name, hostname, 61001);
                    if (sendto(discover_socket, reply, strlen(reply), 0,
                            (struct sockaddr*)&client_addr, addrlen) < 0) {
                        perror("discover sendto");
                    }
                }
            }
            close(discover_socket);
            exit(0);
        }
    }

    while (1) {
        char line[MAX_LINE_LEN + 1];
        memset(line, 0, MAX_LINE_LEN);

        int connected_socket = recv_connection(socket);

        int pid = fork();

        if (pid < 0) {
            close(connected_socket);
            continue;
        } else if (pid == 0) {
            // Child process
            close(socket);
            
            int hello_len;
            Content hello_type;
            if (recv(connected_socket, &hello_type, sizeof(hello_type), MSG_WAITALL) <= 0) {
                close(connected_socket);
                exit(0);
            }
            if (recv(connected_socket, &hello_len, sizeof(hello_len), MSG_WAITALL) <= 0) {
                close(connected_socket);
                exit(0);
            }
            char space_id[128];
            if (hello_len > 0 && hello_len < sizeof(space_id)) {
                recv(connected_socket, space_id, hello_len, MSG_WAITALL);
                space_id[hello_len] = '\0';
            } else {
                close(connected_socket);
                exit(0);
            }
            if (hello_type != TYPE_HELLO) {
                close(connected_socket);
                exit(0);
            }
            
            struct space_entry *target_space = NULL;
            for (int i = 0; i < gcfg.space_count; i++) {
                if (strcmp(gcfg.spaces[i].id, space_id) == 0) {
                    target_space = &gcfg.spaces[i];
                    break;
                }
            }
            if (target_space == NULL) {
                send_error(connected_socket, "Unknown space ID");
                close(connected_socket);
                exit(0);
            }
            
            if (chdir(target_space->path) < 0) {
                send_error(connected_socket, "Failed to access space");
                close(connected_socket);
                exit(0);
            }
            
            struct msync_config config;
            memset(&config, 0, sizeof(config));
            if (load_config(".msync/config.json", &config) != 0) {
                send_error(connected_socket, "Failed to load space config");
                close(connected_socket);
                exit(0);
            }
            
            send_content(connected_socket, "HELLO_ACK", TYPE_HELLO_ACK);
            char token_buf[128];
            if (!recv_token(connected_socket, token_buf, sizeof(token_buf))) {
                send_error(connected_socket, "Token missing");
                close(connected_socket);
                exit(0);
            }
            if (strncmp(token_buf, config.token, sizeof(config.token)) != 0) {
                send_error(connected_socket, "Token invalid");
                close(connected_socket);
                exit(0);
            }
            while (1){
                int length;
                Content content_type;
                recv(connected_socket, &content_type, sizeof(content_type), MSG_WAITALL);

                if (content_type == TYPE_PUSH_FILE) {
                    printf("Content type: PUSH_FILE\n");
                } else if (content_type == TYPE_PULL_FILE) {
                    printf("Content type: PULL_FILE\n");
                } else if (content_type == TYPE_MESSAGE) {
                    printf("Content type: MESSAGE\n");
                } else if (content_type == TYPE_ERROR) {
                    printf("Content type: ERROR\n");
                } else if (content_type == TYPE_DONE) {
                    printf("Content type: DONE\n");
                } else if (content_type == NONE) {
                    printf("Content type: NONE\n");
                } else {
                    printf("Unknown type.\n");
                }

                int bytes = recv(connected_socket, &length, sizeof(length), MSG_WAITALL);
                if (bytes <= 0) {
                    printf("Connection closed\n");
                    break;
                }

                if (content_type == TYPE_DONE || length == 0) {
                    break;
                }

                char *buffer = malloc(length + 1);
                recv(connected_socket, buffer, length, MSG_WAITALL);
                buffer[length] = '\0';
                
                // process
                if (content_type == TYPE_PUSH_FILE) {
                    send_content(connected_socket, "[ACCEPT]", TYPE_PUSH_FILE);
                    if (create_snapshot(buffer) < 0) {
                        send_error(connected_socket, "Snapshot failed");
                    }
                    long long expected_size = -1;
                    if (!recv_meta_size(connected_socket, &expected_size)) {
                        send_error(connected_socket, "Meta size missing");
                        free(buffer);
                        break;
                    }
                    long long received_size = recv_file(connected_socket, buffer);
                    if (expected_size >= 0 && received_size >= 0 && received_size != expected_size) {
                        send_error(connected_socket, "Size mismatch");
                    }
                } else if (content_type == TYPE_PULL_FILE) {
                    send_content(connected_socket, "[ACCEPT]", TYPE_PULL_FILE);
                    long long file_size = get_file_size(buffer);
                    if (file_size < 0) {
                        send_error(connected_socket, "No such file");
                        free(buffer);
                        break;
                    }
                    send_meta_size(connected_socket, file_size);
                    send_file(connected_socket, buffer);
                } else if (content_type == TYPE_MESSAGE) {
                    send_content(connected_socket, "Content type: MESSAGE", TYPE_MESSAGE);
                } else if (content_type == TYPE_ERROR) {
                    send_content(connected_socket, "Content type: ERROR", TYPE_MESSAGE);
                } else if (content_type == TYPE_DONE) {
                    send_content(connected_socket, "Content type: DONE", TYPE_MESSAGE);
                } else {
                    send_error(connected_socket, "Unknown type.");
                }
                send_end_message(connected_socket);

                printf("%s\n", buffer);
                free(buffer);
            }
            close(connected_socket);
            exit(0);
        } else {
            // Parent process
            close(connected_socket);
        }
    }

    return 0;
}