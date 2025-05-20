#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_LINE 80 /* The maximum length command */
#define MAX_ARGS 20 /* The maximum number of arguments */

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

        // Remove trailing newline character
        input[strcspn(input, "\n")] = 0;

        // Handle empty command
        if (strlen(input) == 0) {
            continue;
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
