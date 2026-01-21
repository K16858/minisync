#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

#define MAX_LINE_LEN 1024

struct file_entry {
    char name[MAX_LINE_LEN+1];
    int  size;
    int  mtime;
};

struct msync_config {
    char id[64];
    char name[64];
    char hostname[64];
    char token[64];
    int port;
};

int subst(char *str, char c1, char c2);
void get_line(char *line, FILE *stream);
int get_file_list(char *base_dir, struct file_entry entries[]);
long long get_file_size(const char *path);
int create_snapshot(const char *path);
int load_config(const char *path, struct msync_config *cfg);
int append_target_json(const char *path, const char *id, const char *name, const char *host, int port, long last_connected_at);

#endif