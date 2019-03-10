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

// todo
#define get_ncpus() 2

struct thread_pool* 
thpool_init(unsigned num, thpool_attr_t attr)
{
    struct thread_pool* tp = malloc(sizeof(*tp));


    if (!tp)
        return NULL;

    tp->attr = attr == NULL ? &__default_thpool_attr : attr;
    
    (void) pthread_mutex_init(&tp->mutex, NULL);

    tp->idle_threads = 0;
    tp->num_threads =  num == 0 ? get_ncpus() : num;
    tp->max_threads = tp->num_threads;
    tp->threads = thread_list_init(tp, tp->num_threads);

    job_list_init(&tp->job_list);

    return tp;
}

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

// no longer used by wait
#if 0
static void*
sync_run(thread_pool* pool, struct job* job)
{
    job->status = TPS_RUNNING;

    if ( job_list_pull(&pool->job_list, job) != job) {
        errno = EINVAL;
        return NULL;
    }

    current_job = job;

    void* ret = job->start_routine(job->arg);

    job_return(job, ret);

    return ret;
}
#endif

void* 
thpool_await(thread_pool* pool, thpool_future_t future)
{
    struct job* job = (struct job*) future;

    if (  pthread_mutex_trylock(&job->ret_mutex) ) {
        // TODO change this later to actually check errno, 
        // for now it should always be because of EBUSY
        assert(errno == EBUSY);
        
        return (void*) -1;
    }

    (void) pthread_mutex_lock(&job->mutex);

    if (job->status == TPS_WAITING) {
        if ( job_list_pull(&pool->job_list, job) != job) {
            errno = EINVAL;
            return (void*)-1;
        }

        job_list_push_front(&pool->job_list, job);
    }

    (void) pthread_mutex_unlock(&job->mutex);
        
    if (job->status != TPS_RETURNED || job->status != TPS_KILLED)
        (void) pthread_cond_wait(&job->returned, &job->ret_mutex);

    (void) pthread_mutex_unlock(&job->ret_mutex);

    return job->return_value;
}

tp_job_status_t 
thpool_thread_kill(thread_pool* _, thpool_id_t id)
{
    struct job* job = (struct job*) id;

    tp_job_status_t status = job->status;

    if (status != TPS_RUNNING)
        return status;

    (void) pthread_kill(job->thread_id, SIGUSR1);
    
    // its status gets changed by the SIGUSR1 signal handler
    // so we don't need to change it back here

    assert(job->status == TPS_KILLED);

    return status;
}

tp_job_status_t
thpool_thread_stop(thread_pool_t _, thpool_id_t id)
{
    struct job* job = id;

    tp_job_status_t status = job->status;

    if (status != TPS_RUNNING)
        return status;

    (void) pthread_kill(job->thread_id, SIGSTOP);

    job->status = TPS_STOPPED;

    return status;
}

tp_job_status_t
thpool_thread_cont(thread_pool_t _, thpool_id_t id)
{
    struct job* job = id;

    tp_job_status_t status = job->status;

    if (status != TPS_STOPPED) {
        errno = EINVAL;
        return status;
    }

    (void) pthread_kill(job->thread_id, SIGCONT);

    job->status = TPS_RUNNING;

    return status;
}

// takes a fake thread pool because the functions which call this one will have
// the job in the second register used for calling. Of course it is static and inline
// so the compiler will probably not be moving the job to the first register by calling
// convention, but it can anyway do that knowing that the first parameter is not used
static inline void 
job_kill(thread_pool_t _, struct job* job)
{
    assert(job->status == TPS_RUNNING);

    (void) pthread_kill(job->thread_id, SIGSTOP);

    job->status = TPS_KILLED;
}

tp_job_status_t 
thpool_job_kill(thread_pool_t pool, thpool_id_t id)
{
    struct job* job = id;

    tp_job_status_t status = job->status;

    if (status == TPS_WAITING)
        (void) job_list_pull(&pool->job_list, job);
    else if (status == TPS_RUNNING)
        job_kill(pool, job);
    
    return status;
}

void 
thjob_exit()
{
    (void) raise(SIGUSR1);

    (void) sched_yield();
 
    assert(! "Should never get here");
}

void 
tp_future_destroy(thread_pool_t pool, thpool_future_t future)
{
    struct job* job = future;

    if (job->status == TPS_RUNNING)
        job_kill(pool, job);

    job_destroy(job);
}
