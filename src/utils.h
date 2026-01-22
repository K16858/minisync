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

struct space_entry {
    char id[64];
    char name[64];
    char path[1024];
    int port;
};

struct global_config {
    struct space_entry *spaces;
    int space_count;
    int space_capacity;
};

int subst(char *str, char c1, char c2);
void get_line(char *line, FILE *stream);
int get_file_list(char *base_dir, struct file_entry entries[]);
long long get_file_size(const char *path);
int create_snapshot(const char *path);
int load_config(const char *path, struct msync_config *cfg);
int append_target_json(const char *path, const char *id, const char *name, const char *host, int port, long last_connected_at);
int load_last_target(const char *path, char *host, size_t host_len, int *port);
int load_global_config(struct global_config *gcfg);
int save_global_config(const struct global_config *gcfg);
int add_space_to_global_config(const char *id, const char *name, const char *path, int port);
void free_global_config(struct global_config *gcfg);

#endif