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
#include <arpa/inet.h>
#include <time.h>
#include "protocol.h"
#include "utils.h"

#define MAX_LINE_LEN 1024
#define MAX_DATA 500
struct addrinfo hints, *res;

struct discovered_space {
    char id[64];
    char name[64];
    char hostname[64];
    char ip[64];
    int port;
};

static struct discovered_space discovered[MAX_DATA];
static int discovered_count = 0;

static int save_discover_cache() {
    FILE *fp = fopen(".msync/discover.txt", "w");
    if (fp == NULL) {
        printf("Failed to write .msync/discover.txt\n");
        return 1;
    }
    for (int i = 0; i < discovered_count; i++) {
        fprintf(fp, "%s\t%s\t%s\t%s\t%d\n",
                discovered[i].id,
                discovered[i].name,
                discovered[i].hostname,
                discovered[i].ip,
                discovered[i].port);
    }
    fclose(fp);
    return 0;
}

static int load_discover_cache() {
    FILE *fp = fopen(".msync/discover.txt", "r");
    if (fp == NULL) {
        return 1;
    }
    discovered_count = 0;
    char line[MAX_LINE_LEN + 1];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *id = strtok(line, "\t\n");
        char *name = strtok(NULL, "\t\n");
        char *hostname = strtok(NULL, "\t\n");
        char *ip = strtok(NULL, "\t\n");
        char *port = strtok(NULL, "\t\n");
        if (id == NULL || name == NULL || hostname == NULL || ip == NULL || port == NULL) {
            continue;
        }
        if (discovered_count >= MAX_DATA) {
            break;
        }
        snprintf(discovered[discovered_count].id, sizeof(discovered[discovered_count].id), "%s", id);
        snprintf(discovered[discovered_count].name, sizeof(discovered[discovered_count].name), "%s", name);
        snprintf(discovered[discovered_count].hostname, sizeof(discovered[discovered_count].hostname), "%s", hostname);
        snprintf(discovered[discovered_count].ip, sizeof(discovered[discovered_count].ip), "%s", ip);
        discovered[discovered_count].port = atoi(port);
        discovered_count++;
    }
    fclose(fp);
    return 0;
}


static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s push <path> [--host HOST] [--port PORT] [--token TOKEN] [--yes]\n", prog);
    printf("  %s pull <path> [--host HOST] [--port PORT] [--token TOKEN] [--yes]\n", prog);
    printf("  %s init <name>\n", prog);
    printf("  %s init --name NAME\n", prog);
    printf("  %s discover\n", prog);
    printf("  %s connect <id|index>\n", prog);
    printf("  %s -v | --version\n", prog);
    printf("  %s -h | --help\n", prog);
}

static void bytes_to_hex(const unsigned char *bytes, size_t len, char *out, size_t out_len) {
    static const char hex[] = "0123456789abcdef";
    size_t needed = len * 2 + 1;
    if (out_len < needed) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[bytes[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

static int generate_random_hex(char *out, size_t bytes_len) {
    unsigned char buf[64];
    if (bytes_len > sizeof(buf)) {
        return -1;
    }

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t r = read(fd, buf, bytes_len);
        close(fd);
        if (r == (ssize_t)bytes_len) {
            bytes_to_hex(buf, bytes_len, out, bytes_len * 2 + 1);
            return 0;
        }
    }

    for (size_t i = 0; i < bytes_len; i++) {
        buf[i] = (unsigned char)(rand() & 0xFF);
    }
    bytes_to_hex(buf, bytes_len, out, bytes_len * 2 + 1);
    return 0;
}

static int init_space(const char *init_name) {
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
    if (generate_random_hex(id, 16) < 0 || generate_random_hex(token, 16) < 0) {
        printf("Failed to generate id/token\n");
        return 1;
    }

    char name[64] = "msync-space";
    char hostname[64] = "";
    if (init_name != NULL && init_name[0] != '\0') {
        snprintf(name, sizeof(name), "%s", init_name);
    }
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
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
            "  \"hostname\": \"%s\",\n"
            "  \"token\": \"%s\",\n"
            "  \"port\": %d\n"
            "}\n",
            id, name, hostname, token, 61001);
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

static int discover_spaces() {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        printf("Discover socket error\n");
        return 1;
    }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(61002);

    const char *msg = "MSYNC_DISCOVER";
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(s, msg, strlen(msg), 0, (struct sockaddr*)&addr, sizeof(addr));

    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(s, msg, strlen(msg), 0, (struct sockaddr*)&addr, sizeof(addr));

    discovered_count = 0;
    printf("#  ID                               NAME            HOSTNAME        IP              PORT\n");
    while (1) {
        char buf[MAX_LINE_LEN + 1];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int n = recvfrom(s, buf, MAX_LINE_LEN, 0, (struct sockaddr*)&from, &fromlen);
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';

        char id[64], name[64], hostname[64];
        int port = 0;
        if (sscanf(buf, "MSYNC_HERE %63s %63s %63s %d", id, name, hostname, &port) != 4) {
            printf("Discover parse failed: %s\n", buf);
            continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        if (discovered_count < MAX_DATA) {
            snprintf(discovered[discovered_count].id, sizeof(discovered[discovered_count].id), "%s", id);
            snprintf(discovered[discovered_count].name, sizeof(discovered[discovered_count].name), "%s", name);
            snprintf(discovered[discovered_count].hostname, sizeof(discovered[discovered_count].hostname), "%s", hostname);
            snprintf(discovered[discovered_count].ip, sizeof(discovered[discovered_count].ip), "%s", ip);
            discovered[discovered_count].port = port;
            printf("%d  %-31s %-15s %-15s %-15s %d\n", discovered_count, id, name, hostname, ip, port);
            discovered_count++;
        }
    }

    if (discovered_count == 0) {
        printf("No spaces found\n");
    }

    save_discover_cache();

    close(s);
    return 0;
}

static int connect_space(const char *selector) {
    if (selector == NULL || selector[0] == '\0') {
        printf("connect requires id or index\n");
        return 1;
    }

    if (discovered_count == 0) {
        load_discover_cache();
    }
    if (discovered_count == 0) {
        printf("No discover cache. Run discover first.\n");
        return 1;
    }

    const struct discovered_space *target = NULL;
    int is_number = 1;
    for (size_t i = 0; selector[i] != '\0'; i++) {
        if (selector[i] < '0' || selector[i] > '9') {
            is_number = 0;
            break;
        }
    }

    if (is_number) {
        int index = atoi(selector);
        if (index >= 0 && index < discovered_count) {
            target = &discovered[index];
        }
    } else {
        for (int i = 0; i < discovered_count; i++) {
            if (strcmp(discovered[i].id, selector) == 0) {
                target = &discovered[i];
                break;
            }
        }
    }

    if (target == NULL) {
        printf("Target not found: %s\n", selector);
        return 1;
    }

    time_t now = time(NULL);
    if (append_target_json(".msync/targets.json", target->id, target->name, target->ip, target->port, (long)now) != 0) {
        printf("Failed to save target\n");
        return 1;
    }

    printf("Connected to %s (%s:%d)\n", target->name, target->ip, target->port);
    return 0;
}

int main(int argc, char *argv[]) {
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM; /* TCP */
    hints.ai_family = AF_INET; /* IPv4 */

    char *arg = NULL;
    char *file_path = NULL;
    char *hostname = NULL;
    char *port = NULL;
    char *token = NULL;
    char *init_name = NULL;
    char *connect_target = NULL;
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
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            init_name = argv[++i];
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
            arg = "init";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                init_name = argv[++i];
            }
        } else if (strcmp(argv[i], "discover") == 0) {
            arg = "discover";
        } else if (strcmp(argv[i], "connect") == 0) {
            arg = "connect";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                connect_target = argv[++i];
            }
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (arg == NULL) {
        print_usage(argv[0]);
        return 1;
    }

    if (strncmp(arg, "init", 5) == 0) {
        char input_name[64];
        if (init_name == NULL || init_name[0] == '\0') {
            printf("Enter space name: ");
            get_line(input_name, stdin);
            init_name = input_name;
        }
        if (init_name == NULL || init_name[0] == '\0') {
            printf("Name is required\n");
            return 1;
        }
        return init_space(init_name);
    }

    if (strncmp(arg, "discover", 9) == 0) {
        return discover_spaces();
    }

    if (strncmp(arg, "connect", 8) == 0) {
        return connect_space(connect_target);
    }

    if ((strncmp(arg, "push", 5) == 0 || strncmp(arg, "pull", 5) == 0) && (hostname == NULL || port == NULL)) {
        char last_host[64];
        int last_port = 0;
        if (load_last_target(".msync/targets.json", last_host, sizeof(last_host), &last_port) == 0) {
            hostname = strdup(last_host);
            static char port_buf[16];
            snprintf(port_buf, sizeof(port_buf), "%d", last_port);
            port = port_buf;
        }
    }

    if (hostname == NULL || port == NULL) {
        printf("Host/port not set. Use --host/--port or connect first.\n");
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
