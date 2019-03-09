/**
 * @file thpool.c
 * @author Alex Brachet-Mialot (abrachet@purdue.edu)
 * @brief Implementations of the public facing routines for the thread pool
 * @version 0.1
 * @date 2019-02-12
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#include "thpool.h"
#include "internal_thpool.h"

#include <stdlib.h>

#include <signal.h>

#include <sched.h>

#include <assert.h>
#include <errno.h>

static struct job*
thpool_do_queue(thread_pool* _Nonnull thpool, void* (* _Nonnull start_routine)(void*), 
        void* arg, job_attr_t attr _Nullable)
{
    struct job* job = malloc(sizeof(struct job));

    job_init(job, start_routine, arg, attr);

    job_list_push(&thpool->job_list, job);

    return job;
}

thpool_id_t
thpool_queue(thread_pool* thpool, void* (* start_routine)(void*), 
        void* arg, job_attr_t attr _Nullable)
{
    if (!thpool || !start_routine) {
        errno = EINVAL;
        return NULL;
    }

    if (!attr) 
        attr = &__default_freeable_attr;
    
    return thpool_do_queue(thpool, start_routine, arg, attr);
}

thpool_future_t 
thpool_async(thread_pool* thpool, void* (*start_routine)(void*), 
        void* arg, job_attr_t attr _Nullable)
{
    if (!thpool || !start_routine) {
        errno = EINVAL;
        return NULL;
    }

    if (!attr)
        attr = &__default_future_attr;

    return (void*) thpool_do_queue(thpool, start_routine, arg, attr);
}

tp_job_status_t 
thp_thread_status(thread_pool* pool, thpool_id_t id)
{
    return ((struct job*)id)->status;
}

static void*
sync_run(thread_pool* pool, struct job* job)
{
    if (!job_list_pull(&pool->job_list, job))
        return (void*)-1;


    void* ret = job->start_routine(job->arg);

    job_return(job, ret);

    return ret;
}

void* 
thpool_await(thread_pool* pool, thpool_future_t future)
{
    struct job* job = (struct job*) future;

    int error;
    if (  (error = pthread_mutex_trylock(&job->ret_mutex)) ) {
        // TODO change this later to actually check errno, 
        // for now it should always be because of EBUSY
        assert(errno == EBUSY);
        
        return (void*) -1;
    } 

    if (job->status == TPS_WAITING)
        return sync_run(pool, job);

    if (job->status != TPS_RETURNED)
        (void) pthread_cond_wait(&job->returned, &job->ret_mutex);

    // it should have called job_return which should have set this
    //assert(job->status == TPS_RETURNED);

    return job->return_value;
}

tp_job_status_t 
thp_thread_stop(thread_pool* pool, thpool_id_t id)
{
    struct job* job = (struct job*) id;

    tp_job_status_t status = job->status;

    (void) pthread_kill(job->thread_id, SIGUSR1);
    
    // its status gets changed by the SIGUSR1 signal handler
    // so we don't need to change it back here

    assert(job->status == TPS_KILLED);

    return status;
}

void 
thjob_exit()
{
    (void) raise(SIGUSR1);

    (void) sched_yield();
}
