#include "internal_thpool.h"
#include "thpool.h"
#include "tp_thread.h"

#include <setjmp.h>

// just a random number to know that the thread exited normall
#define NORMAL_EXIT ((void*)748)

static struct job* 
get_job(struct thread_pool* th)
{
    return NULL;
}

static void
cleanup_routine(void* arg)
{
    struct thread_pool* thpool = (struct thread_pool*) arg;

    // notify thread_pool to create another thread_pool if
    // the queue is larger than the number of threads
}


static _Thread_local jmp_buf thlocal_jmp;

static void 
thpool_kill_handler(int sig)
{
    longjmp(thlocal_jmp, 0);
}

static void*
worker(void* arg) 
{
    struct thread_pool* thpool = (struct thread_pool*) arg;

    thpool->num_threads++;

    pthread_cleanup_push(&cleanup_routine, thpool);

    (void) setjmp(thlocal_jmp);
    signal(SIGTHPKILL, &thpool_kill_handler);

    for (;;) {
        if (thpool_removing_threads(thpool))
            break;
           
        struct job* current_job = get_job(thpool);

        (void) pthread_mutex_lock(&current_job->mutex);
        current_job->return_value = current_job->start_routine(current_job->arg);

        (void) pthread_cond_signal(&current_job->returned);
        (void) pthread_mutex_unlock(&current_job->mutex);
    }

    pthread_cleanup_pop(true);

    return NORMAL_EXIT;
}
