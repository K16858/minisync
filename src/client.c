#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include "protocol.h"
#include "utils.h"

#define MAX_LINE_LEN 1024
#define MAX_DATA 500
struct addrinfo hints, *res;

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s push <path> [--host HOST] [--port PORT] [--token TOKEN] [--yes]\n", prog);
    printf("  %s pull <path> [--host HOST] [--port PORT] [--token TOKEN] [--yes]\n", prog);
    printf("  %s init\n", prog);
    printf("  %s discover\n", prog);
    printf("  %s connect\n", prog);
    printf("  %s -v | --version\n", prog);
    printf("  %s -h | --help\n", prog);
}

static int init_space() {
    const char *dir = ".msync";
    struct stat st;

    if (stat(dir, &st) == 0) {
        printf("%s already exists\n", dir);
        return 1;
    }

    if (mkdir(dir, 0700) < 0) {
        printf("Failed to create %s: %s\n", dir, strerror(errno));
        return 1;
    }

    char id[33];
    char token[33];
    for (int i = 0; i < 32; i++) {
        id[i] = "0123456789abcdef"[rand() % 16];
        token[i] = "0123456789abcdef"[rand() % 16];
    }

    char name[64] = "msync-space";
    char hostname[64];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        snprintf(name, sizeof(name), "%s", hostname);
    }

    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/config.json", dir);
    FILE *cfg = fopen(config_path, "w");
    if (cfg == NULL) {
        printf("Failed to write %s: %s\n", config_path, strerror(errno));
        return 1;
    }
    fprintf(cfg,
            "{\n"
            "  \"id\": \"%s\",\n"
            "  \"name\": \"%s\",\n"
            "  \"token\": \"%s\",\n"
            "  \"port\": %d\n"
            "}\n",
            id, name, token, 61001);
    fclose(cfg);

    char targets_path[PATH_MAX];
    snprintf(targets_path, sizeof(targets_path), "%s/targets.json", dir);
    FILE *tgt = fopen(targets_path, "w");
    if (tgt == NULL) {
        printf("Failed to write %s: %s\n", targets_path, strerror(errno));
        return 1;
    }
    fprintf(tgt, "[]\n");
    fclose(tgt);

    printf("Initialized %s\n", dir);
    return 0;
}

int main(int argc, char *argv[]) {
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM; /* TCP */
    hints.ai_family = AF_INET; /* IPv4 */

    char *arg = NULL;
    char *file_path = NULL;
    char *hostname = "localhost";
    char *port = "61001";
    char *token = NULL;
    int yes = 0;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-v", 3) == 0 || strcmp(argv[i], "--version") == 0) {
            arg = "-v";
        } else if (strncmp(argv[i], "-h", 3) == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            hostname = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = argv[++i];
        } else if (strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            token = argv[++i];
        } else if (strcmp(argv[i], "--yes") == 0) {
            yes = 1;
        } else if (strcmp(argv[i], "push") == 0 || strcmp(argv[i], "pull") == 0) {
            arg = argv[i];
            if (i + 1 < argc) {
                file_path = argv[++i];
            }
        } else if (strcmp(argv[i], "init") == 0) {
            return init_space();
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (arg == NULL) {
        print_usage(argv[0]);
        return 1;
    }

    printf("HostName: %s\nPort: %s\n", hostname, port);
    if (token != NULL) {
        printf("Token: [set]\n");
    }
    if (yes) {
        printf("Confirm: disabled\n");
    }

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
