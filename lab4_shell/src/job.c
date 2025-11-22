/*--------------------------------------------------------------------*/
/* job.c */
/* Author: Jongki Park, Kyoungsoo Park */
/*--------------------------------------------------------------------*/

#include "job.h"

extern struct job_manager *manager;
/*--------------------------------------------------------------------*/
void init_job_manager() {
    manager = (struct job_manager *)calloc(1, sizeof(struct job_manager));
    if (manager == NULL) {
        fprintf(stderr, "[Error] job manager allocation failed\n");
        exit(EXIT_FAILURE);
    }

    /*
     * TODO: Init job manager
     */
    manager->n_jobs = 0;
    manager->next_job_id = 1;
    manager->jobs = (struct job *)calloc(MAX_JOBS, sizeof(struct job));
    if (manager->jobs == NULL) {
        fprintf(stderr, "[Error] jobs array allocation failed\n");
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
/* This is just a placeholder for compilation. You can modify it if you want. */
struct job *find_job_by_jid(int job_id) {
    /*
     * TODO: Implement find_job_by_jid()
     */
    int i;
    
    if (manager == NULL || manager->jobs == NULL)
        return NULL;
    
    for (i = 0; i < manager->n_jobs; i++) {
        if (manager->jobs[i].job_id == job_id) {
            return &manager->jobs[i];
        }
    }
    
    return NULL;
}
/*--------------------------------------------------------------------*/
int remove_pid_from_job(struct job *job, pid_t pid) {

    /*
     * TODO: Implement remove_pid_from_job()
    */
    int i;
    
    if (job == NULL || job->pids == NULL)
        return 0;
    
    /* Find the pid in the array */
    for (i = 0; i < job->remaining_processes; i++) {
        if (job->pids[i] == pid) {
            /* Remove pid by shifting remaining elements */
            for (; i < job->remaining_processes - 1; i++) {
                job->pids[i] = job->pids[i + 1];
            }
            job->remaining_processes--;
            return 1;
        }
    }
    
    return 0;
}
/*--------------------------------------------------------------------*/
int delete_job(int jobid) {
    
    /*
     * TODO: Implement delete_job()
     */
    int i, found = 0;
    struct job *job;
    
    if (manager == NULL || manager->jobs == NULL)
        return 0;
    
    /* Find the job */
    for (i = 0; i < manager->n_jobs; i++) {
        if (manager->jobs[i].job_id == jobid) {
            found = 1;
            job = &manager->jobs[i];
            break;
        }
    }
    
    if (!found)
        return 0;
    
    /* Free the pid array if it exists */
    if (job->pids != NULL) {
        free(job->pids);
        job->pids = NULL;
    }
    
    /* Shift remaining jobs to fill the gap */
    for (; i < manager->n_jobs - 1; i++) {
        /* Copy the job structure (this copies the pids pointer) */
        manager->jobs[i] = manager->jobs[i + 1];
    }
    
    /* Clear the last job slot */
    memset(&manager->jobs[manager->n_jobs - 1], 0, sizeof(struct job));
    
    manager->n_jobs--;
    
    return 1;
}
/*--------------------------------------------------------------------*/
/*
 * TODO: Implement any necessary job-control code in job.c 
 */

/*--------------------------------------------------------------------*/
/* Allocate a new job and return a pointer to it. Returns NULL on failure. */
struct job *allocate_job(pid_t pgid, int n_processes, job_state state) {
    struct job *job;
    
    if (manager == NULL || manager->jobs == NULL)
        return NULL;
    
    if (manager->n_jobs >= MAX_JOBS)
        return NULL;
    
    /* Use n_jobs as the index since delete_job() shifts jobs to fill gaps */
    job = &manager->jobs[manager->n_jobs];
    
    /* Initialize the job */
    job->job_id = manager->next_job_id++;
    job->pgid = pgid;
    job->remaining_processes = 0;
    job->total_processes = n_processes;
    job->state = state;
    
    /* Allocate pid array based on n_processes */
    job->pids = (pid_t *)calloc(n_processes, sizeof(pid_t));
    if (job->pids == NULL) {
        return NULL;
    }
    
    manager->n_jobs++;
    
    return job;
}

/*--------------------------------------------------------------------*/
/* Add a pid to a job's pid list. Returns 1 on success, 0 on failure. */
int add_pid_to_job(struct job *job, pid_t pid) {
    if (job == NULL || job->pids == NULL)
        return 0;
    
    if (job->remaining_processes >= job->total_processes)
        return 0;
    
    job->pids[job->remaining_processes++] = pid;
    return 1;
}

/*--------------------------------------------------------------------*/
/* Find a job containing the given PID. Returns NULL if not found. */
struct job *find_job_by_pid(pid_t pid) {
    int i, j;
    
    if (manager == NULL || manager->jobs == NULL)
        return NULL;
    
    for (i = 0; i < manager->n_jobs; i++) {
        for (j = 0; j < manager->jobs[i].remaining_processes; j++) {
            if (manager->jobs[i].pids[j] == pid) {
                return &manager->jobs[i];
            }
        }
    }
    
    return NULL;
}

/*--------------------------------------------------------------------*/
/* Find the foreground job. Returns NULL if no foreground job exists. */
struct job *find_foreground_job(void) {
    int i;
    
    if (manager == NULL || manager->jobs == NULL)
        return NULL;
    
    for (i = 0; i < manager->n_jobs; i++) {
        if (manager->jobs[i].state == FOREGROUND) {
            return &manager->jobs[i];
        }
    }
    
    return NULL;
}
