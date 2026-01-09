#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"

#define MAX_LINE_LEN 1024

int subst(char *str, char c1, char c2) {
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

void get_line(char *line, FILE *stream) {
    while(1){
        line[0] = '\0';
        if(fgets(line, MAX_LINE_LEN+1, stream)==NULL){
            fprintf(stderr,"Input ERROR\n");
        }
        subst(line, '\n', '\0');
        if(line[0]=='\0'){
            fprintf(stderr,"Input ERROR\n");           
        }
        else{
            break;
        }
    }
}