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
        char *full_path = malloc(strlen(home) + strlen(path));
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
        char *shrinked_path = malloc(strlen("~") + strlen(path));
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
    
    char dir_path[PATH_MAX] = "";
    char cmd[PATH_MAX];

    // go back up one level (dirname())
    char *dir = dirname(directory);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", dir);
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
    symlink_path = expand_tilde(symlink_path);
    mkdirs(symlink_path);

    // Remove existing symlink if it exists
    if (unlink(symlink_path) == -1 && errno != ENOENT) {
        perror("unlink");
        exit(EXIT_FAILURE);
    }

    // Create new symlink
    if (symlink(target, symlink_path) == -1) {
        perror("symlink");
        exit(EXIT_FAILURE);
    }

    // printf("Created symlink: %s -> %s\n", symlink_path, target);
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
    text = strdup(text);

    char *result = strstr(text, key);
    if(result != NULL){
        return strncmp(result, key, strlen(key));
    }
    return 1;
}

char* checkAndAdd(char* content, int nSize, const char* contentName, const char* text){
    if(contains(content, text) == 0){
        printf("\n%s already contains: \n%s\n", contentName, text);
    }else{
        snprintf(content, nSize, "\n%s\n", text);
        content += strlen(content);
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

    const char* symlink_str  =  "# Ensure symlink is in the PATH if not already there\n"
                                "if [[ \":$%s:\" != *':%s/bin:'* ]]; then\n"
                                "    export PATH='%s/bin':$%s\n"
                                "fi";

    char path_line[PATH_MAX];
    snprintf(path_line, sizeof(path_line), symlink_str, PATH_ENV_VAR, symlink_path, symlink_path, PATH_ENV_VAR);

    char trap_line[PATH_MAX];
    snprintf(trap_line, sizeof(trap_line), "trap 'source %s' SIGUSR1", shrink_tilde(profile_path));

    // copy old content
    int cSize = strlen(profile_content);    // get old content size
    int nSize = cSize*2;                    // double the buffer size

    char new_content[nSize];                // create new buffer
    char *content = new_content;
    snprintf(content, nSize, "%s", profile_content);
    char* pContent = content;

    content += strlen(content);

    const char* contentName = shrink_tilde(profile_path);

    content = checkAndAdd(content, nSize, contentName, java_home_line);
    content = checkAndAdd(content, nSize, contentName, trap_line);
    content = checkAndAdd(content, nSize, contentName, path_line);

    if((content-pContent) > strlen(profile_content)){
        snprintf(content, nSize, "\n");
        // write to file
        write_file(profile_path, new_content);
        printf("\n\nwritten to profile\n");
    }
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

void read_config(const char *config_path, char **symlink_path, char **profile_file_path, const char *jdk_paths[],  int *jdks_count) {
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

    for (int i = 0; i < *jdks_count; i++) {
        cJSON *jdk_item = cJSON_GetArrayItem(jdk_json, i);
        if (cJSON_IsString(jdk_item)) {
            jdk_paths[i] = strdup(jdk_item->valuestring);  // Allocate memory for the string
        } else {
            jdk_paths[i] = NULL;  // Handle non-string items
        }
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
    *out = l;
    return STR2INT_SUCCESS;
}


char* getExePath(char* current_exe){
    char which_cmd[PATH_MAX];
    snprintf(which_cmd, sizeof(which_cmd), "which %s", basename(current_exe));

    // Open a pipe to execute the "which" command in read mode
    FILE *which_fp = popen(which_cmd, "r");
    if (which_fp == NULL) {
        perror("popen");
        return NULL;
    }

    char which_path[PATH_MAX];
    if (fgets(which_path, sizeof(which_path), which_fp) == NULL) {
        fprintf(stderr, "Error: Executable '%s' not found.\n", current_exe);
        pclose(which_fp);
        return NULL;
    }

    // Remove the newline character at the end
    which_path[strcspn(which_path, "\n")] = '\0';

    pclose(which_fp);

    char resolved_path[PATH_MAX];
    ssize_t len = readlink(which_path, resolved_path, sizeof(resolved_path) - 1);

    if (len != -1) {
        resolved_path[len] = '\0';
        char *absolute_path = realpath(resolved_path, NULL);

        if (absolute_path) {
            // printf("Absolute path: %s\n", absolute_path);
            return dirname(absolute_path);
        } else {
            perror("realpath");
            return NULL;
        }
    } else {
        perror("readlink");
        return NULL;
    }
}


int main(int argc, char* argv[]) {
    char *config_path = CONFIG_JSON;
    char *symlink_path = NULL;
    char *profile_file_path = NULL;
    const char *jdk_paths[20]; // Array to store paths
    int jdks_count = 0;

    char exe_path[PATH_MAX];
    snprintf(exe_path, PATH_MAX, "%s", getExePath(argv[0]));
    
    char config_file[PATH_MAX];
    snprintf(config_file, PATH_MAX, "%s/%s", exe_path, config_path);

    read_config(config_file, &symlink_path, &profile_file_path, jdk_paths, &jdks_count);
    // printf("config.json: \n  - symlink_path = \'%s\'; \n  - profile_file_path = \'%s\';\n\n", symlink_path, profile_file_path);

    if(argc > 1 && strcmp(argv[1],"--list") == 0){
        printf("\n");
        // Print the collected paths (optional)
        for (int i = 0; i < jdks_count; i++) {
            printf("  %d. %s\n", i+1, shrink_tilde(jdk_paths[i]));
        }
        printf("\n");
    }


    if(argc > 2 && strcmp(argv[1],"--use") == 0){
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

        symlink_path        = expand_tilde(symlink_path);
        profile_file_path   = expand_tilde(profile_file_path);

        system("pkill -SIGUSR1 -u $USER zsh");

        // Check if the symlink directory is already in PATH
        if (path_contains_symlink(symlink_path) != 0) {
            printf("\n\nAdding to Path...\n");
            // Update the profile file to persist environment variable changes
            update_profile(target_jdk, symlink_path, profile_file_path);
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
