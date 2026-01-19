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

int send_content(int socket, char *msg, Content content_type) {
    int length = strlen(msg);

    if (send(socket, &content_type, sizeof(content_type), 0) < 0) {
        return -1;
    }

    if (send(socket, &length, sizeof(length), 0) < 0) {
        return -1;
    }
    
    if (send(socket, msg, length, 0) < 0) {
        return -1;
    }
    
    return 0;
}

int send_end_message(int socket) {
    int length = 0;
    Content content_type = TYPE_DONE;

    send(socket, &content_type, sizeof(content_type), 0);
    send(socket, &length, sizeof(length), 0);
    return 0;
}

int send_file(int socket, char *file) {
    FILE *fpr;
    fpr = fopen(file,"rb");
    char line[MAX_LINE_LEN+1];
    char msg[MAX_LINE_LEN+1];
    memset(msg, 0, MAX_LINE_LEN);

    if(fpr==NULL){
        sprintf(msg, "No such file: %s", file);
    } else {
        while(fgets(line,1025,fpr)!=NULL){
            send_content(socket, line, TYPE_PUSH_FILE);

            memset(line, 0, sizeof(line));
        }
        fclose(fpr);
        strncpy(msg, "Complete send data", 19);
    }
    send_content(socket, msg, TYPE_MESSAGE);
    send_end_message(socket);
}

int send_file_list(int socket) {
    char *base_dir = "./";
    struct file_entry entries[MAX_DATA];
    char msg[MAX_LINE_LEN+1];

    int num_files = get_file_list(base_dir, entries);

    if (num_files > 0) {
        for (int i=0;i<num_files;i++) {
            send_content(socket, entries[i]->name, TYPE_FILE_LIST);
            send_content(socket, entries[i]->size, TYPE_FILE_LIST);
            send_content(socket, entries[i]->mtime, TYPE_FILE_LIST);
        }
        strncpy(msg, "Complete send data", 19);
    } else {
        strncpy(msg, "Failed send data", 17);
    }

    send_content(socket, msg, TYPE_MESSAGE);
    send_end_message(socket);
}

int recv_file(int socket, char *file) {
    FILE *fpw;
    fpw = fopen(file, "wb");

    while(1) {
        int length;
        Content content_type;

        recv(socket, &content_type, sizeof(content_type), MSG_WAITALL);
        if (content_type != TYPE_PUSH_FILE) {
            break;
        }
        int bytes = recv(socket, &length, sizeof(length), MSG_WAITALL);

        if (length == 0) {
            break;
        }
        
        char *buffer = malloc(length + 1);
        recv(socket, buffer, length, MSG_WAITALL);
        buffer[length] = '\0';

        fprintf(fpw, "%s", buffer);
        free(buffer);
    }

    fclose(fpw);
    send_content(socket, "Complete recieve data", TYPE_MESSAGE);
    send_end_message(socket);
}

int request_file_op(int socket, char *file, Content content_type) {
    int length;
    Content response_type;
    send_content(socket, file, content_type);
    
    if (recv(socket, &response_type, sizeof(response_type), MSG_WAITALL) <= 0) {
        return 0;
    }

    int bytes = recv(socket, &length, sizeof(length), MSG_WAITALL);
    if (bytes <= 0) {
        return 0;
    }
    if (length == 0) {
        return 0;
    }
    
    char *buffer = malloc(length + 1);
    recv(socket, buffer, length, MSG_WAITALL);
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

    if (recv(socket, &content_type, sizeof(content_type), MSG_WAITALL) <= 0) {
        return 0;
    }
    if (recv(socket, &length, sizeof(length), MSG_WAITALL) <= 0) {
        return 0;
    }
    if (length > 0) {
        char *buffer = malloc(length + 1);
        recv(socket, buffer, length, MSG_WAITALL);
        buffer[length] = '\0';
        free(buffer);
    }

    return (content_type == TYPE_HELLO_ACK);
}

int send_error(int socket, char *msg) {
    return send_content(socket, msg, TYPE_ERROR);
}
