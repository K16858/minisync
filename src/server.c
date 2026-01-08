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
    send(socket, &length, sizeof(length), 0);
    return 0;
}

int send_message(int socket, char *msg) {
    int length = strlen(msg);

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
            send_message(socket, "InputError");
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
            while (get_message(line, connected_socket)){
                printf("%s\n", line);
                // process
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