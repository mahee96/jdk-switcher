#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <libgen.h>  // for dirname()
#include <cjson/cJSON.h>

#define JAVA_HOME_ENV_VAR "JAVA_HOME"
#define PATH_ENV_VAR "PATH"
#define CONFIG_JSON "config.json"

char buffer[PATH_MAX];

// Function to expand ~ to the home directory
char* expand_tilde(const char *path) {
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "Cannot find home directory\n");
            exit(EXIT_FAILURE);
        }
        char *full_path = malloc(strlen(home) + strlen(path) + 1);
        if (!full_path) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        strcpy(full_path, home);
        strcat(full_path, path + strlen("~"));
        return full_path;
    }
    return strdup(path);
}

// Function to shrink home directory to ~
char* shrink_tilde(const char *path) {
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "Cannot find home directory\n");
        exit(EXIT_FAILURE);
    }
    int hLen = strlen(home);
    int pLen = strlen(path);
    if (pLen >= hLen && strncmp(path, home, hLen) == 0) {
        char *shrinked_path = malloc(1 + (pLen - hLen) + 1);
        if (!shrinked_path) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        strcpy(shrinked_path, "~");
        strcat(shrinked_path, path + hLen);
        return shrinked_path;
    }
    return strdup(path);
}

void mkdirs(const char *path){
    char *directory = strdup(path);
    if (!directory) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    // go back up one level (dirname())
    char *dir = dirname(directory);
    char cmd[PATH_MAX];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", dir);
    // printf("cmd: %s \n\n", cmd);
    if (system(cmd) != 0) {
        perror("mkdir -p");
        exit(EXIT_FAILURE);
    }

    // // create split iterator
    // char *token = strtok(directory, "/");
    // // iterate over the tokens
    // while (token != NULL) {
    //     // append tokens
    //     snprintf(dir_path, sizeof(dir_path), "%s/%s", dir_path, token);
    //     // create the command
    //     snprintf(cmd, sizeof(cmd), "mkdir %s >/dev/null 2>&1", dir_path);
    //     // except last token, create intermediate parent dirs
    //     token = strtok(NULL, "/");
    //     if (token != NULL){
    //         printf("cmd: %s \n", cmd);
    //         system(cmd);
    //     }
    // }

    free(directory);
}

void create_symlink(const char *target, const char *symlink_path) {
    // create intermediate dirs 
    char *expanded = expand_tilde(symlink_path);
    mkdirs(expanded);
  
    // Remove existing symlink if it exists
    if (unlink(expanded) == -1 && errno != ENOENT) {
        perror("unlink");
        exit(EXIT_FAILURE);
    }

    // Create new symlink
    if (symlink(target, expanded) == -1) {
        perror("symlink");
        exit(EXIT_FAILURE);
    }

    // printf("Created symlink: %s -> %s\n", symlink_path, target);
    free(expanded);
}

char* read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        snprintf(buffer, PATH_MAX, "fopen: %s", filename);
        perror(buffer);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *data = malloc(length + 1);
    if (!data) {
        perror("malloc");
        fclose(file);
        return NULL;
    }

    fread(data, 1, length, file);
    data[length] = '\0';
    fclose(file);

    return data;
}

void write_file(const char *filename, const char* content) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        snprintf(buffer, PATH_MAX, "fopen: %s", filename);
        perror(buffer);
        exit(EXIT_FAILURE);
    }
    fprintf(file, "%s", content);
    fclose(file);
}

int contains(const char* text, const char* key) {
    char *dup = strdup(text);
    if (!dup) return 1;

    int found = strstr(dup, key) ? 0 : 1;
    free(dup);
    return found;
}

char* checkAndAdd(char* content, size_t remaining, const char* contentName, const char* text){
    if (contains(content, text) == 0) {
        printf("\n%s already contains:\n%s\n", contentName, text);
    } else {
        int written = snprintf(content, remaining, "\n%s\n", text);
        if (written > 0 && (size_t)written < remaining) {
            content += written;
            remaining -= written;
        }
    }
    return content;
}

void update_profile(const char *java_home, const char *symlink_path, const char *profile_path) {
    char *profile_content = read_file(profile_path);
    if (!profile_content) {
        fprintf(stderr, "Failed to read profile file: %s\n", profile_path);
        exit(EXIT_FAILURE);
    }

    // Construct export lines
    char java_home_line[PATH_MAX];
    snprintf(java_home_line, sizeof(java_home_line), "export %s='%s'", JAVA_HOME_ENV_VAR, symlink_path);

    const char* symlink_str =
        "# Ensure symlink is in the PATH if not already there\n"
        "if [[ \":$%s:\" != *':%s/bin:'* ]]; then\n"
        "    export PATH='%s/bin':$%s\n"
        "fi";

    char path_line[PATH_MAX];
    snprintf(path_line, sizeof(path_line), symlink_str, PATH_ENV_VAR, symlink_path, symlink_path, PATH_ENV_VAR);

    char *shrunk = shrink_tilde(profile_path);
    char trap_line[PATH_MAX];
    snprintf(trap_line, sizeof(trap_line), "trap 'source %s' SIGUSR1", shrunk);
    free(shrunk);

    // copy old content
    size_t cSize = strlen(profile_content);     // get old content size
    size_t nSize = cSize * 2 + PATH_MAX;        // double the buffer size

    char *new_content = malloc(nSize);          // create new buffer
    if (!new_content) {
        perror("malloc failed to allocate size");
        exit(EXIT_FAILURE);
    }

    snprintf(new_content, nSize, "%s", profile_content);
    char *content = new_content + strlen(new_content);
    size_t remaining = nSize - (content - new_content);

    char *name = shrink_tilde(profile_path);
    content = checkAndAdd(content, remaining, name, java_home_line);
    remaining = nSize - (content - new_content);

    content = checkAndAdd(content, remaining, name, trap_line);
    remaining = nSize - (content - new_content);

    content = checkAndAdd(content, remaining, name, path_line);

    if (strlen(new_content) > cSize) {       
        snprintf(content, nSize, "\n");
        // write to file
        write_file(profile_path, new_content);
        printf("\n\nwritten to profile\n");
    }

    free(name);
    free(profile_content);
    free(new_content);
}

int path_contains_symlink(const char *symlink_path) {
    char *path_env = getenv(PATH_ENV_VAR);
    if (!path_env) {
        perror("getenv");
        exit(EXIT_FAILURE);
    }

    // return contains(path_env, symlink_path, ":");
    return contains(path_env, symlink_path);
}

void read_config(const char *config_path, char **symlink_path, char **profile_file_path, const char *jdk_paths[], int *jdks_count, int *max_jdk_len) {
    FILE *file = fopen(config_path, "r");
    if (!file) {
        snprintf(buffer, PATH_MAX, "fopen: %s", config_path);
        perror(buffer);
        exit(EXIT_FAILURE);
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *data = malloc(length + 1);
    if (!data) {
        perror("malloc");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    fread(data, 1, length, file);
    data[length] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(data);
    if (!json) {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        free(data);
        exit(EXIT_FAILURE);
    }

    cJSON *symlink_json = cJSON_GetObjectItem(json, "symlink_path");
    cJSON *profile_json = cJSON_GetObjectItem(json, "profile_file_path");
    cJSON *jdk_json     = cJSON_GetObjectItem(json, "jdks");

    if (!cJSON_IsString(symlink_json) || !cJSON_IsString(profile_json)) {
        fprintf(stderr, "Invalid JSON format\n");
        cJSON_Delete(json);
        free(data);
        exit(EXIT_FAILURE);
    }

    *symlink_path = strdup(symlink_json->valuestring);
    *profile_file_path = strdup(profile_json->valuestring);

    if (!cJSON_IsArray(jdk_json)) {
        fprintf(stderr, "Expected 'jdks' to be an array\n");
        cJSON_Delete(json);
        free(data);
        exit(EXIT_FAILURE);
    }

    *jdks_count = cJSON_GetArraySize(jdk_json);
    *max_jdk_len = 0;

    for (int i = 0; i < *jdks_count; i++) {
        cJSON *jdk_item = cJSON_GetArrayItem(jdk_json, i);
        if (!cJSON_IsString(jdk_item)) {
            jdk_paths[i] = NULL;
            continue;
        }

        char *path = strdup(jdk_item->valuestring);
        if (!path) {
            perror("strdup");
            exit(EXIT_FAILURE);
        }

        char *expanded = expand_tilde(path);
        if (access(expanded, F_OK) != 0) {
            char *invalid = malloc(strlen(path) + strlen(" (Invalid)") + 1);
            if (!invalid) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            strcpy(invalid, path);
            strcat(invalid, " (Invalid)");
            free(path);
            path = invalid;
        }
        free(expanded);

        jdk_paths[i] = path;

        char *shrunk = shrink_tilde(path);
        char *suffix = strstr(shrunk, " (Invalid)");
        if (suffix) { *suffix = '\0'; }

        int len = strlen(shrunk);
        if (len > *max_jdk_len) {
            *max_jdk_len = len;
        }
        free(shrunk);

    }

    cJSON_Delete(json);
    free(data);
}

typedef enum {
    STR2INT_SUCCESS,
    STR2INT_OVERFLOW,
    STR2INT_UNDERFLOW,
    STR2INT_INCONVERTIBLE
} str2int_errno;

str2int_errno str2int(int *out, char *s, int base) {
    char *end;
    if (s[0] == '\0' || isspace(s[0]))
        return STR2INT_INCONVERTIBLE;
    errno = 0;
    long l = strtol(s, &end, base);
    /* Both checks are needed because INT_MAX == LONG_MAX is possible. */
    if (l > INT_MAX || (errno == ERANGE && l == LONG_MAX))
        return STR2INT_OVERFLOW;
    if (l < INT_MIN || (errno == ERANGE && l == LONG_MIN))
        return STR2INT_UNDERFLOW;
    if (*end != '\0')
        return STR2INT_INCONVERTIBLE;
    *out = (int)l;
    return STR2INT_SUCCESS;
}

char* getAbsoluteExeDir(char* current_exe){
    char candidate[PATH_MAX];

    if (strchr(current_exe, '/')) {
        if (current_exe[0] == '/') {
            strncpy(candidate, current_exe, sizeof(candidate) - 1);
        } else {
            char cwd[PATH_MAX];
            getcwd(cwd, sizeof(cwd));
            snprintf(candidate, sizeof(candidate), "%s/%s", cwd, current_exe);
        }
    } else {
        char cmd[PATH_MAX];
        snprintf(cmd, sizeof(cmd), "/usr/bin/which %s", basename(current_exe));
        FILE *fp = popen(cmd, "r");
        fgets(candidate, sizeof(candidate), fp);
        pclose(fp);
    }

    candidate[strcspn(candidate, "\n")] = '\0';

    char *absolute = realpath(candidate, NULL);
    char *dir = strdup(dirname(absolute));
    free(absolute);
    return dir;
}


int main(int argc, char* argv[]) {
    char *config_path = CONFIG_JSON;
    char *symlink_path = NULL;
    char *profile_file_path = NULL;
    const char *jdk_paths[20]; // Array to store paths
    int jdks_count = 0;

    char exe_path[PATH_MAX];
    snprintf(exe_path, sizeof(exe_path), "%s", getAbsoluteExeDir(argv[0]));

    char config_file[PATH_MAX];
    snprintf(config_file, sizeof(config_file), "%s/%s", exe_path, config_path);

    int max_jdk_len = 0;
    read_config(config_file, &symlink_path, &profile_file_path, jdk_paths, &jdks_count, &max_jdk_len);
    // printf("config.json: \n  - symlink_path = \'%s\'; \n  - profile_file_path = \'%s\';\n\n", symlink_path, profile_file_path);

    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        printf("\n");
        // Print the collected paths (optional)
        for (int i = 0; i < jdks_count; i++) {
            char *s = shrink_tilde(jdk_paths[i]);
            char *suffix = strstr(s, " (Invalid)");

            if (suffix) { *suffix = '\0'; } // temporarily split
            printf("  %d. %-*s", i + 1, max_jdk_len + 4, s);
            if (suffix) { printf(" (Invalid)"); }

            printf("\n");
            free(s);
        }
        printf("\n");
        return 0;
    }


    if (argc > 2 && strcmp(argv[1], "--use") == 0) {
        // use requested jdk
        int selection;
        if((str2int(&selection, argv[2], 10) != STR2INT_SUCCESS) || (selection <= 0 || selection > jdks_count)){
            fprintf(stderr, "  No JDK found for the selection: `%s`\n"
                            "  use '--list' option to list available JDKs\n", argv[2]);
            return 0;
        }
      
        // const char *target_jdk = jdk_paths[0];
        const char *target_jdk = jdk_paths[selection-1];
        target_jdk = expand_tilde(target_jdk);

        printf("\ntarget.jdk: %s \n\n", shrink_tilde(target_jdk));

        // Create symlink to target JDK directory
        create_symlink(target_jdk, symlink_path);

        char *expanded_symlink = expand_tilde(symlink_path);
        char *expanded_profile = expand_tilde(profile_file_path);

        system("pkill -SIGUSR1 -u $USER zsh");
        // Check if the symlink directory is already in PATH
        if (path_contains_symlink(expanded_symlink) != 0) {            
            printf("\n\nAdding to Path...\n");
            // Update the profile file to persist environment variable changes
            update_profile(target_jdk, expanded_symlink, expanded_profile);
            // Send SIGUSR1 signal to all shell instances
            system("pkill -SIGUSR1 -u $USER zsh");
            // printf("\n  ==> shell ENV vars refreshed");
        } else {
            // printf("\n  ==> %s is already in %s\n\n", symlink_path, PATH_ENV_VAR);
        }
    }
    else
    {
        printf("\nInvalid arguments. Supported commands:\n");
        printf("\n  - `./jdk --list` to show jdks listed in config.json");
        printf("\n  - `./jdk --use <index>` to use the jdk dir as JAVA_HOME, defined by its order in the config.json");
        printf("\n\n");
    }

    return 0;
}


/* 
  HOW TO USE this
  1. compile using `gcc -o jdk jdk.c -L$BREW_LIBS  -lcjson`
  2. run using 
       - `./jdk --list` to show jdks listed in config.json
       - `./jdk --use <index>` to use the jdk dir defined by its index ordering in the config.json
 */
