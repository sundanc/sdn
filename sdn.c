#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>


#define MAX_LINE 80 /* The maximum length command */
#define MAX_ARGS 20 /* The maximum number of arguments */
#define HISTORY_FILE_NAME ".sdn_history"


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
        fprintf(fp, "%s\n", command);
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
        char line[MAX_LINE + 2]; 
        int count = 1;
        while (fgets(line, sizeof(line), fp)) {
            printf("%5d  %s", count++, line);
        }
        fclose(fp);
    } else {
        
        if (access(history_file_path, F_OK) == -1) {
             
             
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

    while (1) {
        
        while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
            
            
            printf("Shell: Background process with PID %d terminated.\n", wpid);
        }

        printf("sdn> ");
        fflush(stdout); // Ensure the prompt is displayed

        // Read input
        if (fgets(input, sizeof(input), stdin) == NULL) {
            // Handle EOF (Ctrl+D)
            printf("\nExiting sdn.\n");
            break;
        }

        
        strncpy(history_entry_buffer, input, MAX_LINE - 1);
        history_entry_buffer[MAX_LINE - 1] = '\0'; 
        history_entry_buffer[strcspn(history_entry_buffer, "\n")] = 0; 

        // Prepare input for parsing
        input[strcspn(input, "\n")] = 0;

        // Handle empty command
        if (strlen(input) == 0) {
            continue;
        }

        
        if (strlen(history_entry_buffer) > 0) {
            save_to_history(history_entry_buffer);
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

    return 0;
}
