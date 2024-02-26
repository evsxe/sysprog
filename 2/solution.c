#include "custom_parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

struct custom_pipes {
    bool is_input_pipe;
    bool is_output_pipe;
    int input_fd;
    int output_fd;
};

char** add_custom_cmd_name_to_args(const struct custom_command *cmd) {
    char **temp = malloc(sizeof(char*) * (cmd->arg_count + 2));
    temp[0] = strdup(cmd->exe);

    for (uint32_t i = 0; i < cmd->arg_count; ++i)
    {
        temp[i + 1] = strdup(cmd->args[i]);
    }
    temp[cmd->arg_count + 1] = NULL;

    return temp;
}

static void execute_custom_cd(const struct custom_command *cmd) {
    assert(cmd != NULL);
    assert(cmd->exe != NULL);

    if (strcmp(cmd->exe, "cd") == 0) {
        if (cmd->arg_count == 1) {
            if (chdir(cmd->args[0]) == 0) {
                printf("Changed directory to: %s\n", cmd->args[0]);
            }
            else {
                if (errno == ENOENT) {
                    fprintf(stderr, "cd: No such file or directory: %s\n", cmd->args[0]);
                }
                else {
                    perror("chdir");
                    exit(EXIT_FAILURE);
                }
            }
        }
        else if (cmd->arg_count > 1) {
            fprintf(stderr, "cd: Too many arguments\n");
        }
        else if (cmd->arg_count == 0) {
            if (chdir(getenv("HOME")) == 0) {
                printf("Changed directory to home\n");
            }
            else {
                perror("chdir");
                exit(EXIT_FAILURE);
            }
        }
        else {
            fprintf(stderr, "cd: Missing arguments\n");
        }
    }
}

static void execute_custom_exit(const struct custom_command *cmd) {
    assert(cmd != NULL);
    assert(cmd->exe != NULL);

    if (strcmp(cmd->exe, "exit") == 0) {
        if (cmd->arg_count > 1) {
            fprintf(stderr, "exit: Too many arguments\n");
        }
        else {
            exit(EXIT_SUCCESS);
        }
    }
}

static void execute_custom_command(const struct custom_command *cmd) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0) {
        if (cmd->exe == NULL) {
            fprintf(stderr, "execute_command: Missing executable name\n");
            exit(EXIT_FAILURE);
        }

        char** args = add_custom_cmd_name_to_args(cmd);

        if (execvp(cmd->exe, args) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
        else {
            exit(EXIT_SUCCESS);
        }
    }
    else {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            exit(EXIT_FAILURE);
        }
    }
}

static void
execute_custom_command_line(const struct custom_command_line *line) {
    assert(line != NULL);
    const struct custom_expr *e = line->head;
    int pipefd[2], lastfd = -1;

    if (e != NULL && e->type == EXPR_TYPE_COMMAND &&
        strcmp(e->cmd.exe, "cd") == 0 && e->next == NULL) {
        execute_custom_cd(&e->cmd);
        return;
    }

    if (e != NULL && e->type == EXPR_TYPE_COMMAND &&
        strcmp(e->cmd.exe, "exit") == 0 && e->next == NULL) {
        execute_custom_exit(&e->cmd);
        return;
    }

    while (e != NULL) {
        if (e->type == EXPR_TYPE_COMMAND) {
            if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                if (pipe(pipefd) == -1) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
            }

            pid_t pid = fork();
            if (pid == -1) {
                perror("FORK");
                exit(EXIT_FAILURE);
            }
            else if (pid == 0) {
                if (lastfd != -1) {
                    dup2(lastfd, STDIN_FILENO);
                    close(lastfd);
                }

                int outfd = STDOUT_FILENO;
                if (e->next == NULL || e->next->type != EXPR_TYPE_PIPE) {
                    if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
                        outfd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    }
                    else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
                        outfd = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                    }
                    if (outfd == -1) {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }
                    dup2(outfd, STDOUT_FILENO);
                    if (outfd != STDOUT_FILENO) {
                        close(outfd);
                    }
                }
                execute_custom_command(&e->cmd);
            }
            else {
                if (lastfd != -1) {
                    close(lastfd);
                }
                if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                    lastfd = pipefd[0];
                    close(pipefd[1]);
                }
                else {
                    lastfd = -1;
                }
                int status;
                if (waitpid(pid, &status, 0) == -1) {
                    perror("waitpid");
                    exit(EXIT_FAILURE);
                }
            }
        }

        e = e->next;

        if (lastfd != -1) {
            close(lastfd);
        }
    }
}

int main(void) {
    const size_t buf_size = 1024;
    char buf[buf_size];
    int rc;
    struct custom_parser *p = custom_parser_new();

    while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
        custom_parser_feed(p, buf, rc);
        struct custom_command_line *line = NULL;
        while (true) {
            enum custom_parser_error err = custom_parser_pop_next(p, &line);
            if (err == PARSER_ERR_NONE && line == NULL)
                break;
            if (err != PARSER_ERR_NONE) {
                printf("Error: %d\n", (int)err);
                continue;
            }
            execute_custom_command_line(line);
            custom_command_line_delete(line);
        }
    }
    custom_parser_delete(p);
    return 0;
}