#define _DEFAULT_SOURCE // For DT_DIR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <termios.h>
#include <ctype.h>
#include <fcntl.h>
#include <dirent.h> // Add for directory operations
#include <glob.h>   // For wildcard expansion (globbing)

#define MAX_LINE 80 
#define MAX_ARGS 20 
#define HISTORY_FILE_NAME ".sdn_history"
#define MAX_HISTORY_ENTRIES 1000
#define MAX_COMMAND_SEGMENTS 10 
#define MAX_ALIASES 50
#define MAX_ALIAS_NAME_LEN 50
#define MAX_ALIAS_COMMAND_LEN MAX_LINE

#define ANSI_COLOR_GRAY "\033[90m"
#define ANSI_COLOR_RESET "\033[0m"

struct termios orig_termios;

typedef struct {
    char *commands[MAX_HISTORY_ENTRIES];
    int count;
} HistoryCache;

typedef struct {
    char *args[MAX_ARGS];
    char *inputFile;
    char *outputFile;
    int appendMode;
} CommandSegment;

typedef struct {
    char name[MAX_ALIAS_NAME_LEN];
    char command[MAX_ALIAS_COMMAND_LEN];
} AliasEntry;

AliasEntry alias_table[MAX_ALIASES];
int alias_count = 0;

// Helper structure to store matching files
typedef struct {
    char **files;
    int count;
    int capacity;
} FileMatches;

void get_history_file_path(char *path_buffer, size_t buffer_size);

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Function to find a matching command in history
char *find_matching_command(const char *partial, HistoryCache *cache) {
    if (strlen(partial) == 0) return NULL;
    
    for (int i = cache->count - 1; i >= 0; i--) {
        if (strncmp(cache->commands[i], partial, strlen(partial)) == 0) {
            return cache->commands[i];
        }
    }
    return NULL;
}

void load_history_cache(HistoryCache *cache) {
    char history_file_path[FILENAME_MAX];
    get_history_file_path(history_file_path, sizeof(history_file_path));
    
    FILE *fp = fopen(history_file_path, "r");
    if (!fp) return;
    
    char line[MAX_LINE + 30]; 
    cache->count = 0;
    
    while (fgets(line, sizeof(line), fp) && cache->count < MAX_HISTORY_ENTRIES) {
        char *cmd_start = strchr(line, ']');
        if (!cmd_start) continue;
        
        cmd_start += 2; // Skip "] "
        
        // Remove trailing newline
        line[strcspn(line, "\n")] = 0;
        
        // Store unique commands only
        int is_duplicate = 0;
        for (int i = 0; i < cache->count; i++) {
            if (strcmp(cache->commands[i], cmd_start) == 0) {
                is_duplicate = 1;
                break;
            }
        }
        
        if (!is_duplicate) {
            cache->commands[cache->count] = strdup(cmd_start);
            cache->count++;
        }
    }
    
    fclose(fp);
}

void free_history_cache(HistoryCache *cache) {
    for (int i = 0; i < cache->count; i++) {
        free(cache->commands[i]);
    }
    cache->count = 0;
}

const char *find_alias_command(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(alias_table[i].name, name) == 0) {
            return alias_table[i].command;
        }
    }
    return NULL;
}

void add_or_update_alias(const char *name, const char *command) {
    if (strlen(name) >= MAX_ALIAS_NAME_LEN || strlen(command) >= MAX_ALIAS_COMMAND_LEN) {
        fprintf(stderr, "sdn: alias name or command too long\n");
        return;
    }
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(alias_table[i].name, name) == 0) {
            strncpy(alias_table[i].command, command, MAX_ALIAS_COMMAND_LEN -1);
            alias_table[i].command[MAX_ALIAS_COMMAND_LEN -1] = '\0';
            return;
        }
    }
    if (alias_count < MAX_ALIASES) {
        strncpy(alias_table[alias_count].name, name, MAX_ALIAS_NAME_LEN - 1);
        alias_table[alias_count].name[MAX_ALIAS_NAME_LEN - 1] = '\0';
        strncpy(alias_table[alias_count].command, command, MAX_ALIAS_COMMAND_LEN - 1);
        alias_table[alias_count].command[MAX_ALIAS_COMMAND_LEN - 1] = '\0';
        alias_count++;
    } else {
        fprintf(stderr, "sdn: alias table full\n");
    }
}

void remove_alias(const char *name) {
    int found_idx = -1;
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(alias_table[i].name, name) == 0) {
            found_idx = i;
            break;
        }
    }
    if (found_idx != -1) {
        for (int i = found_idx; i < alias_count - 1; i++) {
            alias_table[i] = alias_table[i+1];
        }
        alias_count--;
    } else {
        fprintf(stderr, "sdn: unalias: %s: not found\n", name);
    }
}

void print_all_aliases() {
    for (int i = 0; i < alias_count; i++) {
        printf("%s='%s'\n", alias_table[i].name, alias_table[i].command);
    }
}

void handle_alias_builtin(char **args) {
    if (args[1] == NULL) {
        print_all_aliases();
        return;
    }

    char *equals_ptr = strchr(args[1], '=');
    if (equals_ptr != NULL) {
        char alias_name[MAX_ALIAS_NAME_LEN];
        char alias_value[MAX_ALIAS_COMMAND_LEN];

        size_t name_len = equals_ptr - args[1];
        if (name_len == 0 || name_len >= MAX_ALIAS_NAME_LEN) {
            fprintf(stderr, "sdn: alias: invalid alias name\n");
            return;
        }
        strncpy(alias_name, args[1], name_len);
        alias_name[name_len] = '\0';

        char *value_start = equals_ptr + 1;
        strncpy(alias_value, value_start, MAX_ALIAS_COMMAND_LEN - 1);
        alias_value[MAX_ALIAS_COMMAND_LEN - 1] = '\0';

        size_t val_len = strlen(alias_value);
        if ((alias_value[0] == '"' && alias_value[val_len-1] == '"') ||
            (alias_value[0] == '\'' && alias_value[val_len-1] == '\'')) {
            memmove(alias_value, alias_value + 1, val_len - 2);
            alias_value[val_len - 2] = '\0';
        }
        
        add_or_update_alias(alias_name, alias_value);
    } else {
        if (args[2] != NULL) {
            fprintf(stderr, "sdn: alias: usage: alias [name[=value] ...]\n");
            return;
        }
        const char *cmd = find_alias_command(args[1]);
        if (cmd) {
            printf("%s='%s'\n", args[1], cmd);
        } else {
            fprintf(stderr, "sdn: alias: %s: not found\n", args[1]);
        }
    }
}

void handle_unalias_builtin(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "sdn: unalias: usage: unalias name [name ...]\n");
        return;
    }
    for (int i = 1; args[i] != NULL; i++) {
        remove_alias(args[i]);
    }
}

// Initialize FileMatches structure
void init_file_matches(FileMatches *matches) {
    matches->capacity = 10;
    matches->files = malloc(matches->capacity * sizeof(char*));
    matches->count = 0;
}

// Free memory used by FileMatches
void free_file_matches(FileMatches *matches) {
    for (int i = 0; i < matches->count; i++) {
        free(matches->files[i]);
    }
    free(matches->files);
    matches->count = 0;
    matches->capacity = 0;
}

// Add a file to FileMatches
void add_file_match(FileMatches *matches, const char *filename) {
    if (matches->count >= matches->capacity) {
        matches->capacity *= 2;
        matches->files = realloc(matches->files, matches->capacity * sizeof(char*));
    }
    matches->files[matches->count++] = strdup(filename);
}

// Extract the word being completed
char *get_current_word(const char *buffer, int position) {
    if (position == 0) return strdup("");
    
    int word_start = position;
    while (word_start > 0 && !isspace(buffer[word_start - 1])) {
        word_start--;
    }
    
    int word_len = position - word_start;
    char *word = malloc(word_len + 1);
    strncpy(word, buffer + word_start, word_len);
    word[word_len] = '\0';
    
    return word;
}

// Find files that match the prefix
void find_matching_files(const char *prefix, FileMatches *matches) {
    DIR *dir;
    struct dirent *entry;
    
    char *dir_path = ".";
    char *name_prefix = strdup(prefix);
    
    // Check if prefix contains a directory path
    char *last_slash = strrchr(prefix, '/');
    if (last_slash) {
        // Extract directory path and filename prefix
        int dir_len = last_slash - prefix + 1;
        dir_path = malloc(dir_len + 1);
        strncpy(dir_path, prefix, dir_len);
        dir_path[dir_len] = '\0';
        
        free(name_prefix);
        name_prefix = strdup(last_slash + 1);
    }
    
    dir = opendir(dir_path);
    if (!dir) {
        if (strcmp(dir_path, ".") != 0) free(dir_path);
        free(name_prefix);
        return;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files unless the prefix starts with a dot
        if (entry->d_name[0] == '.' && name_prefix[0] != '.') {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                continue;
            }
        }
        
        // Add file if it matches the prefix
        if (strncmp(entry->d_name, name_prefix, strlen(name_prefix)) == 0) {
            char *full_path;
            if (strcmp(dir_path, ".") == 0) {
                full_path = strdup(entry->d_name);
            } else {
                full_path = malloc(strlen(dir_path) + strlen(entry->d_name) + 1);
                sprintf(full_path, "%s%s", dir_path, entry->d_name);
            }
            
            // Add a slash to directories
            if (entry->d_type == DT_DIR) {
                char *with_slash = malloc(strlen(full_path) + 2);
                sprintf(with_slash, "%s/", full_path);
                free(full_path);
                full_path = with_slash;
            }
            
            add_file_match(matches, full_path);
            free(full_path);
        }
    }
    
    closedir(dir);
    if (strcmp(dir_path, ".") != 0) free(dir_path);
    free(name_prefix);
}

// Find the longest common prefix among matching files
char *find_common_prefix(FileMatches *matches) {
    if (matches->count == 0) return strdup("");
    
    char *first = matches->files[0];
    int prefix_len = strlen(first);
    
    for (int i = 1; i < matches->count; i++) {
        int j = 0;
        while (j < prefix_len && first[j] == matches->files[i][j]) {
            j++;
        }
        prefix_len = j;
    }
    
    char *common = malloc(prefix_len + 1);
    strncpy(common, first, prefix_len);
    common[prefix_len] = '\0';
    
    return common;
}

int read_line_with_completion(char *buffer, int max_size, HistoryCache *cache) {
    int c;
    int position = 0;
    char suggestion[MAX_LINE] = {0}; // Initialize to empty
    int history_nav_idx = cache->count; // Current position in history navigation
    FileMatches file_matches;
    init_file_matches(&file_matches);

    memset(buffer, 0, max_size);
    enable_raw_mode();
    
    while (1) {
        c = getchar();
        
        if (c == '\033') { // Escape sequence
            getchar(); // Skip '['
            switch(getchar()) {
                case 'A': // Up arrow
                    if (cache->count > 0 && history_nav_idx > 0) {
                        history_nav_idx--;
                        strncpy(buffer, cache->commands[history_nav_idx], max_size -1);
                        buffer[max_size-1] = '\0';
                        position = strlen(buffer);
                        printf("\033[2K\r"); 
                        printf("sdn> %s", buffer);
                        suggestion[0] = '\0'; 
                    }
                    break;
                case 'B': // Down arrow
                    if (cache->count > 0 && history_nav_idx < cache->count) {
                        history_nav_idx++;
                        if (history_nav_idx < cache->count) {
                            strncpy(buffer, cache->commands[history_nav_idx], max_size -1);
                            buffer[max_size-1] = '\0';
                        } else { // Moved past the last history item to a new line
                            buffer[0] = '\0';
                        }
                        position = strlen(buffer);
                        printf("\033[2K\r"); 
                        printf("sdn> %s", buffer);
                        suggestion[0] = '\0'; 
                    }
                    break;
            }
            fflush(stdout); // Ensure prompt and buffer are displayed
        } else if (c == '\n' || c == '\r') {
            printf("\n");
            break;
        } else if (c == 127 || c == '\b') { // Backspace
            if (position > 0) {
                position--;
                buffer[position] = '\0';
                history_nav_idx = cache->count; // Editing, so reset history navigation

                printf("\033[2K\r"); 
                printf("sdn> %s", buffer);
                
                suggestion[0] = '\0'; // Clear previous suggestion first
                char *match = find_matching_command(buffer, cache);
                if (match && strlen(buffer) > 0) { 
                    strncpy(suggestion, match + position, MAX_LINE -1); // position is new strlen(buffer)
                    suggestion[MAX_LINE-1] = '\0';
                    if (strlen(suggestion) > 0) {
                        printf("%s%s%s", ANSI_COLOR_GRAY, suggestion, ANSI_COLOR_RESET);
                        printf("\033[%dD", (int)strlen(suggestion)); 
                    }
                }
            }
            fflush(stdout); // Ensure changes are displayed
        } else if (c == '\t') {
            if (suggestion[0] != '\0') {
                // Handle command history completion as before
                if (position + strlen(suggestion) < max_size -1) {
                    strcat(buffer, suggestion);
                    position += strlen(suggestion);
                }
                
                printf("\033[2K\r"); 
                printf("sdn> %s", buffer);
                suggestion[0] = '\0';
                history_nav_idx = cache->count;
            } else {
                // Handle file completion
                char *word = get_current_word(buffer, position);
                
                // Only attempt file completion if we have a word
                if (strlen(word) > 0) {
                    free_file_matches(&file_matches);
                    init_file_matches(&file_matches);
                    find_matching_files(word, &file_matches);
                    
                    if (file_matches.count == 1) {
                        // Single match - complete the word
                        int word_start = position - strlen(word);
                        int completion_len = strlen(file_matches.files[0]);
                        
                        if (word_start + completion_len < max_size - 1) {
                            // Remove the partial word
                            position = word_start;
                            buffer[position] = '\0';
                            
                            // Add the complete filename
                            strcat(buffer, file_matches.files[0]);
                            position += completion_len;
                            
                            printf("\033[2K\r");
                            printf("sdn> %s", buffer);
                        }
                    } else if (file_matches.count > 1) {
                        // Multiple matches - find common prefix and show options
                        char *common = find_common_prefix(&file_matches);
                        
                        // Complete to the common prefix if it's longer than current word
                        if (strlen(common) > strlen(word)) {
                            int word_start = position - strlen(word);
                            int completion_len = strlen(common);
                            
                            if (word_start + completion_len < max_size - 1) {
                                // Remove the partial word
                                position = word_start;
                                buffer[position] = '\0';
                                
                                // Add the common prefix
                                strcat(buffer, common);
                                position += completion_len;
                            }
                        }
                        
                        // Display all matches below
                        printf("\n");
                        for (int i = 0; i < file_matches.count; i++) {
                            printf("%s  ", file_matches.files[i]);
                            if ((i + 1) % 4 == 0) printf("\n");
                        }
                        if (file_matches.count % 4 != 0) printf("\n");
                        
                        // Redraw the prompt and buffer
                        printf("sdn> %s", buffer);
                        
                        free(common);
                    }
                }
                free(word);
            }
            fflush(stdout);
        } else if (c == 4) { // CTRL+D
            disable_raw_mode();
            free_file_matches(&file_matches);
            return -1;
        } else if (isprint(c)) {
            if (position < max_size - 1) {
                buffer[position++] = c;
                buffer[position] = '\0';
                history_nav_idx = cache->count; // Editing, so reset history navigation
                
                printf("\033[2K\r"); // Clear entire line, cursor to beginning
                printf("sdn> %s", buffer); // Print prompt and updated buffer
                
                suggestion[0] = '\0'; // Clear previous suggestion
                char *match = find_matching_command(buffer, cache);
                // `position` is current strlen(buffer)
                if (match) { // find_matching_command returns NULL if strlen(buffer) is 0
                    strncpy(suggestion, match + position, MAX_LINE - 1);
                    suggestion[MAX_LINE-1] = '\0';
                    if (strlen(suggestion) > 0) {
                        printf("%s%s%s", ANSI_COLOR_GRAY, suggestion, ANSI_COLOR_RESET);
                        printf("\033[%dD", (int)strlen(suggestion)); // Move cursor back
                    }
                }
            }
            fflush(stdout); // Ensure character and suggestion are displayed
        }
    }
    
    disable_raw_mode();
    free_file_matches(&file_matches);
    return position;
}

void get_history_file_path(char *path_buffer, size_t buffer_size) {
    char *home_dir = getenv("HOME");
    if (home_dir) {
        snprintf(path_buffer, buffer_size, "%s/%s", home_dir, HISTORY_FILE_NAME);
    } else {
        strncpy(path_buffer, HISTORY_FILE_NAME, buffer_size -1);
        path_buffer[buffer_size - 1] = '\0';
    }
}

void save_to_history(const char *command) {
    char history_file_path[FILENAME_MAX];
    get_history_file_path(history_file_path, sizeof(history_file_path));

    FILE *fp = fopen(history_file_path, "a"); 
    if (fp) {
        time_t now = time(NULL);
        struct tm *timeinfo = localtime(&now);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        fprintf(fp, "[%s] %s\n", timestamp, command);
        fclose(fp);
    } else {
        perror("sdn: error writing to history file");
    }
}

void display_history() {
    char history_file_path[FILENAME_MAX];
    get_history_file_path(history_file_path, sizeof(history_file_path));
    FILE *fp = fopen(history_file_path, "r");
    if (fp) {
        char line[MAX_LINE + 30]; // Extra space for timestamp
        int count = 1;
        
        printf("\nCommand History:\n");
        printf("----------------\n");
        
        while (fgets(line, sizeof(line), fp)) {
            // Remove trailing newline
            line[strcspn(line, "\n")] = 0;
            printf("%3d  %s\n", count++, line);
        }
        printf("----------------\n");
        fclose(fp);
    } else {
        if (access(history_file_path, F_OK) == -1) {
            printf("No command history found.\n");
        } else {
            perror("sdn: error reading history file");
        }
    }
}

void free_command_segment_internals(CommandSegment *segment) {
    for (int i = 0; segment->args[i] != NULL; i++) {
        free(segment->args[i]);
        segment->args[i] = NULL;
    }
    if (segment->inputFile) {
        free(segment->inputFile);
        segment->inputFile = NULL;
    }
    if (segment->outputFile) {
        free(segment->outputFile);
        segment->outputFile = NULL;
    }
}

int parse_single_command_segment(char *segment_str, CommandSegment *cmd_segment) {
    // Initialize segment
    cmd_segment->inputFile = NULL;
    cmd_segment->outputFile = NULL;
    cmd_segment->appendMode = 0;
    for(int i=0; i<MAX_ARGS; ++i) cmd_segment->args[i] = NULL;

    char segment_str_copy[MAX_LINE];
    strncpy(segment_str_copy, segment_str, MAX_LINE - 1);
    segment_str_copy[MAX_LINE - 1] = '\0';

    char *raw_tokens[MAX_ARGS];
    int raw_token_count = 0;
    char *token = strtok(segment_str_copy, " \n\t\r");
    while (token != NULL && raw_token_count < MAX_ARGS) {
        raw_tokens[raw_token_count++] = token;
        token = strtok(NULL, " \n\t\r");
    }

    if (raw_token_count == 0) {
        return 0; 
    }

    int current_arg_idx = 0;
    for (int i = 0; i < raw_token_count; ) {
        char *current_raw_token = raw_tokens[i];

        if (strcmp(current_raw_token, "<") == 0) {
            if (i + 1 < raw_token_count) {
                if (cmd_segment->inputFile) free(cmd_segment->inputFile);
                cmd_segment->inputFile = strdup(raw_tokens[i + 1]);
                if (!cmd_segment->inputFile) { perror("sdn: strdup error"); free_command_segment_internals(cmd_segment); return -1; }
                i += 2;
            } else {
                fprintf(stderr, "sdn: syntax error near `<'\n");
                free_command_segment_internals(cmd_segment); return -1;
            }
        } else if (strcmp(current_raw_token, ">") == 0) {
            if (i + 1 < raw_token_count) {
                if (cmd_segment->outputFile) free(cmd_segment->outputFile);
                cmd_segment->outputFile = strdup(raw_tokens[i + 1]);
                if (!cmd_segment->outputFile) { perror("sdn: strdup error"); free_command_segment_internals(cmd_segment); return -1; }
                cmd_segment->appendMode = 0;
                i += 2;
            } else {
                fprintf(stderr, "sdn: syntax error near `>'\n");
                free_command_segment_internals(cmd_segment); return -1;
            }
        } else if (strcmp(current_raw_token, ">>") == 0) {
            if (i + 1 < raw_token_count) {
                if (cmd_segment->outputFile) free(cmd_segment->outputFile);
                cmd_segment->outputFile = strdup(raw_tokens[i + 1]);
                if (!cmd_segment->outputFile) { perror("sdn: strdup error"); free_command_segment_internals(cmd_segment); return -1; }
                cmd_segment->appendMode = 1;
                i += 2;
            } else {
                fprintf(stderr, "sdn: syntax error near `>>'\n");
                free_command_segment_internals(cmd_segment); return -1;
            }
        } else { // Command or argument
            if (strpbrk(current_raw_token, "*?[]") != NULL) { // Has wildcards
                glob_t glob_result;
                memset(&glob_result, 0, sizeof(glob_result));
                // GLOB_TILDE: Expands tilde.
                // GLOB_NOCHECK: If pattern doesn't match, return pattern itself.
                // GLOB_BRACE: Expands {a,b}
                int glob_flags = GLOB_TILDE | GLOB_NOCHECK | GLOB_BRACE;
                int ret = glob(current_raw_token, glob_flags, NULL, &glob_result);

                if (ret == 0) { // Success
                    for (size_t k = 0; k < glob_result.gl_pathc; k++) {
                        if (current_arg_idx < MAX_ARGS - 1) {
                            cmd_segment->args[current_arg_idx] = strdup(glob_result.gl_pathv[k]);
                            if (!cmd_segment->args[current_arg_idx]) { perror("sdn: strdup error"); globfree(&glob_result); free_command_segment_internals(cmd_segment); return -1; }
                            current_arg_idx++;
                        } else {
                            fprintf(stderr, "sdn: too many arguments after wildcard expansion for '%s'\n", current_raw_token);
                            globfree(&glob_result);
                            free_command_segment_internals(cmd_segment); return -1;
                        }
                    }
                } else if (ret == GLOB_NOMATCH) { 
                    // if (current_arg_idx < MAX_ARGS - 1) {
                    //    cmd_segment->args[current_arg_idx++] = strdup(current_raw_token);
                    // } else { /* error */ }
                } else { // Other glob errors (GLOB_NOSPACE, GLOB_ABORTED)
                    fprintf(stderr, "sdn: glob error for pattern '%s'\n", current_raw_token);
                    globfree(&glob_result);
                    free_command_segment_internals(cmd_segment); return -1;
                }
                globfree(&glob_result);
                i++;
            } else { // No wildcards
                if (current_arg_idx < MAX_ARGS - 1) {
                    cmd_segment->args[current_arg_idx] = strdup(current_raw_token);
                    if (!cmd_segment->args[current_arg_idx]) { perror("sdn: strdup error"); free_command_segment_internals(cmd_segment); return -1; }
                    current_arg_idx++;
                } else {
                    fprintf(stderr, "sdn: too many arguments for command\n");
                    free_command_segment_internals(cmd_segment); return -1;
                }
                i++;
            }
        }
    }
    cmd_segment->args[current_arg_idx] = NULL; 
    return 0;
}


void execute_pipeline(CommandSegment segments[], int num_segments, int background) {
    int pipe_fds[2];
    int prev_pipe_read_end = STDIN_FILENO;
    pid_t pids[MAX_COMMAND_SEGMENTS];
    int status;

    for (int i = 0; i < num_segments; i++) {
        if (i < num_segments - 1) {
            if (pipe(pipe_fds) == -1) {
                perror("sdn: pipe");
                exit(EXIT_FAILURE);
            }
        }

        pids[i] = fork();
        if (pids[i] < 0) {
            perror("sdn: fork");
            exit(EXIT_FAILURE);
        }

        if (pids[i] == 0) { // Child process
            if (prev_pipe_read_end != STDIN_FILENO) {
                if (dup2(prev_pipe_read_end, STDIN_FILENO) == -1) {
                    perror("sdn: dup2 stdin");
                    exit(EXIT_FAILURE);
                }
                close(prev_pipe_read_end);
            }

            if (i < num_segments - 1) {
                close(pipe_fds[0]); // Close read end in child
                if (dup2(pipe_fds[1], STDOUT_FILENO) == -1) {
                    perror("sdn: dup2 stdout");
                    exit(EXIT_FAILURE);
                }
                close(pipe_fds[1]);
            }

            if (segments[i].inputFile) {
                int fd_in = open(segments[i].inputFile, O_RDONLY);
                if (fd_in == -1) {
                    perror("sdn: open input file");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd_in, STDIN_FILENO) == -1) {
                    perror("sdn: dup2 input file");
                    exit(EXIT_FAILURE);
                }
                close(fd_in);
            }

            if (segments[i].outputFile) {
                int flags = O_WRONLY | O_CREAT;
                if (segments[i].appendMode) {
                    flags |= O_APPEND;
                } else {
                    flags |= O_TRUNC;
                }
                int fd_out = open(segments[i].outputFile, flags, 0644);
                if (fd_out == -1) {
                    perror("sdn: open output file");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd_out, STDOUT_FILENO) == -1) {
                    perror("sdn: dup2 output file");
                    exit(EXIT_FAILURE);
                }
                close(fd_out);
            }
            
            if (segments[i].args[0] == NULL) {
                fprintf(stderr, "sdn: attempt to execute empty command\n");
                exit(EXIT_FAILURE);
            }
            if (execvp(segments[i].args[0], segments[i].args) == -1) {
                perror("sdn: execvp failed");
                exit(EXIT_FAILURE);
            }
        } else { // Parent process
            if (prev_pipe_read_end != STDIN_FILENO) {
                close(prev_pipe_read_end);
            }
            if (i < num_segments - 1) {
                close(pipe_fds[1]); // Close write end in parent
                prev_pipe_read_end = pipe_fds[0];
            }
        }
    }

    if (!background) {
        for (int i = 0; i < num_segments; i++) {
            waitpid(pids[i], &status, 0);
        }
    } else {
        for (int i = 0; i < num_segments; i++) {
             printf("[%d] ", pids[i]);
        }
        printf("\n");
    }
}


int main(void) {
    char input_line_raw[MAX_LINE];
    char input_line_for_parsing[MAX_LINE];
    char history_entry_buffer[MAX_LINE];
    char expanded_line[MAX_LINE];
    
    pid_t wpid; 
    int status;
    int overall_background; 
    
    CommandSegment command_segments[MAX_COMMAND_SEGMENTS];
    int num_segments;
    
    HistoryCache history_cache = {0};
    load_history_cache(&history_cache);

    while (1) {
        
        while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
            printf("Shell: Background process with PID %d terminated.\n", wpid);
        }

        printf("sdn> ");
        fflush(stdout);

        int result = read_line_with_completion(input_line_raw, sizeof(input_line_raw), &history_cache);
        
        if (result == -1) {
            printf("\nExiting sdn.\n");
            break;
        }

        if (strlen(input_line_raw) == 0) {
            continue;
        }
        
        strncpy(expanded_line, input_line_raw, sizeof(expanded_line) - 1);
        expanded_line[sizeof(expanded_line) - 1] = '\0';

        char temp_line_for_first_word[MAX_LINE];
        strcpy(temp_line_for_first_word, input_line_raw);
        char *first_word = strtok(temp_line_for_first_word, " \t\n");

        if (first_word) {
            const char *alias_cmd_str = find_alias_command(first_word);
            if (alias_cmd_str) {
                char *rest_of_command = strchr(input_line_raw, ' '); // Find first space
                if (rest_of_command) { // If there are arguments after the alias
                    // Skip the space itself for appending
                    snprintf(expanded_line, sizeof(expanded_line), "%s%s", alias_cmd_str, rest_of_command);
                } else { // Alias used without arguments
                    strncpy(expanded_line, alias_cmd_str, sizeof(expanded_line) - 1);
                    expanded_line[sizeof(expanded_line) - 1] = '\0';
                }
            }
        }
        
        strncpy(history_entry_buffer, expanded_line, MAX_LINE - 1);
        history_entry_buffer[MAX_LINE - 1] = '\0';

        if (strlen(history_entry_buffer) > 0) {
            save_to_history(history_entry_buffer);
            
            int is_duplicate = 0;
            for (int i = 0; i < history_cache.count; i++) {
                if (strcmp(history_cache.commands[i], history_entry_buffer) == 0) {
                    is_duplicate = 1;
                    break;
                }
            }
            
            if (!is_duplicate && history_cache.count < MAX_HISTORY_ENTRIES) {
                history_cache.commands[history_cache.count] = strdup(history_entry_buffer);
                history_cache.count++;
            }
        }
        
        strcpy(input_line_for_parsing, expanded_line);

        overall_background = 0;
        int len = strlen(input_line_for_parsing);
        if (len > 0 && input_line_for_parsing[len-1] == '&') {
            overall_background = 1;
            input_line_for_parsing[len-1] = '\0'; 
            if (len > 1 && input_line_for_parsing[len-2] == ' ') {
                 input_line_for_parsing[len-2] = '\0';
            }
        }
        
        if (strcmp(input_line_for_parsing, "exit") == 0) {
            while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
                printf("Shell: Background process with PID %d terminated before exit.\n", wpid);
            }
            printf("Exiting sdn.\n");
            break;
        }
        
        num_segments = 0;
        char *saveptr_pipe;
        char *command_str = strtok_r(input_line_for_parsing, "|", &saveptr_pipe);
        while(command_str != NULL && num_segments < MAX_COMMAND_SEGMENTS) {
            char temp_cmd_str[MAX_LINE];
            strncpy(temp_cmd_str, command_str, MAX_LINE-1);
            temp_cmd_str[MAX_LINE-1] = '\0';

            if (parse_single_command_segment(temp_cmd_str, &command_segments[num_segments]) == -1) {
                num_segments = -1; // Indicate parsing error
                break;
            }
            if (command_segments[num_segments].args[0] == NULL && command_segments[num_segments].inputFile == NULL && command_segments[num_segments].outputFile == NULL) {
                // Empty segment, likely due to "||" or trailing/leading "|"
                if (num_segments > 0 || strtok_r(NULL, "|", &saveptr_pipe) != NULL) { // only error if not the only segment or more segments follow
                     fprintf(stderr, "sdn: syntax error near `|'\n");
                     num_segments = -1;
                     break;
                }
            } else {
                 num_segments++;
            }
            command_str = strtok_r(NULL, "|", &saveptr_pipe);
        }

        if (num_segments == -1 || (num_segments == 0 && strlen(input_line_for_parsing) > 0) ) { 
             for (int k = 0; k < MAX_COMMAND_SEGMENTS; k++) { 
                free_command_segment_internals(&command_segments[k]);
            }
            continue;
        }
        if (num_segments == 0) {
            continue;
        }
        
        int built_in_executed = 0;
        if (num_segments == 1 && command_segments[0].args[0] != NULL) {
            if (strcmp(command_segments[0].args[0], "cd") == 0) {
                if (command_segments[0].args[1] == NULL) {
                    char *home_dir = getenv("HOME");
                    if (home_dir) {
                        if (chdir(home_dir) != 0) {
                            perror("sdn: cd failed");
                        }
                    } else {
                        fprintf(stderr, "sdn: cd: HOME not set\n");
                    }
                } else {
                    if (chdir(command_segments[0].args[1]) != 0) {
                        perror("sdn: cd failed");
                    }
                }
                built_in_executed = 1;
            } else if (strcmp(command_segments[0].args[0], "history") == 0) {
                display_history();
                built_in_executed = 1;
            } else if (strcmp(command_segments[0].args[0], "alias") == 0) {
                handle_alias_builtin(command_segments[0].args);
                built_in_executed = 1;
            } else if (strcmp(command_segments[0].args[0], "unalias") == 0) {
                handle_unalias_builtin(command_segments[0].args);
                built_in_executed = 1;
            }
        }

        if (!built_in_executed) {
            execute_pipeline(command_segments, num_segments, overall_background);
        }

        for (int k = 0; k < num_segments; k++) {
            free_command_segment_internals(&command_segments[k]);
        }
    }

    free_history_cache(&history_cache);
    return 0;
}
