#include "protocol.h"
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

static int send_all(int socket, const void *buf, int length) {
    int sent = 0;
    const char *p = (const char *)buf;
    while (sent < length) {
        int n = send(socket, p + sent, length - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += n;
    }
    return sent;
}

static int recv_all(int socket, void *buf, int length) {
    int received = 0;
    char *p = (char *)buf;
    while (received < length) {
        int n = recv(socket, p + received, length - received, 0);
        if (n <= 0) {
            return -1;
        }
        received += n;
    }
    return received;
}

int send_content(int socket, char *msg, Content content_type) {
    int length = strlen(msg);

    if (send_all(socket, &content_type, sizeof(content_type)) < 0) {
        return -1;
    }

    if (send_all(socket, &length, sizeof(length)) < 0) {
        return -1;
    }
    
    if (send_all(socket, msg, length) < 0) {
        return -1;
    }
    
    return 0;
}

int send_end_message(int socket) {
    int length = 0;
    Content content_type = TYPE_DONE;

    send_all(socket, &content_type, sizeof(content_type));
    send_all(socket, &length, sizeof(length));
    return 0;
}

int send_file(int socket, char *file) {
    FILE *fpr;
    fpr = fopen(file, "rb");
    if (fpr == NULL) {
        char msg[MAX_LINE_LEN + 1];
        snprintf(msg, sizeof(msg), "No such file: %s", file);
        send_error(socket, msg);
        send_end_message(socket);
        return -1;
    }

    unsigned char buffer[4096];
    size_t nread;
    while ((nread = fread(buffer, 1, sizeof(buffer), fpr)) > 0) {
        Content content_type = TYPE_PUSH_FILE;
        int length = (int)nread;
        if (send_all(socket, &content_type, sizeof(content_type)) < 0) {
            fclose(fpr);
            return -1;
        }
        if (send_all(socket, &length, sizeof(length)) < 0) {
            fclose(fpr);
            return -1;
        }
        if (send_all(socket, buffer, length) < 0) {
            fclose(fpr);
            return -1;
        }
    }

    fclose(fpr);
    send_end_message(socket);
    return 0;
}

int send_file_list(int socket) {
    char *base_dir = "./";
    struct file_entry entries[MAX_DATA];
    char msg[MAX_LINE_LEN+1];
    char size_buf[64];
    char mtime_buf[64];

    int num_files = get_file_list(base_dir, entries);

    if (num_files > 0) {
        for (int i=0;i<num_files;i++) {
            send_content(socket, entries[i].name, TYPE_FILE_LIST);
            snprintf(size_buf, sizeof(size_buf), "%d", entries[i].size);
            snprintf(mtime_buf, sizeof(mtime_buf), "%d", entries[i].mtime);
            send_content(socket, size_buf, TYPE_FILE_LIST);
            send_content(socket, mtime_buf, TYPE_FILE_LIST);
        }
        strncpy(msg, "Complete send data", 19);
    } else {
        strncpy(msg, "Failed send data", 17);
    }

    send_content(socket, msg, TYPE_MESSAGE);
    send_end_message(socket);
}

long long recv_file(int socket, char *file) {
    FILE *fpw;
    fpw = fopen(file, "wb");
    if (fpw == NULL) {
        return -1;
    }

    long long total_written = 0;
    while(1) {
        int length;
        Content content_type;

        if (recv_all(socket, &content_type, sizeof(content_type)) < 0) {
            break;
        }
        if (recv_all(socket, &length, sizeof(length)) < 0) {
            break;
        }

        if (content_type == TYPE_DONE || length == 0) {
            break;
        }

        char *buffer = malloc(length);
        if (recv_all(socket, buffer, length) < 0) {
            free(buffer);
            break;
        }

        if (content_type == TYPE_PUSH_FILE) {
            fwrite(buffer, 1, length, fpw);
            total_written += length;
        }
        free(buffer);
    }

    fclose(fpw);
    send_end_message(socket);
    return total_written;
}

int request_file_op(int socket, char *file, Content content_type) {
    int length;
    Content response_type;
    send_content(socket, file, content_type);
    
    if (recv_all(socket, &response_type, sizeof(response_type)) <= 0) {
        return 0;
    }

    int bytes = recv_all(socket, &length, sizeof(length));
    if (bytes <= 0) {
        return 0;
    }
    if (length == 0) {
        return 0;
    }
    
    char *buffer = malloc(length + 1);
    recv_all(socket, buffer, length);
    buffer[length] = '\0';

    int accepted = (response_type == content_type) && (strncmp(buffer, "[ACCEPT]", 9) == 0);
    free(buffer);
    return accepted;
}

int send_hello(int socket) {
    return send_content(socket, "HELLO", TYPE_HELLO);
}

int recv_hello_ack(int socket) {
    int length;
    Content content_type;

    if (recv_all(socket, &content_type, sizeof(content_type)) <= 0) {
        return 0;
    }
    if (recv_all(socket, &length, sizeof(length)) <= 0) {
        return 0;
    }
    if (length > 0) {
        char *buffer = malloc(length + 1);
        recv_all(socket, buffer, length);
        buffer[length] = '\0';
        free(buffer);
    }

    return (content_type == TYPE_HELLO_ACK);
}

int send_error(int socket, char *msg) {
    return send_content(socket, msg, TYPE_ERROR);
}

int send_meta_size(int socket, long long size) {
    char msg[MAX_LINE_LEN + 1];
    snprintf(msg, sizeof(msg), "%lld", size);
    return send_content(socket, msg, TYPE_META);
}

int recv_meta_size(int socket, long long *size) {
    int length;
    Content content_type;

    if (recv_all(socket, &content_type, sizeof(content_type)) <= 0) {
        return 0;
    }
    if (recv_all(socket, &length, sizeof(length)) <= 0) {
        return 0;
    }
    if (length <= 0) {
        return 0;
    }

    char *buffer = malloc(length + 1);
    recv_all(socket, buffer, length);
    buffer[length] = '\0';

    if (content_type != TYPE_META) {
        free(buffer);
        return 0;
    }

    *size = atoll(buffer);
    free(buffer);
    return 1;
}
