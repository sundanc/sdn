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
#define LOCAL_ALIASES_FILENAME ".sdn_local_aliases"

#define MAX_VARIABLES 100
#define MAX_VAR_NAME_LEN 50
#define MAX_VAR_VALUE_LEN MAX_LINE

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

typedef struct {
    char name[MAX_VAR_NAME_LEN];
    char value[MAX_VAR_VALUE_LEN];
} VariableEntry;

AliasEntry alias_table[MAX_ALIASES];
int alias_count = 0;

AliasEntry local_alias_table[MAX_ALIASES];
int local_alias_count = 0;

VariableEntry variable_table[MAX_VARIABLES];
int variable_count = 0;

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
    // Check local aliases first
    for (int i = 0; i < local_alias_count; i++) {
        if (strcmp(local_alias_table[i].name, name) == 0) {
            return local_alias_table[i].command;
        }
    }
    // Then check global aliases
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(alias_table[i].name, name) == 0) {
            return alias_table[i].command;
        }
    }
    return NULL;
}

void add_alias_to_table(AliasEntry table[], int *count, const char *name, const char *command, int max_aliases) {
    if (strlen(name) >= MAX_ALIAS_NAME_LEN || strlen(command) >= MAX_ALIAS_COMMAND_LEN) {
        fprintf(stderr, "sdn: alias name or command too long\n");
        return;
    }
    for (int i = 0; i < *count; i++) {
        if (strcmp(table[i].name, name) == 0) {
            strncpy(table[i].command, command, MAX_ALIAS_COMMAND_LEN -1);
            table[i].command[MAX_ALIAS_COMMAND_LEN -1] = '\0';
            return;
        }
    }
    if (*count < max_aliases) {
        strncpy(table[*count].name, name, MAX_ALIAS_NAME_LEN - 1);
        table[*count].name[MAX_ALIAS_NAME_LEN - 1] = '\0';
        strncpy(table[*count].command, command, MAX_ALIAS_COMMAND_LEN - 1);
        table[*count].command[MAX_ALIAS_COMMAND_LEN - 1] = '\0';
        (*count)++;
    } else {
        fprintf(stderr, "sdn: alias table full\n");
    }
}

void add_or_update_alias(const char *name, const char *command) {
    add_alias_to_table(alias_table, &alias_count, name, command, MAX_ALIASES);
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
    printf("Global Aliases:\n");
    for (int i = 0; i < alias_count; i++) {
        printf("  %s='%s'\n", alias_table[i].name, alias_table[i].command);
    }
    if (local_alias_count > 0) {
        printf("Local Aliases (current directory):\n");
        for (int i = 0; i < local_alias_count; i++) {
            printf("  %s='%s'\n", local_alias_table[i].name, local_alias_table[i].command);
        }
    }
}

void handle_alias_builtin(char **args) {
    if (args[1] == NULL) {
        print_all_aliases();
        return;
    }

    char reconstructed_assignment[MAX_LINE]; // Buffer for "name=value"
    char *first_arg_equals_ptr = strchr(args[1], '=');

    if (first_arg_equals_ptr != NULL) {
        strncpy(reconstructed_assignment, args[1], sizeof(reconstructed_assignment) - 1);
        reconstructed_assignment[sizeof(reconstructed_assignment) - 1] = '\0';

        for (int i = 2; args[i] != NULL; i++) {
            size_t current_len = strlen(reconstructed_assignment);
            if (current_len < sizeof(reconstructed_assignment) - 1) {
                reconstructed_assignment[current_len++] = ' '; // Add space separator
                reconstructed_assignment[current_len] = '\0';   // Null-terminate for strncat
            } else {
                fprintf(stderr, "sdn: alias: command too long after reconstructing arguments\n");
                break; 
            }

            if (current_len < sizeof(reconstructed_assignment) - 1) {
                 strncat(reconstructed_assignment, args[i], sizeof(reconstructed_assignment) - 1 - current_len);
            } else {
                fprintf(stderr, "sdn: alias: command too long, cannot append further arguments\n");
                break; 
            }
        }
        
        char alias_name[MAX_ALIAS_NAME_LEN];
        char alias_value[MAX_ALIAS_COMMAND_LEN];
        
        char *equals_ptr = strchr(reconstructed_assignment, '='); 
        
        if (equals_ptr == NULL) { 
            fprintf(stderr, "sdn: alias: internal error parsing assignment\n");
            return;
        }

        size_t name_len = equals_ptr - reconstructed_assignment;
        if (name_len == 0 || name_len >= MAX_ALIAS_NAME_LEN) {
            fprintf(stderr, "sdn: alias: invalid alias name\n");
            return;
        }
        strncpy(alias_name, reconstructed_assignment, name_len);
        alias_name[name_len] = '\0';

        char *value_start = equals_ptr + 1;
        strncpy(alias_value, value_start, MAX_ALIAS_COMMAND_LEN - 1);
        alias_value[MAX_ALIAS_COMMAND_LEN - 1] = '\0';

        size_t val_len = strlen(alias_value);
        if (val_len >= 2 && 
            ((alias_value[0] == '"' && alias_value[val_len-1] == '"') ||
             (alias_value[0] == '\'' && alias_value[val_len-1] == '\''))) {
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

void clear_local_aliases() {
    // For simplicity, we just reset the count.
    // A more robust implementation would free strduped names/commands if they were dynamically allocated per entry.
    // Since we use fixed-size arrays and strncpy, this is okay for now.
    local_alias_count = 0;
    // If AliasEntry members were char*, you'd loop and free them here.
}

void load_local_aliases(const char *current_dir_path) {
    char local_alias_file_path[FILENAME_MAX];
    snprintf(local_alias_file_path, sizeof(local_alias_file_path), "%s/%s", current_dir_path, LOCAL_ALIASES_FILENAME);

    FILE *fp = fopen(local_alias_file_path, "r");
    if (!fp) {
        return; // No local alias file, or not readable
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0; // Remove newline

        char *equals_ptr = strchr(line, '=');
        if (equals_ptr != NULL) {
            char alias_name[MAX_ALIAS_NAME_LEN];
            char alias_value[MAX_ALIAS_COMMAND_LEN];

            size_t name_len = equals_ptr - line;
            if (name_len > 0 && name_len < MAX_ALIAS_NAME_LEN) {
                strncpy(alias_name, line, name_len);
                alias_name[name_len] = '\0';

                char *value_start = equals_ptr + 1;
                strncpy(alias_value, value_start, MAX_ALIAS_COMMAND_LEN - 1);
                alias_value[MAX_ALIAS_COMMAND_LEN - 1] = '\0';
                
                // Optional: remove quotes like in global alias handling
                size_t val_len = strlen(alias_value);
                if (val_len >= 2 && ((alias_value[0] == '"' && alias_value[val_len-1] == '"') ||
                    (alias_value[0] == '\'' && alias_value[val_len-1] == '\''))) {
                    memmove(alias_value, alias_value + 1, val_len - 2);
                    alias_value[val_len - 2] = '\0';
                }
                add_alias_to_table(local_alias_table, &local_alias_count, alias_name, alias_value, MAX_ALIASES);
            }
        }
    }
    fclose(fp);
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
            char *full_path_base; 
            size_t needed_len;
            if (strcmp(dir_path, ".") == 0) {
                full_path_base = strdup(entry->d_name);
            } else {
                // Ensure dir_path ends with a '/' if it's a directory path other than "."
                // This was a potential source of issues if dir_path was e.g. "/foo" and entry->d_name was "bar"
                // resulting in "/foobar" instead of "/foo/bar"
                // However, the logic already tries to add / to dir_path if last_slash is found.
                // The main concern here is buffer overflow with sprintf.
                needed_len = strlen(dir_path) + strlen(entry->d_name) + 2; // +1 for potential '/', +1 for null
                full_path_base = malloc(needed_len);
                if (full_path_base) {
                    // Check if dir_path already ends with a slash or is empty (should not be for non-"." case)
                    if (strlen(dir_path) > 0 && dir_path[strlen(dir_path)-1] == '/') {
                        snprintf(full_path_base, needed_len, "%s%s", dir_path, entry->d_name);
                    } else {
                        snprintf(full_path_base, needed_len, "%s/%s", dir_path, entry->d_name);
                    }
                }
            }

            if (!full_path_base) {
                perror("sdn: malloc/strdup failed in find_matching_files");
                continue;
            }
            
            // Add a slash to directories
            if (entry->d_type == DT_DIR) {
                size_t base_len = strlen(full_path_base);
                needed_len = base_len + 2; // +1 for '/' and +1 for null terminator
                char *with_slash = malloc(needed_len);
                if (with_slash) {
                    snprintf(with_slash, needed_len, "%s/", full_path_base);
                    free(full_path_base);
                    full_path_base = with_slash;
                } else {
                    perror("sdn: malloc failed for directory slash");
                    free(full_path_base);
                    continue;
                }
            }
            
            add_file_match(matches, full_path_base);
            free(full_path_base);
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

void get_prompt(char *prompt_buffer, size_t buffer_size) {
    char cwd[FILENAME_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        snprintf(prompt_buffer, buffer_size, "%s> ", cwd);
    } else {
        strncpy(prompt_buffer, "sdn> ", buffer_size);
        prompt_buffer[buffer_size - 1] = '\0';
    }
}

int read_line_with_completion(char *buffer, int max_size, HistoryCache *cache) {
    int c;
    int position = 0;
    char suggestion[MAX_LINE] = {0}; // Initialize to empty
    int history_nav_idx = cache->count; // Current position in history navigation
    FileMatches file_matches;
    init_file_matches(&file_matches);
    char prompt[FILENAME_MAX + 3]; 
    get_prompt(prompt, sizeof(prompt));

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
                        printf("%s%s", prompt, buffer);
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
                        printf("%s%s", prompt, buffer);
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
                printf("%s%s", prompt, buffer);
                
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
                if (position + strlen(suggestion) < (size_t)(max_size -1)) { // Cast to size_t
                    strcat(buffer, suggestion);
                    position += strlen(suggestion);
                }
                
                printf("\033[2K\r"); 
                printf("%s%s", prompt, buffer);
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
                            printf("%s%s", prompt, buffer);
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
                        printf("%s%s", prompt, buffer);
                        
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
                
                // Autocomplete suggestion logic
                // Update suggestion based on current buffer
                // For command completion (first word)
                if (strchr(buffer, ' ') == NULL) { 
                    char *match = find_matching_command(buffer, cache);
                    if (match) {
                        strncpy(suggestion, match + strlen(buffer), MAX_LINE - 1 - strlen(buffer));
                        suggestion[MAX_LINE - 1 - strlen(buffer)] = '\0';
                    } else {
                        suggestion[0] = '\0';
                    }
                } else { // For file/directory completion
                    // ... (file completion suggestion logic might go here or be part of tab handling) ...
                    suggestion[0] = '\0'; // Clear for now if not first word
                }

                // Display current buffer + suggestion
                // Ensure this part is correct and doesn't cause issues
                printf("\r%s%s%s%s", prompt, buffer, ANSI_COLOR_GRAY, suggestion);
                printf(ANSI_COLOR_RESET);
                // Move cursor back to position after buffer
                if (strlen(suggestion) > 0) {
                    for (size_t i = 0; i < strlen(suggestion); ++i) printf("\b");
                }

                // This was the location of the warning.
                // The check should be: (current buffer length) + (suggestion length) < max_size
                // position is already the new length of buffer *after* adding char c.
                if (position + strlen(suggestion) < (size_t)max_size) {
                    // It seems the suggestion display logic is already above.
                    // This check might be redundant or misplaced if suggestion is already part of display.
                    // The primary concern is that `buffer` itself doesn't overflow.
                    // `position < max_size - 1` already handles buffer overflow for `c`.
                    // If `suggestion` is meant to be appended, that needs careful handling.
                } else {
                    // If suggestion would cause overflow if appended, clear it.
                    // suggestion[0] = '\0'; 
                    // The current display logic prints buffer then suggestion separately, 
                    // so overflow of a combined string isn't the direct issue here,
                    // but rather the visual representation and ensuring `buffer` is safe.
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

// Helper to check for valid variable name characters
int is_valid_identifier_char(char c) {
    return isalnum(c) || c == '_';
}

// Helper to validate a variable name
int is_valid_variable_name(const char *name) {
    if (name == NULL || *name == '\0' || isdigit(*name)) {
        return 0; // Cannot be empty, NULL, or start with a digit
    }
    for (int i = 0; name[i] != '\0'; i++) {
        if (!is_valid_identifier_char(name[i])) {
            return 0; // Contains invalid character
        }
    }
    return 1;
}

// Helper to remove matching leading/trailing quotes (" or ') from a string in-place
char* unquote_string_in_place(char *str) {
    if (!str) return str;
    int len = strlen(str);
    if (len >= 2) {
        char first = str[0];
        char last = str[len - 1];
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            memmove(str, str + 1, len - 2);
            str[len - 2] = '\0';
        }
    }
    return str;
}

// Function to set or update a shell variable
void set_shell_variable(const char *name, const char *value) {
    if (!is_valid_variable_name(name)) {
        fprintf(stderr, "sdn: invalid variable name: %s\n", name);
        return;
    }
    if (strlen(name) >= MAX_VAR_NAME_LEN || strlen(value) >= MAX_VAR_VALUE_LEN) {
        fprintf(stderr, "sdn: variable name or value too long\n");
        return;
    }

    for (int i = 0; i < variable_count; i++) {
        if (strcmp(variable_table[i].name, name) == 0) {
            strncpy(variable_table[i].value, value, MAX_VAR_VALUE_LEN - 1);
            variable_table[i].value[MAX_VAR_VALUE_LEN - 1] = '\0';
            return;
        }
    }
    if (variable_count < MAX_VARIABLES) {
        strncpy(variable_table[variable_count].name, name, MAX_VAR_NAME_LEN - 1);
        variable_table[variable_count].name[MAX_VAR_NAME_LEN - 1] = '\0';
        strncpy(variable_table[variable_count].value, value, MAX_VAR_VALUE_LEN - 1);
        variable_table[variable_count].value[MAX_VAR_VALUE_LEN - 1] = '\0';
        variable_count++;
    } else {
        fprintf(stderr, "sdn: maximum number of variables reached\n");
    }
}

// Function to get a shell variable's value
const char *get_shell_variable(const char *name) {
    for (int i = 0; i < variable_count; i++) {
        if (strcmp(variable_table[i].name, name) == 0) {
            return variable_table[i].value;
        }
    }
    // Optionally, could check getenv(name) here as a fallback if desired
    return NULL;
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

// Function to expand variables in a list of arguments
void expand_variables_in_args(char **args) {
    if (!args) return;
    for (int i = 0; args[i] != NULL; i++) {
        if (args[i][0] == '$') {
            const char *var_name = args[i] + 1; // Skip '$'
            const char *value = get_shell_variable(var_name);
            
            // Environment variable fallback if not in shell variables
            if (value == NULL) {
                value = getenv(var_name);
            }

            if (value != NULL) {
                free(args[i]); 
                args[i] = strdup(value);
                if (!args[i]) {
                    perror("sdn: strdup error during variable expansion");
                }
            } else {
                // Variable not found, replace with empty string
                free(args[i]); 
                args[i] = strdup("");
                 if (!args[i]) {
                    perror("sdn: strdup error during variable expansion (empty)");
                }
            }
        }
    }
}

void handle_export_builtin(char **args) {
    if (args[1] == NULL) {
        // List all environment variables set by this shell instance (those in variable_table and also in environ)
        // Or simply list all shell variables that have been exported.
        // For simplicity now, list all shell variables and mark if they are in environ.
        printf("Shell Variables (export VAR or VAR=value to set/export):\n");
        for(int i=0; i<variable_count; ++i) {
            const char* env_val = getenv(variable_table[i].name);
            printf("  %s=%s%s\n", variable_table[i].name, variable_table[i].value, env_val ? " (exported)" : "");
        }
        return;
    }

    for (int i = 1; args[i] != NULL; i++) {
        char *arg_copy = strdup(args[i]);
        if (!arg_copy) {
            perror("sdn: strdup failed in export");
            continue;
        }

        char *eq_ptr = strchr(arg_copy, '=');
        char *var_name;
        const char *var_value_str; // This will be the string representation of the value

        if (eq_ptr != NULL) { // Case: export VAR=value
            *eq_ptr = '\0'; // Split name and value
            var_name = arg_copy;
            var_value_str = eq_ptr + 1;
            
            // Unquote the value part if it's quoted
            char temp_value_for_unquoting[MAX_VAR_VALUE_LEN];
            strncpy(temp_value_for_unquoting, var_value_str, MAX_VAR_VALUE_LEN -1);
            temp_value_for_unquoting[MAX_VAR_VALUE_LEN-1] = '\0';
            unquote_string_in_place(temp_value_for_unquoting);

            if (!is_valid_variable_name(var_name)) {
                 fprintf(stderr, "sdn: export: '%s': not a valid identifier\n", var_name);
                 free(arg_copy);
                 continue;
            }
            set_shell_variable(var_name, temp_value_for_unquoting); // Set/update in shell's internal table
            // Use temp_value_for_unquoting for setenv as well
            if (setenv(var_name, temp_value_for_unquoting, 1) != 0) {
                perror("sdn: export: setenv failed");
            }
        } else { // Case: export VAR
            var_name = arg_copy;
            if (!is_valid_variable_name(var_name)) {
                 fprintf(stderr, "sdn: export: '%s': not a valid identifier\n", var_name);
                 free(arg_copy);
                 continue;
            }
            var_value_str = get_shell_variable(var_name); // Get from shell's internal table
            if (var_value_str != NULL) {
                if (setenv(var_name, var_value_str, 1) != 0) { // Value is already unquoted from table
                    perror("sdn: export: setenv failed");
                }
            } else {
                 // If not in shell variables, check if it's an environment variable already
                 // If so, it's effectively "exported". If not, it's an error to export non-existent var.
                if (getenv(var_name) == NULL) {
                    fprintf(stderr, "sdn: export: variable '%s' not found in shell or environment\n", var_name);
                }
                // If it exists in env but not shell, setenv will effectively re-export it or do nothing if value is same.
                // No explicit action needed if it's already an env var but not a shell var.
            }
        }
        free(arg_copy);
    }
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

    // Initial load of local aliases for the starting directory
    char initial_cwd[FILENAME_MAX];
    if (getcwd(initial_cwd, sizeof(initial_cwd)) != NULL) {
        load_local_aliases(initial_cwd);
    }

    while (1) {
        
        while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
            printf("Shell: Background process with PID %d terminated.\n", wpid);
        }

        char prompt[FILENAME_MAX + 3];
        get_prompt(prompt, sizeof(prompt));
        printf("%s", prompt);
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
            // Built-in command check
            if (strcmp(command_segments[0].args[0], "cd") == 0) {
                char target_dir[FILENAME_MAX];
                if (command_segments[0].args[1] == NULL) {
                    char *home_dir = getenv("HOME");
                    if (home_dir) {
                        strncpy(target_dir, home_dir, FILENAME_MAX -1);
                        target_dir[FILENAME_MAX-1] = '\0';
                    } else {
                        fprintf(stderr, "sdn: cd: HOME not set\n");
                        target_dir[0] = '\0'; 
                    }
                } else {
                    strncpy(target_dir, command_segments[0].args[1], FILENAME_MAX -1);
                    target_dir[FILENAME_MAX-1] = '\0';
                }

                if (target_dir[0] != '\0') {
                    clear_local_aliases(); // Clear old local aliases
                    if (chdir(target_dir) != 0) {
                        perror("sdn: cd failed");
                        // Attempt to reload local aliases for the original directory if chdir failed
                        // though current_dir_path might be stale if chdir modified it partially
                        // For simplicity, we might just leave local aliases cleared or try to get CWD again.
                        char current_cwd_after_fail[FILENAME_MAX];
                         if (getcwd(current_cwd_after_fail, sizeof(current_cwd_after_fail)) != NULL) {
                            load_local_aliases(current_cwd_after_fail);
                        }
                    } else {
                        // chdir was successful, load new local aliases
                        char new_cwd[FILENAME_MAX];
                        if (getcwd(new_cwd, sizeof(new_cwd)) != NULL) {
                            load_local_aliases(new_cwd);
                        } else {
                            perror("sdn: getcwd failed after cd");
                        }
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
            } else if (strcmp(command_segments[0].args[0], "export") == 0) {
                handle_export_builtin(command_segments[0].args);
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
