/*--------------------------------------------------------------------*/
/* execute.c */
/* Author: Jongki Park, Kyoungsoo Park */
/*--------------------------------------------------------------------*/

#include "dynarray.h"
#include "token.h"
#include "util.h"
#include "lexsyn.h"
#include "snush.h"
#include "execute.h"
#include "job.h"

extern struct job_manager *manager;
extern volatile sig_atomic_t sigchld_flag;
extern volatile sig_atomic_t sigint_flag;

/* Structure to store completed background job information (defined in snush.c) */
struct completed_bg_job {
    int job_id;
    pid_t pgid;
};
extern struct completed_bg_job completed_bg_jobs[];
extern int n_completed_bg_jobs;

/*--------------------------------------------------------------------*/
void block_signal(int sig, int block) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL) < 0) {
        fprintf(stderr, 
            "[Error] block_signal: sigprocmask(%s, sig=%d) failed: %s\n",
            block ? "SIG_BLOCK" : "SIG_UNBLOCK", sig, strerror(errno));
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
void handle_sigchld(void) {

    /*
     * TODO: Implement handle_sigchld() in execute.c
     * Call waitpid() to wait for the child process to terminate.
     * If the child process terminates, handle the job accordingly.
     * Be careful to handle the SIGCHLD signal flag and unblock SIGCHLD.
    */
    pid_t pid;
    int status;
    struct job *job;
    
    /* Check if SIGCHLD signal was received */
    if (!sigchld_flag)
        return;
    
    /* Block SIGCHLD while processing to avoid race conditions */
    block_signal(SIGCHLD, TRUE);
    
    /* Reset the flag */
    sigchld_flag = 0;
    
    /* Wait for any child process to terminate (non-blocking) */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        /* Find the job containing this PID */
        job = find_job_by_pid(pid);
        
        if (job == NULL) {
            fprintf(stderr, "[Error] Job not found for PID %d\n", pid);
            exit(EXIT_FAILURE);
        }
        
        if (remove_pid_from_job(job, pid) && job->remaining_processes == 0) {
            if (job->state == BACKGROUND && n_completed_bg_jobs < MAX_JOBS) {
                completed_bg_jobs[n_completed_bg_jobs].job_id = job->job_id;
                completed_bg_jobs[n_completed_bg_jobs].pgid = job->pgid;
                n_completed_bg_jobs++;
            }
            delete_job(job->job_id);
        }
    }
    
    /* Unblock SIGCHLD */
    block_signal(SIGCHLD, FALSE);
}
/*--------------------------------------------------------------------*/
void handle_sigint(void) {
    
    /*
     * TODO: Implement handle_sigint() in execute.c
     * Find the foreground job and send signal to every process in the
     * process group.
     * Be careful to handle the SIGINT signal flag and unblock SIGINT.
     */
    struct job *job;
    
    /* Check if SIGINT signal was received */
    if (!sigint_flag)
        return;
    
    /* Block SIGINT while processing to avoid race conditions */
    block_signal(SIGINT, TRUE);
    
    /* Reset the flag */
    sigint_flag = 0;
    
    /* Find the foreground job */
    job = find_foreground_job();
    
    if (job != NULL) {
        /* Send SIGINT to all processes in the process group */
        if (kill(-job->pgid, SIGINT) < 0) {
            /* Ignore error if process group doesn't exist */
        }
    }
    
    /* Unblock SIGINT */
    block_signal(SIGINT, FALSE);
}
/*--------------------------------------------------------------------*/
void dup2_e(int oldfd, int newfd, const char *func, const int line) {
    int ret;

    ret = dup2(oldfd, newfd);
    if (ret < 0) {
        fprintf(stderr, 
            "Error dup2(%d, %d): %s(%s) at (%s:%d)\n", 
            oldfd, newfd, strerror(errno), errno_name(errno), func, line);
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
/* Do not modify this function. It is used to check the signals and 
 * handle them accordingly. It is called in the main loop of snush.c.
 */
void check_signals(void) {
    handle_sigchld();
    handle_sigint();
}
/*--------------------------------------------------------------------*/
void redout_handler(char *fname) {
    /*
     TODO: Implement redout_handler in execute.c
    */
    int fd;
    
    /* Open file for writing, create if doesn't exist, truncate if exists */
    fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        error_print(NULL, PERROR);
        exit(EXIT_FAILURE);
    }
    
    dup2_e(fd, STDOUT_FILENO, __func__, __LINE__);
    close(fd);
}
/*--------------------------------------------------------------------*/
void redin_handler(char *fname) {
    int fd;

    fd = open(fname, O_RDONLY);
    if (fd < 0) {
        error_print(NULL, PERROR);
        exit(EXIT_FAILURE);
    }

    dup2_e(fd, STDIN_FILENO, __func__, __LINE__);
    close(fd);
}
/*--------------------------------------------------------------------*/
void build_command_partial(DynArray_T oTokens, int start, 
                        int end, char *args[]) {
    int i, redin = FALSE, redout = FALSE, cnt = 0;
    struct Token *t;

    /* Build command */
    for (i = start; i < end; i++) {
        t = dynarray_get(oTokens, i);

        if (t->token_type == TOKEN_WORD) {
            if (redin == TRUE) {
                redin_handler(t->token_value);
                redin = FALSE;
            }
            else if (redout == TRUE) {
                redout_handler(t->token_value);
                redout = FALSE;
            }
            else {
                args[cnt++] = t->token_value;
            }
        }
        else if (t->token_type == TOKEN_REDIN)
            redin = TRUE;
        else if (t->token_type == TOKEN_REDOUT)
            redout = TRUE;
    }

    if (cnt >= MAX_ARGS_CNT) 
        fprintf(stderr, "[BUG] args overflow! cnt=%d\n", cnt);

    args[cnt] = NULL;

#ifdef DEBUG
    for (i = 0; i < cnt; i++) {
        if (args[i] == NULL)
            printf("CMD: NULL\n");
        else
            printf("CMD: %s\n", args[i]);
    }
    printf("END\n");
#endif
}
/*--------------------------------------------------------------------*/
void build_command(DynArray_T oTokens, char *args[]) {
    build_command_partial(oTokens, 0, 
                        dynarray_get_length(oTokens), 
                        args);
}
/*--------------------------------------------------------------------*/
int execute_builtin_partial(DynArray_T toks, int start, int end,
                            enum BuiltinType btype, int in_child) {
    
    int argc = end - start;
    struct Token *t1;
    int ret;
    char *dir;

    switch (btype) {
    case B_EXIT:
        if (in_child) return 0;
        
        if (argc == 1) {
            dynarray_map(toks, free_token, NULL);
            dynarray_free(toks);
            exit(EXIT_SUCCESS);
        }
        else {
            error_print("exit does not take any parameters", FPRINTF);
            return -1;
        }

    case B_CD: {
        if (argc == 1) {
            dir = getenv("HOME");
            if (!dir) {
                error_print("cd: HOME variable not set", FPRINTF);
                return -1;
            }
        } 
        else if (argc == 2) {
            t1 = dynarray_get(toks, start + 1);
            if (t1 && t1->token_type == TOKEN_WORD) 
                dir = t1->token_value;
        } 
        else {
            error_print("cd: Too many parameters", FPRINTF);
            return -1;
        }

        ret = chdir(dir);
        if (ret < 0) {
            error_print(NULL, PERROR);
            return -1;
        }
        return 0;
    }

    default:
        error_print("Bug found in execute_builtin_partial", FPRINTF);
        return -1;
    }
}
/*--------------------------------------------------------------------*/
int execute_builtin(DynArray_T oTokens, enum BuiltinType btype) {
    return execute_builtin_partial(oTokens, 0, 
                                dynarray_get_length(oTokens), btype, FALSE);
}
/*--------------------------------------------------------------------*/
/* 
 * You need to finish implementing job related APIs. (find_job_by_jid(),
 * remove_pid_from_job(), delete_job()) in job.c to handle the job.
 * Feel free to modify the format of the job API according to your design.
 */
void wait_fg(int job_id) {
    pid_t pid;
    int status;

     // Find the job structure by job ID
    struct job *job = find_job_by_jid(job_id);
    if (!job) {
        fprintf(stderr, "Job: %d not found\n", job_id);
        return;
    }

    while (1) {
        pid = waitpid(-job->pgid, &status, 0);

        if (pid > 0) {
            // Remove the finished process from the job's pid list
            if (!remove_pid_from_job(job, pid)) {
                fprintf(stderr, "Pid %d not found in the job: %d list\n", 
                    pid, job->job_id);
            }

            if (job->remaining_processes == 0) break;
        }

        if (pid == 0) continue;

        if (pid < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
            error_print("Unknown error waitpid() in wait_fg()", PERROR);
        }
    }

    // Clean up job table entry if all processes are done
    if (job->remaining_processes == 0)
        delete_job(job->job_id);
}
/*--------------------------------------------------------------------*/
void print_job(int job_id, pid_t pgid) {
    fprintf(stdout, 
        "[%d] Process group: %d running in the background\n", job_id, pgid);
}
/*--------------------------------------------------------------------*/
int fork_exec(DynArray_T oTokens, int is_background) {
    /*
     * TODO: Implement fork_exec() in execute.c
     * To run a newly forked process in the foreground, call wait_fg() 
     * to wait for the process to finish.  
     * To run it in the background, call print_job() to print job id and
     * process group id.  
     * All terminated processes must be handled by sigchld_handler() in * snush.c. 
     */
    /* Process management */
    pid_t pid;                                 /* PID of forked child process */
    pid_t shell_pgid = 0;                      /* Shell's process group ID (for terminal control) */
    struct job *job = NULL;                    /* Job structure for this process */
    
    /* Synchronization */
    int sync_pipe[2];                          /* Pipe for parent-child synchronization */
    char sync_byte = 0;                        /* Byte for synchronization */
    
    /* Command execution */
    char *args[MAX_ARGS_CNT];                  /* Command arguments for execvp() */
    
    /* Block signals while setting up job */
    block_signal(SIGCHLD, TRUE);
    block_signal(SIGINT, TRUE);
    
    /* Create pipe for synchronization */
    if (pipe(sync_pipe) < 0) {
        error_print("pipe() failed for sync", PERROR);
        goto cleanup_signals;
    }
    
    /* Save shell's process group ID for foreground jobs */
    if (!is_background) {
        shell_pgid = getpgrp();
    }
    
    /* Fork child process */
    pid = fork();
    if (pid < 0) {
        error_print("fork() failed", PERROR);
        goto cleanup_pipe;
    }
    
    if (pid == 0) {
        /* Child process */
        close(sync_pipe[1]);
        
        /* Set process group ID to own PID */
        if (setpgid(0, 0) < 0) {
            error_print("setpgid() failed in child", PERROR);
            exit(EXIT_FAILURE);
        }
        
        /* Wait for parent to register job before executing */
        if (read(sync_pipe[0], &sync_byte, 1) < 0) {
            error_print("read() from sync pipe failed", PERROR);
            exit(EXIT_FAILURE);
        }
        close(sync_pipe[0]);
        
        /* Unblock signals in child before execvp */
        block_signal(SIGCHLD, FALSE);
        block_signal(SIGINT, FALSE);
        
        /* Build command arguments and handle redirections */
        build_command(oTokens, args);
        
        /* Execute the command */
        if (execvp(args[0], args) < 0) {
            error_print(args[0], PERROR);
            exit(EXIT_FAILURE);
        }
        /* Never returns */
    }
    
    /* Parent process */
    close(sync_pipe[0]);
    
    /* Allocate job before child starts executing */
    job = allocate_job(pid, 1, is_background ? BACKGROUND : FOREGROUND);
    if (job == NULL) {
        error_print("allocate_job() failed", FPRINTF);
        goto cleanup;
    }
    
    /* Set process group ID in parent (child's pgid = child's pid) */
    setpgid(pid, pid);  /* Ignore error - child may have already set it */
    
    /* Add pid to job */
    if (!add_pid_to_job(job, pid)) {
        error_print("add_pid_to_job() failed", FPRINTF);
        goto cleanup;
    }
    
    /* For foreground jobs, give terminal control to the job's process group */
    if (!is_background && tcsetpgrp(STDIN_FILENO, job->pgid) < 0) {
        error_print("tcsetpgrp() failed in parent", PERROR);
        goto cleanup;
    }
    
    /* Signal child that job registration is complete */
    if (write(sync_pipe[1], &sync_byte, 1) < 0) {
        error_print("write() to sync pipe failed", PERROR);
        goto cleanup;
    }
    close(sync_pipe[1]);
    
    /* Unblock signals */
    block_signal(SIGCHLD, FALSE);
    block_signal(SIGINT, FALSE);
    
    /* Save job_id before wait_fg() may delete the job */
    int job_id = job->job_id;
    
    /* Wait for foreground job or print background job */
    if (is_background) {
        print_job(job->job_id, job->pgid);
    } else {
        wait_fg(job->job_id);
        /* Restore terminal control back to shell after foreground job finishes */
        tcsetpgrp(STDIN_FILENO, shell_pgid);  /* Ignore error */
    }
    
    return job_id;
    
cleanup:
    /* Restore terminal control to shell if it was transferred */
    if (!is_background && shell_pgid != 0) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);  /* Ignore error */
    }
    
    kill(pid, SIGKILL);
    close(sync_pipe[1]);
    goto cleanup_signals;
    
cleanup_pipe:
    close(sync_pipe[0]);
    close(sync_pipe[1]);
    
cleanup_signals:
    block_signal(SIGCHLD, FALSE);
    block_signal(SIGINT, FALSE);
    return -1;
}
/*--------------------------------------------------------------------*/
int iter_pipe_fork_exec(int n_pipe, DynArray_T oTokens, int is_background) {
    /*
     * TODO: Implement iter_pipe_fork_exec() in execute.c
     * To run a newly forked process in the foreground, call wait_fg() 
     * to wait for the process to finish.  
     * To run it in the background, call print_job() to print job id and
     * process group id.  
     * All terminated processes must be handled by sigchld_handler() in * snush.c. 
     */
    /* Pipeline configuration */
    int n_processes = n_pipe + 1;              /* Number of processes in pipeline */
    int pipes[n_pipe][2];                      /* Pipes for inter-process communication */

    /* Process management */
    pid_t pids[n_processes];                   /* PIDs of all forked processes */
    pid_t first_pid = 0;                       /* PID of first process (becomes PGID) */
    pid_t shell_pgid = 0;                      /* Shell's process group ID (for terminal control) */
    struct job *job = NULL;                    /* Job structure for this pipeline */

    /* Synchronization */
    int sync_pipe[2];                          /* Pipe for parent-child synchronization */
    char sync_byte = 0;                        /* Byte for synchronization */

    /* Command execution */
    char *args[MAX_ARGS_CNT];                  /* Command arguments for execvp() */
    
    /* Built-in execution */
    enum BuiltinType btype;                    /* Built-in command type (cd, exit, or NORMAL) */
    int ret;                                   /* Return value from built-in execution */

    /* Token parsing */
    int token_len = dynarray_get_length(oTokens);
    int segment_starts[n_processes];           /* Start indices of each command segment */
    int segment_ends[n_processes];             /* End indices of each command segment */

    /* Loop variables */
    int i, j;
    
    /* Find segment boundaries by TOKEN_PIPE */
    segment_starts[0] = 0;
    for (i = 0, j = 0; i < token_len && j < n_pipe; i++) {
        struct Token *t = (struct Token *)dynarray_get(oTokens, i);
        if (t->token_type == TOKEN_PIPE) {
            segment_ends[j] = i;
            segment_starts[j + 1] = i + 1;
            j++;
        }
    }
    segment_ends[n_pipe] = token_len;
    
    /* Block signals while setting up job */
    block_signal(SIGCHLD, TRUE);
    block_signal(SIGINT, TRUE);
    
    /* Create sync pipe for synchronization */
    if (pipe(sync_pipe) < 0) {
        error_print("pipe() failed for sync", PERROR);
        goto cleanup_signals;
    }
    
    /* Create pipes for inter-process communication */
    for (i = 0; i < n_pipe; i++) {
        if (pipe(pipes[i]) < 0) {
            error_print("pipe() failed", PERROR);
            /* Close already created pipes */
            for (j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            close(sync_pipe[0]);
            close(sync_pipe[1]);
            goto cleanup_signals;
        }
    }
    
    /* Save shell's process group ID for foreground jobs */
    if (!is_background) {
        shell_pgid = getpgrp();
    }
    
    /* Fork all processes */
    for (i = 0; i < n_processes; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            error_print("fork() failed", PERROR);
            /* Kill already forked processes */
            for (j = 0; j < i; j++) {
                kill(pids[j], SIGKILL);
            }
            goto cleanup_pipes;
        }
        
        if (pid == 0) {
            /* Child process */
            close(sync_pipe[1]);
            
            /* Close all pipe ends that this process doesn't need */
            for (j = 0; j < n_pipe; j++) {
                if (j != i - 1) {
                    close(pipes[j][0]);
                }
                if (j != i) {
                    close(pipes[j][1]);
                }
            }
            
            /* Set up stdin/stdout redirection */
            if (i > 0) {
                /* Not the first process: connect stdin to previous pipe */
                dup2_e(pipes[i - 1][0], STDIN_FILENO, __func__, __LINE__);
                close(pipes[i - 1][0]);
            }
            
            if (i < n_pipe) {
                /* Not the last process: connect stdout to next pipe */
                dup2_e(pipes[i][1], STDOUT_FILENO, __func__, __LINE__);
                close(pipes[i][1]);
            }
            
            /* Set process group ID */
            if (i == 0) {
                /* First process: create new process group */
                if (setpgid(0, 0) < 0) {
                    error_print("setpgid() failed in first child", PERROR);
                    exit(EXIT_FAILURE);
                }
            } else {
                /* Other processes: join first process's group */
                if (setpgid(0, first_pid) < 0) {
                    error_print("setpgid() failed in child", PERROR);
                    exit(EXIT_FAILURE);
                }
            }
            
            /* Wait for parent to register job before executing */
            if (read(sync_pipe[0], &sync_byte, 1) < 0) {
                error_print("read() from sync pipe failed", PERROR);
                exit(EXIT_FAILURE);
            }
            close(sync_pipe[0]);
            
            /* Unblock signals in child before execvp */
            block_signal(SIGCHLD, FALSE);
            block_signal(SIGINT, FALSE);
            
            /* Check if this pipeline stage is a built-in (cd/exit in pipeline run in child) */
            btype = check_builtin(dynarray_get(oTokens, segment_starts[i]));
            if (btype != NORMAL) {
                ret = execute_builtin_partial(oTokens, segment_starts[i], segment_ends[i], btype, TRUE);
                if (ret < 0) {
                    error_print("Invalid return value "\
                        " of execute_builtin()", FPRINTF);
                    exit(EXIT_FAILURE);
                }
                exit(EXIT_SUCCESS);
            }

            /* Build command arguments for this segment */
            build_command_partial(oTokens, segment_starts[i], segment_ends[i], args);
            
            /* Execute the command (normal program, not built-in) */
            if (execvp(args[0], args) < 0) {
                error_print(args[0], PERROR);
                exit(EXIT_FAILURE);
            }
            /* Never returns */
        }
        
        /* Parent process */
        pids[i] = pid;
        if (i == 0) {
            first_pid = pid;
        }
    }
    
    /* Parent: close all pipe ends */
    for (i = 0; i < n_pipe; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    close(sync_pipe[0]);
    
    /* Allocate job before children start executing */
    job = allocate_job(first_pid, n_processes, is_background ? BACKGROUND : FOREGROUND);
    if (job == NULL) {
        error_print("allocate_job() failed", FPRINTF);
        goto cleanup;
    }
    
    /* Set process group ID for all processes */
    for (i = 0; i < n_processes; i++) {
        setpgid(pids[i], first_pid);  /* Ignore error - child may have already set it */
    }
    
    /* Add all pids to job */
    for (i = 0; i < n_processes; i++) {
        if (!add_pid_to_job(job, pids[i])) {
            error_print("add_pid_to_job() failed", FPRINTF);
            goto cleanup;
        }
    }
    
    /* For foreground jobs, give terminal control to the job's process group */
    if (!is_background && tcsetpgrp(STDIN_FILENO, job->pgid) < 0) {
        error_print("tcsetpgrp() failed in parent", PERROR);
        goto cleanup;
    }
    
    /* Signal all children that job registration is complete */
    for (i = 0; i < n_processes; i++) {
        if (write(sync_pipe[1], &sync_byte, 1) < 0) {
            error_print("write() to sync pipe failed", PERROR);
            goto cleanup;
        }
    }
    close(sync_pipe[1]);
    
    /* Unblock signals */
    block_signal(SIGCHLD, FALSE);
    block_signal(SIGINT, FALSE);
    
    /* Save job_id before wait_fg() may delete the job */
    int job_id = job->job_id;
    
    /* Wait for foreground job or print background job */
    if (is_background) {
        print_job(job->job_id, job->pgid);
    } else {
        wait_fg(job->job_id);
        /* Restore terminal control back to shell after foreground job finishes */
        tcsetpgrp(STDIN_FILENO, shell_pgid);  /* Ignore error */
    }
    
    return job_id;
    
cleanup:
    /* Restore terminal control to shell if it was transferred */
    if (!is_background && shell_pgid != 0) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);  /* Ignore error */
    }
    
    /* Kill all forked processes */
    for (i = 0; i < n_processes; i++) {
        kill(pids[i], SIGKILL);
    }
    close(sync_pipe[1]);
    goto cleanup_signals;
    
cleanup_pipes:
    for (i = 0; i < n_pipe; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    close(sync_pipe[0]);
    close(sync_pipe[1]);
    
cleanup_signals:
    block_signal(SIGCHLD, FALSE);
    block_signal(SIGINT, FALSE);
    return -1;
}
/*--------------------------------------------------------------------*/
