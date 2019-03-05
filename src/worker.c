#include "internal_thpool.h"
#include "thpool.h"

#include <setjmp.h>
#include <stdio.h>
#include <signal.h>

static _Thread_local jmp_buf thlocal_jmp;
static _Thread_local struct job* current_job;

static struct job* 
get_job(struct thread_pool* th)
{
    struct job* job = job_list_pop(&th->job_list, 1000);
    if (!job)
        return NULL;
    
        
    printf("job->SR = %p\n", (void*)job->start_routine);
    return job;
}

static void
cleanup_routine(void* arg)
{
    struct thread_pool* thpool = (struct thread_pool*) arg;

    // notify thread_pool to create another thread_pool if
    // the queue is larger than the number of threads
    thpool->num_threads--;
}


static void 
thpool_kill_handler(int sig)
{
    longjmp(thlocal_jmp, 0);
}

int 
thjob_on_exit(void (*function)(void *, void *), void * arg) {
    return job_register_on_exit(current_job, function, arg);
}

int 
thjob_atexit(void (*function)(void)) {
    return thjob_on_exit((void(*)(void*, void*))function, 0);
}

thpool_future_t 
thjob_self()
{
    return current_job;
}

void 
thjob_exit()
{
    (void) raise(SIGUSR1);

    // spinlock until next context switch
    // I don't know a better way to do this yet
    for (;;);
}

void*
worker(void* arg) 
{
    struct thread_pool* thpool = (struct thread_pool*) arg;

    puts("Worker thread started");

    thpool->num_threads++;

    //pthread_cleanup_push(&cleanup_routine, thpool);

    (void) setjmp(thlocal_jmp);
    signal(SIGUSR1, &thpool_kill_handler);

    for (;;) {
        
        // thpool_removing_threads not ready yet
        #if 0
        if (thpool_removing_threads(thpool))
            break;
        #endif
           
        struct job* job = get_job(thpool);

        current_job = job;

        // if we got a NULL job then we timed out
        if (!job)
            return (void*)-1;

        
        (void) pthread_mutex_lock(&job->mutex);
        job->return_value = job->start_routine(job->arg);

        (void) pthread_cond_signal(&job->returned);
        (void) pthread_mutex_unlock(&job->mutex);
    }

    //pthread_cleanup_pop(true);

    //return NORMAL_EXIT;
    return NULL;
}
