#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

#define MAX_LINE_LEN 1024

struct file_entry {
    char name[MAX_LINE_LEN+1];
    int  size;
    int  mtime;
};

int subst(char *str, char c1, char c2);
void get_line(char *line, FILE *stream);
int get_file_list(char *base_dir, struct file_entry entries[]);
long long get_file_size(const char *path);
int create_snapshot(const char *path);

#endif