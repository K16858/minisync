#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_LINE_LEN 1024

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

void get_line(char *line, FILE *stream){
    while(1){
        line[0] = '\0';
        if(fgets(line,MAX_LINE_LEN+1,stream)==NULL){
            fprintf(stderr,"Input ERROR\n");
        }
        subst(line,'\n','\0');
        if(line[0]=='\0'){
            fprintf(stderr,"Input ERROR\n");           
        }
        else{
            break;
        }
    }
}

int send_message(int socket, char *msg) {
    int length = strlen(msg);
    enum Content content_type = TYPE_MESSAGE;

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
    enum Content content_type = NONE;

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
    } else{
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
    send_message(socket, msg);
    send_end_message(socket);
}

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

        send_file(s, line);

        // send_message(s, line);
        // send_end_message(s);

        int connection_closed = 0;
        int should_quit = 0;

        while (1) {
            int length;
            enum Content content_type;
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
