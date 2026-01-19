#ifndef PROTOCOL_H
#define PROTOCOL_H

typedef enum {
    TYPE_HELLO,
    TYPE_HELLO_ACK,
    TYPE_PULL_FILE,
    TYPE_PUSH_FILE,
    TYPE_FILE_LIST,
    TYPE_MESSAGE,
    TYPE_ERROR,
    TYPE_DONE,
    NONE
} Content;

int send_content(int socket, char *msg, Content content_type);
int send_end_message(int socket);
int send_file(int socket, char *file);
int send_file_list(int socket);
int recv_file(int socket, char *file);
int recv_file_list(int socket, struct file_entry entries[]);
int request_file_op(int socket, char *file, Content content_type);
int send_hello(int socket);
int recv_hello_ack(int socket);
int send_error(int socket, char *msg);

#endif