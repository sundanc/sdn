#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <termios.h>
#include <ctype.h>

#define MAX_LINE 80 /* The maximum length command */
#define MAX_ARGS 20 /* The maximum number of arguments */
#define HISTORY_FILE_NAME ".sdn_history"
#define MAX_HISTORY_ENTRIES 1000

#define ANSI_COLOR_GRAY "\033[90m"
#define ANSI_COLOR_RESET "\033[0m"

struct termios orig_termios;

typedef struct {
    char *commands[MAX_HISTORY_ENTRIES];
    int count;
} HistoryCache;

// Function prototype
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

int read_line_with_completion(char *buffer, int max_size, HistoryCache *cache) {
    int c;
    int position = 0;
    char suggestion[MAX_LINE] = {0};
    int history_nav_idx = cache->count; // Current position in history navigation

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
        } else if (c == '\n' || c == '\r') {
            printf("\n");
            break;
        } else if (c == 127 || c == '\b') {
            if (position > 0) {
                position--;
                buffer[position] = '\0';
                history_nav_idx = cache->count; // Editing, so reset history navigation

                printf("\033[2K\r"); 
                printf("sdn> %s", buffer);
                
                char *match = find_matching_command(buffer, cache);
                if (match && strlen(buffer) > 0) { // Only suggest if buffer is not empty
                    strncpy(suggestion, match + position, MAX_LINE -1);
                    suggestion[MAX_LINE-1] = '\0';
                    printf("%s%s%s", ANSI_COLOR_GRAY, suggestion, ANSI_COLOR_RESET);
                    printf("\033[%dD", (int)strlen(suggestion)); 
                } else {
                    suggestion[0] = '\0';
                }
            }
        } else if (c == '\t') {
            if (suggestion[0] != '\0') {
                if (position + strlen(suggestion) < max_size -1) {
                    strcat(buffer, suggestion);
                    position += strlen(suggestion);
                }
                
                printf("\033[2K\r"); 
                printf("sdn> %s", buffer);
                suggestion[0] = '\0';
                history_nav_idx = cache->count; // Editing, so reset history navigation
            }
        } else if (c == 4) {
            disable_raw_mode();
            return -1;
        } else if (isprint(c)) {
            if (position < max_size - 1) {
                buffer[position++] = c;
                buffer[position] = '\0';
                history_nav_idx = cache->count; // Editing, so reset history navigation
                
                printf("%c", c);
                
                char *match = find_matching_command(buffer, cache);
                if (match) {
                    strncpy(suggestion, match + position, MAX_LINE -1);
                    suggestion[MAX_LINE-1] = '\0';
                    printf("%s%s%s", ANSI_COLOR_GRAY, suggestion, ANSI_COLOR_RESET);
                    printf("\033[%dD", (int)strlen(suggestion)); 
                } else {
                    suggestion[0] = '\0';
                }
            }
        }
    }
    
    disable_raw_mode();
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

// Function to parse the command line input
int parse_input(char *input, char **args) {
    int i = 0;
    int background = 0;
    char *token = strtok(input, " \n\t\r");
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \n\t\r");
    }
    args[i] = NULL; // Null-terminate the arguments array

    if (i > 0 && strcmp(args[i-1], "&") == 0) {
        background = 1;
        args[i-1] = NULL; 
    }
    return background;
}

int main(void) {
    char input[MAX_LINE];
    char history_entry_buffer[MAX_LINE];
    char *args[MAX_ARGS];
    pid_t pid, wpid; 
    int status;
    int background;
    
    HistoryCache history_cache = {0};
    load_history_cache(&history_cache);

    while (1) {
        
        while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
            printf("Shell: Background process with PID %d terminated.\n", wpid);
        }

        printf("sdn> ");
        fflush(stdout);

        int result = read_line_with_completion(input, sizeof(input), &history_cache);
        
        if (result == -1) {
            // Handle EOF (Ctrl+D)
            printf("\nExiting sdn.\n");
            break;
        }
        
        // Copy for history
        strncpy(history_entry_buffer, input, MAX_LINE - 1);
        history_entry_buffer[MAX_LINE - 1] = '\0';

        // Handle empty command
        if (strlen(input) == 0) {
            continue;
        }

        // Save to history and update cache
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
        
        // Handle "exit" command
        if (strcmp(input, "exit") == 0) {
            while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
                printf("Shell: Background process with PID %d terminated before exit.\n", wpid);
            }
            printf("Exiting sdn.\n");
            break;
        }

        // Parse the input
        background = parse_input(input, args);

        if (args[0] == NULL) { 
            continue;
        }
        
        if (strcmp(args[0], "cd") == 0) {
            if (args[1] == NULL) {
                
                
                
                char *home_dir = getenv("HOME");
                if (home_dir) {
                    if (chdir(home_dir) != 0) {
                        perror("sdn: cd failed");
                    }
                } else {
                    fprintf(stderr, "sdn: cd: HOME not set\n");
                }
            } else {
                if (chdir(args[1]) != 0) {
                    perror("sdn: cd failed");
                }
            }
            continue; 
        } else if (strcmp(args[0], "history") == 0) {
            display_history();
            continue;
        }

        // Fork a child process
        pid = fork();

        if (pid < 0) {
            // Error forking
            perror("fork failed");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // Child process
            if (execvp(args[0], args) == -1) {
                perror("execvp failed");
                exit(EXIT_FAILURE); // Exit child if execvp fails
            }
        } else {
            // Parent process
            if (background) {
                printf("[%d] %s &\n", pid, args[0]); 
            } else {
                // Wait for the foreground child to complete
                waitpid(pid, &status, 0);
            }
        }
    }

    free_history_cache(&history_cache);
    return 0;
}
