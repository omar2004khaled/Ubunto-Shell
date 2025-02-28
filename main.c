#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

#define maxargs 100
#define maxinput 1024 
#define LOG_FILE "/home/omar/Desktop/UbuntuShell/logfile.txt"
#define RESET "\033[0m"
#define BLACK "\033[30m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define CYAN "\033[36m"


// Forward declarations
void register_child_signal();
void on_child_exit(int signum);
void reap_child_zombie();
void write_to_log_file(int pid, int status);
void setup_environment();
void shell_main();

char *current_dir;

//
void register_child_signal() {
    signal(SIGCHLD, on_child_exit);
}

void on_child_exit(int signum) {
    reap_child_zombie();
}

void reap_child_zombie() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        write_to_log_file(pid, status);
    }
}

void write_to_log_file(int pid, int status) {
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file != NULL) {
        fprintf(log_file, "Child PID %d terminated with status %d\n", pid, status);
        fclose(log_file);
    } else {
        perror("Failed to open log file");
    }
}

void setup_environment() {
    current_dir = getenv("HOME");
    if (current_dir == NULL) {
        perror("Unable to get HOME environment variable");
    }
    if (chdir(current_dir) == -1) {
        perror("chdir");
    }
}
//

char **parse_input(char *input) {
    char **argv = malloc(sizeof(char *) * (maxargs + 1));
    char *token;
    int argc = 0;

    if (!argv) {
        fprintf(stderr, "failed\n");
        exit(EXIT_FAILURE);
    }

    token = strsep(&input, " \t\n");
    while (token != NULL) {
        if (*token != '\0') {
            argv[argc++] = strdup(token);
        }
        token = strsep(&input, " \t\n");
    }

    argv[argc] = NULL;

    if (argc == 0) {
        free(argv);
        return NULL;
    }

    return argv;
}

int is_background(char **command_args) {
    int i = 0;
    while (command_args[i] != NULL) {
        i++;
    }

    if (i > 0 && strcmp(command_args[i - 1], "&") == 0) {
        command_args[i - 1] = NULL;
        return 1;
    }

    return 0;
}

char **evaluate_expression(char **parameters, int *background) {
    char **argv = malloc(sizeof(char *) * (maxargs + 1));
    int j = 0;

    if (!argv) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; parameters[i] != NULL; ++i) {
        char *result = parameters[i];
        if (result[0] == '"') {
            result = result + 1;
        }
        if (result[strlen(result) - 1] == '"') {
            result[strlen(result) - 1] = '\0';
        }
        if (strchr(result, '$') != NULL) {
            char *envvariable = NULL;
            if (result[0] == '$') {
                envvariable = strdup(result);
                envvariable++;
            } else {
                envvariable = strtok(result, "$");
                argv[j++] = strdup(envvariable);
                envvariable = strtok(NULL, "$");
            }

            char *ex_value = getenv(envvariable);

            char *tokenizer;
            char re[1000] = "";
            for (tokenizer = strtok(ex_value, " \t\n"); tokenizer != NULL; tokenizer = strtok(NULL, " \t\n")) {
                argv[j++] = strdup(tokenizer);
                strcat(re, tokenizer);
                strcat(re, " ");
            }
            if (setenv(envvariable, re, 1) != 0) {
                printf("Error: unable to set environment variable\n");
            }
        } else if (strcmp(result, "&") == 0) {
            *background = 1;
        } else {
            argv[j++] = strdup(result);
        }
    }
    argv[j] = NULL;
    return argv;
}

int is_builtin_command(char *command) {
    if (strcmp(command, "cd") == 0) {
        return 1;
    } else if (strcmp(command, "echo") == 0) {
        return 1;
    } else if (strcmp(command, "export") == 0) {
        return 1;
    }
    return 0;
}

void execute_shell_builtin(char **parameters) {
    if (strcmp(parameters[0], "cd") == 0) {
        if (parameters[1] == NULL || strcmp(parameters[1], "~") == 0) {
            const char *homedir = getenv("HOME");

            if (homedir != NULL) {
                if (chdir(homedir) != 0) {
                    perror("chdir");
                }
            } else {
                fprintf(stderr, "cd: could not find home directory\n");
            }
        } else if (parameters[1] != NULL) {
            if (chdir(parameters[1]) != 0) {
                perror("chdir");
            }
        }
    } else if (strcmp(parameters[0], "export") == 0) {
        char *name, *value;

        value = strchr(parameters[1], '=');
        if (value == NULL) {
            printf("Error: invalid input\n");
            return;
        }
        *value = '\0';
        name = parameters[1];
        value++;
        if (value[0] == '"') {
            value++;
        }
        if (value[strlen(value) - 1] == '"') {
            value[strlen(value) - 1] = '\0';
        }
        char re[1000] = "";
        strcat(re, value);
        for (int j = 2; parameters[j] != NULL; j++) {
            char *result = parameters[j];
            if (result[0] == '"') {
                result = result + 1;
            }
            if (result[strlen(result) - 1] == '"') {
                result[strlen(result) - 1] = '\0';
            }
            strcat(re, " ");
            strcat(re, result);
        }

        if (setenv(name, re, 1) != 0) {
            printf("Error: unable to set environment variable\n");
        }
    } else {
        for (int j = 1; parameters[j] != NULL; j++) {
            char *result = parameters[j];
            if (result[0] == '"') {
                result = result + 1;
            }
            if (result[strlen(result) - 1] == '"') {
                result[strlen(result) - 1] = '\0';
            }
            printf("%s ", result);
        }
        printf("\n");
    }
}

void execute_command(char **command_args, int background) {
    pid_t child_pid = fork();

    if (child_pid == -1) {
        perror("fork failed");
        exit(1);
    }

    if (child_pid == 0) {
        if (execvp(command_args[0], command_args) == -1) {
            perror("Error executing command");
            exit(1);
        }
    } else {
        int status;
        if (!background) {
            waitpid(child_pid, &status, 0);
        } else {
            printf("Background process started with PID: %d\n", child_pid);
            waitpid(child_pid, &status, WNOHANG);
        }
    }
}

void shell_main() {
    char *input = NULL;
    size_t len = 0;
    ssize_t read;
    printf(CYAN "\n");
    printf(CYAN "*                                                                                                  *\n");
    printf(CYAN "*    " GREEN "██╗    ██╗███████╗██╗     ██╗      ███████╗ ████████╗ ███╗   ███╗███████╗   "  CYAN "*\n");
    printf(CYAN "*    " GREEN "██║    ██║██╔════╝██║     ██║      ██╔════╝ ██╔══ ██║ ████╗ ████║██╔════╝    " CYAN "*\n");
    printf(CYAN "*    " GREEN "██║ █╗ ██║█████╗  ██║     ██║      ██       ██    ██║ ██╔████╔██║█████╗   "    CYAN "*\n");
    printf(CYAN "*    " GREEN "██║███╗██║██╔══╝  ██║     ██║      ██       ██    ██║ ██║╚██╔╝██║██╔══╝   "    CYAN "*\n");
    printf(CYAN "*    " GREEN "╚███╔███╔╝███████╗███████╗███████╗ ███████╗ ████████║ ██║ ╚═╝ ██║███████╗   "  CYAN "*\n");
    printf(CYAN "*    ╚══╝ ╚══╝ ╚══════╝╚══════╝╚══════╝ ╚══════╝ ╚═══════╝ ╚═╝     ╚═╝╚══════╝ "    CYAN "*\n");
    printf(CYAN "*                                                                                                  *\n");
   

    while (1) {
        // Display the shell prompt
        printf("%s%s%s$ ", CYAN, current_dir, RESET);
        fflush(stdout);

        // Read user input
        read = getline(&input, &len, stdin);
        if (read == -1) {
            if (feof(stdin)) {
                printf("\n"); // Handle EOF (Ctrl+D)
                break;
            } else {
                perror("getline failed");
                continue;
            }
        }

        // Remove the newline character from the input
        input[strcspn(input, "\n")] = '\0';

        // Parse the input into arguments
        char **parameters = parse_input(input);
        if (parameters == NULL) {
            continue; // Skip empty input
        }

        // Check if the command should run in the background
        int background = is_background(parameters);

        // Evaluate expressions (e.g., environment variables)
        char **command = evaluate_expression(parameters, &background);

        // Handle the "exit" command
        if (strcmp(command[0], "exit") == 0) {
            free(input);
            for (int i = 0; parameters[i] != NULL; ++i) free(parameters[i]);
            free(parameters);
            for (int i = 0; command[i] != NULL; ++i) free(command[i]);
            free(command);
            exit(0);
        }

        // Execute built-in commands or external commands
        if (is_builtin_command(command[0])) {
            execute_shell_builtin(command);
        } else {
            execute_command(command, background);
        }

        // Free allocated memory for parameters and command 
        for (int i = 0; parameters[i] != NULL; ++i) free(parameters[i]);
        free(parameters);
        for (int i = 0; command[i] != NULL; ++i) free(command[i]);
        free(command);
    }

    // Free the input buffer
    free(input);
}

int main() {
    register_child_signal();
    setup_environment();
    shell_main();
}
