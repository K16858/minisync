#ifndef UTILS_H
#define UTILS_H

struct file_entry {
    char name[256];
    int  size;
    int  mtime;
};

int subst(char *str, char c1, char c2);
void get_line(char *line, FILE *stream);

#endif