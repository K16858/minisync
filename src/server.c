#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#define MAX_LINE_LEN 1024

struct addrinfo hints, *res;

enum Content {
    TYPE_FILE,
    TYPE_MESSAGE,
    NONE
};

int subst(char *str, char c1, char c2){
    int c=0;
    while(*str!='\0'){
        if(*str==c1){
            *str=c2;
            c++;
        }
        str++;
    }

    return c;
}

int send_end_message(int socket) {
    int length = 0;
    enum Content content_type = NONE;

    send(socket, &content_type, sizeof(content_type), 0);
    send(socket, &length, sizeof(length), 0);
    return 0;
}

int send_content(int socket, char *msg, enum Content content_type) {
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

int get_message(char *line, int socket) {
    while(1) {
        int byte = recv(socket, line, MAX_LINE_LEN, 0);

        if (byte <= 0) {
            printf("Connection closed\n");
            return 0;
        }

        subst(line,'\n','\0');
        if(line[0]=='\0'){
            send_content(socket, "InputError", TYPE_MESSAGE);
            send_end_message(socket);
        } else {
            return 1;
        }
    }
}

int start_server() {
    struct sockaddr_in sa;
    memset((char *)&sa, 0, sizeof(sa));

    int s = socket(AF_INET, SOCK_STREAM, 0);

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sa.sin_family = AF_INET; // IPv4
    sa.sin_port = htons(61001); // 待ち受けポート番号
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(s, (struct sockaddr*)&sa, sizeof(sa));

    listen(s, 1);
    printf("Start...\n");

    return s;
}

int recv_connection(int socket) {
    char buf[MAX_LINE_LEN+1];

    int connect_s = accept(socket, NULL, NULL);
    int count = recv(connect_s, buf, MAX_LINE_LEN, 0);
    if (count > 0) {
        printf("Connected client\n");
        send(connect_s, "Connected. This is a Server\n", 29, 0);
        return connect_s;
    }

    return 0;
}

int send_file(int socket, char *file) {
    FILE *fpr;
    fpr = fopen(file,"rb");
    char line[MAX_LINE_LEN+1];
    char msg[MAX_LINE_LEN+1];
    memset(msg, 0, sizeof(msg));

    if(fpr==NULL){
        sprintf(msg, "No such file: %s", file);
    }
    else{
        while(fgets(line,1025,fpr)!=NULL){
            subst(line,'\n','\0');
            int length = strlen(line);
            enum Content content_type = TYPE_FILE;

            if (send(socket, &content_type, sizeof(content_type), 0) < 0) {
                return -1;
            }

            if (send(socket, &length, sizeof(length), 0) < 0) {
                return -1;
            }
            
            if (send(socket, line, length, 0) < 0) {
                return -1;
            }

            memset(line, 0, sizeof(line));
        }
        fclose(fpr);
        strncpy(msg, "Complete send data", 19);
    }
    send_content(socket, msg, TYPE_MESSAGE);
    send_end_message(socket);
}

int recv_file(int socket, char *file) {
    FILE *fpw;
    fpw = fopen(file, "wb");

    while(1) {
        int length;
        enum Content content_type;

        recv(socket, &content_type, sizeof(content_type), MSG_WAITALL);
        if (content_type != TYPE_FILE) {
            break;
        }
        int bytes = recv(socket, &length, sizeof(length), MSG_WAITALL);

        if (length == 0) {
            break;
        }
        
        char *buffer = malloc(length + 1);
        recv(socket, buffer, length, MSG_WAITALL);
        buffer[length] = '\0';

        fprintf(fpw, "%s\n", buffer);
        free(buffer);
    }

    fclose(fpw);
    send_content(socket, "Complete recieve data", TYPE_MESSAGE);
    send_end_message(socket);
}

int main(void) {
    int socket = start_server();

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
            while (1){
                int length;
                enum Content content_type;
                recv(connected_socket, &content_type, sizeof(content_type), MSG_WAITALL);

                if (content_type == TYPE_FILE) {
                    printf("Content type: FILE\n");
                } else if (content_type == TYPE_MESSAGE) {
                    printf("Content type: MESSAGE\n");
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

                if (length == 0) {
                    break;
                }

                char *buffer = malloc(length + 1);
                recv(connected_socket, buffer, length, MSG_WAITALL);
                buffer[length] = '\0';
                
                // process
                // recv_file(connected_socket);
                if (content_type == TYPE_FILE) {
                    send_content(connected_socket, "Content type: FILE", TYPE_MESSAGE);
                    recv_file(connected_socket, buffer);
                } else if (content_type == TYPE_MESSAGE) {
                    send_content(connected_socket, "Content type: MESSAGE", TYPE_MESSAGE);
                } else if (content_type == NONE) {
                    send_content(connected_socket, "Content type: NONE", TYPE_MESSAGE);
                } else {
                    send_content(connected_socket, "Unknown type.", TYPE_MESSAGE);
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