#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h> 
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include "utils.h"

#define MAX_LINE_LEN 1024
#define MAX_DATA 500

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

int get_file_list(char *base_dir, struct file_entry entries[]) {
    DIR *dir = opendir(base_dir);
    struct dirent *ent;
    int file_count = 0;

    if (dir == NULL) {
        return 0;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", base_dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) < 0) {
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        
        strncpy(entries[file_count].name, ent->d_name, MAX_LINE_LEN);
        entries[file_count].size = st.st_size;
        entries[file_count].mtime = st.st_mtime;
        // printf("file: %s size=%d mtime=%d\n",ent->d_name, st.st_size, st.st_mtime);

        file_count++;

        if (file_count > MAX_DATA) {
            return file_count;
        }
    }

    closedir(dir);

    return file_count;
}

static int extract_json_string(const char *json, const char *key, char *out, size_t out_len) {
    const char *p = strstr(json, key);
    if (p == NULL) {
        return 0;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return 0;
    }
    p = strchr(p, '"');
    if (p == NULL) {
        return 0;
    }
    p++;
    const char *end = strchr(p, '"');
    if (end == NULL) {
        return 0;
    }
    size_t len = (size_t)(end - p);
    if (len + 1 > out_len) {
        return 0;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

static int extract_json_int(const char *json, const char *key, int *out) {
    const char *p = strstr(json, key);
    if (p == NULL) {
        return 0;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    *out = atoi(p);
    return 1;
}

int load_config(const char *path, struct msync_config *cfg) {
    if (cfg == NULL) {
        return -1;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 65536) {
        fclose(fp);
        return -1;
    }

    char *json = malloc((size_t)size + 1);
    if (json == NULL) {
        fclose(fp);
        return -1;
    }

    if (fread(json, 1, (size_t)size, fp) != (size_t)size) {
        free(json);
        fclose(fp);
        return -1;
    }
    json[size] = '\0';
    fclose(fp);

    if (!extract_json_string(json, "\"id\"", cfg->id, sizeof(cfg->id)) ||
        !extract_json_string(json, "\"name\"", cfg->name, sizeof(cfg->name)) ||
        !extract_json_string(json, "\"hostname\"", cfg->hostname, sizeof(cfg->hostname)) ||
        !extract_json_string(json, "\"token\"", cfg->token, sizeof(cfg->token))) {
        free(json);
        return -1;
    }

    if (!extract_json_int(json, "\"port\"", &cfg->port)) {
        cfg->port = 61001;
    }

    free(json);
    return 0;
}

long long get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) {
        return -1;
    }
    return (long long)st.st_size;
}

int create_snapshot(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) {
        return 0;
    }

    char backup_path[PATH_MAX];
    snprintf(backup_path, sizeof(backup_path), "%s.minisync.bak", path);

    FILE *src = fopen(path, "rb");
    if (src == NULL) {
        return -1;
    }
    FILE *dst = fopen(backup_path, "wb");
    if (dst == NULL) {
        fclose(src);
        return -1;
    }

    unsigned char buffer[4096];
    size_t nread;
    while ((nread = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, nread, dst) != nread) {
            fclose(src);
            fclose(dst);
            return -1;
        }
    }

    fclose(src);
    fclose(dst);
    return 0;
}

static void json_escape(const char *in, char *out, size_t out_len) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 2 < out_len; i++) {
        if (in[i] == '"' || in[i] == '\\') {
            out[j++] = '\\';
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
}

int append_target_json(const char *path, const char *id, const char *name, const char *host, int port, long last_connected_at) {
    FILE *fp = fopen(path, "r");
    char *buf = NULL;
    long size = 0;

    if (fp != NULL) {
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (size < 0 || size > 65536) {
            fclose(fp);
            return 1;
        }
        buf = malloc((size_t)size + 1);
        if (buf == NULL) {
            fclose(fp);
            return 1;
        }
        if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
            free(buf);
            fclose(fp);
            return 1;
        }
        buf[size] = '\0';
        fclose(fp);
    }

    char id_esc[128], name_esc[128], host_esc[128];
    json_escape(id, id_esc, sizeof(id_esc));
    json_escape(name, name_esc, sizeof(name_esc));
    json_escape(host, host_esc, sizeof(host_esc));

    char entry[MAX_LINE_LEN + 1];
    snprintf(entry, sizeof(entry),
             "{\"id\":\"%s\",\"name\":\"%s\",\"host\":\"%s\",\"port\":%d,\"last_connected_at\":%ld}",
             id_esc, name_esc, host_esc, port, last_connected_at);

    FILE *out = fopen(path, "w");
    if (out == NULL) {
        free(buf);
        return 1;
    }

    if (buf == NULL || size == 0) {
        fprintf(out, "[%s]\n", entry);
    } else {
        while (size > 0 && (buf[size - 1] == '\n' || buf[size - 1] == '\r' || buf[size - 1] == ' ')) {
            buf[size - 1] = '\0';
            size--;
        }
        if (strcmp(buf, "[]") == 0) {
            fprintf(out, "[%s]\n", entry);
        } else {
            char *end = strrchr(buf, ']');
            if (end != NULL) {
                *end = '\0';
            }
            fprintf(out, "%s,%s]\n", buf, entry);
        }
    }

    free(buf);
    fclose(out);
    return 0;
}

int load_last_target(const char *path, char *host, size_t host_len, int *port) {
    if (host == NULL || port == NULL) {
        return 1;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 65536) {
        fclose(fp);
        return 1;
    }

    char *json = malloc((size_t)size + 1);
    if (json == NULL) {
        fclose(fp);
        return 1;
    }

    if (fread(json, 1, (size_t)size, fp) != (size_t)size) {
        free(json);
        fclose(fp);
        return 1;
    }
    json[size] = '\0';
    fclose(fp);

    char *last_host = NULL;
    int last_port = -1;
    char *p = json;
    while ((p = strstr(p, "\"host\"")) != NULL) {
        char *colon = strchr(p, ':');
        if (colon == NULL) {
            break;
        }
        char *q = strchr(colon, '"');
        if (q == NULL) {
            break;
        }
        q++;
        char *end = strchr(q, '"');
        if (end == NULL) {
            break;
        }
        *end = '\0';
        last_host = q;

        char *port_key = strstr(end + 1, "\"port\"");
        if (port_key != NULL) {
            char *pcolon = strchr(port_key, ':');
            if (pcolon != NULL) {
                last_port = atoi(pcolon + 1);
            }
        }
        p = end + 1;
    }

    if (last_host == NULL || last_port <= 0) {
        free(json);
        return 1;
    }

    snprintf(host, host_len, "%s", last_host);
    *port = last_port;
    free(json);
    return 0;
}
