#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h> 
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
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

static char* get_global_config_path(char *buf, size_t buf_len) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        return NULL;
    }
    snprintf(buf, buf_len, "%s/.config/minisync/config.json", home);
    return buf;
}

static int ensure_config_dir(void) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        return -1;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.config", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.config/minisync", home);
    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int load_global_config(struct global_config *gcfg) {
    if (gcfg == NULL) {
        return -1;
    }
    
    gcfg->spaces = NULL;
    gcfg->space_count = 0;
    gcfg->space_capacity = 0;

    char path[PATH_MAX];
    if (get_global_config_path(path, sizeof(path)) == NULL) {
        return -1;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 1048576) {
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

    char *spaces_start = strstr(json, "\"spaces\"");
    if (spaces_start == NULL) {
        free(json);
        return 0;
    }
    
    char *array_start = strchr(spaces_start, '[');
    if (array_start == NULL) {
        free(json);
        return 0;
    }

    gcfg->space_capacity = 16;
    gcfg->spaces = malloc(sizeof(struct space_entry) * (size_t)gcfg->space_capacity);
    if (gcfg->spaces == NULL) {
        free(json);
        return -1;
    }

    char *p = array_start + 1;
    while (*p != '\0' && *p != ']') {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',') {
            p++;
        }
        if (*p != '{') {
            break;
        }

        char *obj_start = p;
        int brace_count = 0;
        while (*p != '\0') {
            if (*p == '{') brace_count++;
            if (*p == '}') brace_count--;
            if (brace_count == 0) break;
            p++;
        }
        if (brace_count != 0) {
            break;
        }

        char obj_buf[4096];
        size_t obj_len = (size_t)(p - obj_start + 1);
        if (obj_len >= sizeof(obj_buf)) {
            continue;
        }
        memcpy(obj_buf, obj_start, obj_len);
        obj_buf[obj_len] = '\0';

        if (gcfg->space_count >= gcfg->space_capacity) {
            gcfg->space_capacity *= 2;
            struct space_entry *new_spaces = realloc(gcfg->spaces, 
                sizeof(struct space_entry) * (size_t)gcfg->space_capacity);
            if (new_spaces == NULL) {
                break;
            }
            gcfg->spaces = new_spaces;
        }

        struct space_entry *entry = &gcfg->spaces[gcfg->space_count];
        if (extract_json_string(obj_buf, "\"id\"", entry->id, sizeof(entry->id)) &&
            extract_json_string(obj_buf, "\"name\"", entry->name, sizeof(entry->name)) &&
            extract_json_string(obj_buf, "\"path\"", entry->path, sizeof(entry->path))) {
            gcfg->space_count++;
        }

        p++;
    }

    free(json);
    return 0;
}

int save_global_config(const struct global_config *gcfg) {
    if (gcfg == NULL) {
        return -1;
    }

    if (ensure_config_dir() < 0) {
        return -1;
    }

    char path[PATH_MAX];
    if (get_global_config_path(path, sizeof(path)) == NULL) {
        return -1;
    }

    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return -1;
    }

    fprintf(fp, "{\n  \"spaces\": [\n");
    for (int i = 0; i < gcfg->space_count; i++) {
        const struct space_entry *e = &gcfg->spaces[i];
        char id_esc[128], name_esc[128], path_esc[2048];
        json_escape(e->id, id_esc, sizeof(id_esc));
        json_escape(e->name, name_esc, sizeof(name_esc));
        json_escape(e->path, path_esc, sizeof(path_esc));
        
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"id\": \"%s\",\n", id_esc);
        fprintf(fp, "      \"name\": \"%s\",\n", name_esc);
        fprintf(fp, "      \"path\": \"%s\"\n", path_esc);
        fprintf(fp, "    }%s\n", (i + 1 < gcfg->space_count) ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");

    fclose(fp);
    return 0;
}

int add_space_to_global_config(const char *id, const char *name, const char *path) {
    struct global_config gcfg;
    if (load_global_config(&gcfg) < 0) {
        return -1;
    }

    for (int i = 0; i < gcfg.space_count; i++) {
        if (strcmp(gcfg.spaces[i].id, id) == 0) {
            strncpy(gcfg.spaces[i].name, name, sizeof(gcfg.spaces[i].name) - 1);
            gcfg.spaces[i].name[sizeof(gcfg.spaces[i].name) - 1] = '\0';
            strncpy(gcfg.spaces[i].path, path, sizeof(gcfg.spaces[i].path) - 1);
            gcfg.spaces[i].path[sizeof(gcfg.spaces[i].path) - 1] = '\0';
            int result = save_global_config(&gcfg);
            free_global_config(&gcfg);
            return result;
        }
    }

    if (gcfg.space_count >= gcfg.space_capacity) {
        int new_capacity = (gcfg.space_capacity == 0) ? 16 : gcfg.space_capacity * 2;
        struct space_entry *new_spaces = realloc(gcfg.spaces, 
            sizeof(struct space_entry) * (size_t)new_capacity);
        if (new_spaces == NULL) {
            free_global_config(&gcfg);
            return -1;
        }
        gcfg.spaces = new_spaces;
        gcfg.space_capacity = new_capacity;
    }

    struct space_entry *entry = &gcfg.spaces[gcfg.space_count];
    strncpy(entry->id, id, sizeof(entry->id) - 1);
    entry->id[sizeof(entry->id) - 1] = '\0';
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';
    gcfg.space_count++;

    int result = save_global_config(&gcfg);
    free_global_config(&gcfg);
    return result;
}

void free_global_config(struct global_config *gcfg) {
    if (gcfg != NULL && gcfg->spaces != NULL) {
        free(gcfg->spaces);
        gcfg->spaces = NULL;
        gcfg->space_count = 0;
        gcfg->space_capacity = 0;
    }
}

int validate_file_path(const char *file_path, const char *base_dir, char *resolved_path, size_t resolved_len) {
    if (file_path == NULL || base_dir == NULL || resolved_path == NULL) {
        return -1;
    }

    if (file_path[0] == '/') {
        return -1;
    }

    if (strlen(file_path) != strcspn(file_path, "\0")) {
        return -1;
    }

    if (strstr(file_path, "..") != NULL) {
        return -1;
    }

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, file_path);

    char *real_path = realpath(full_path, NULL);
    if (real_path == NULL) {
        char parent_path[PATH_MAX];
        strncpy(parent_path, full_path, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';
        
        char *last_slash = strrchr(parent_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            char *real_parent = realpath(parent_path, NULL);
            if (real_parent == NULL) {
                return -1;
            }
            char *real_base = realpath(base_dir, NULL);
            if (real_base == NULL) {
                free(real_parent);
                return -1;
            }
            
            int is_safe = (strncmp(real_parent, real_base, strlen(real_base)) == 0);
            if (is_safe) {
                size_t base_len = strlen(real_base);
                if (strlen(real_parent) == base_len || real_parent[base_len] == '/') {
                    snprintf(resolved_path, resolved_len, "%s", full_path);
                    free(real_parent);
                    free(real_base);
                    return 0;
                }
            }
            free(real_parent);
            free(real_base);
            return -1;
        }
        return -1;
    }

    char *real_base = realpath(base_dir, NULL);
    if (real_base == NULL) {
        free(real_path);
        return -1;
    }

    int is_safe = (strncmp(real_path, real_base, strlen(real_base)) == 0);
    if (is_safe) {
        size_t base_len = strlen(real_base);
        if (strlen(real_path) == base_len || real_path[base_len] == '/') {
            snprintf(resolved_path, resolved_len, "%s", real_path);
            free(real_path);
            free(real_base);
            return 0;
        }
    }

    free(real_path);
    free(real_base);
    return -1;
}
