/**
 * @file thpool.h
 * @author Alex Brachet (abrachet@purdue.edu)
 * @brief Public interface for the Thread Pool
 * @version 0.1
 * @date 2019-02-05
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#pragma once

/// This is just used to show that attributes for example can be NULL
#if __has_feature(nullablity)
    #define _Nullable nullable
#else
    #define _Nullable
#endif


#if __STDC_VERSION__ < 201112L
    #define _Noreturn 
#endif


#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef void* thpool_id_t;
typedef void* thpool_future_t;

typedef struct _opaque_job_attr_t {
    bool return_needed;             ///< If true, once the job returns the job should not be destroyed
                                    ///< true signifies that this is a future, and that the caller wants it to exist
    void (*free_returned)(void*);    ///< If not null, use this function to free the returned value
                                    ///< No clean way to get this to work with munmap, unfortunately
    bool unique_attr;               ///< If unique the struct gets free(3)'d  when a job ends
} *job_attr_t;


typedef struct _opaque_thpool_attr_t {
    uint32_t    timeout;        ///< How long the queue should wait for new jobs before removing excess threads
} *thpool_attr_t;

/// status of a job
typedef enum {
    TPS_NOEXIST = -2,   ///< Returned by thp_thread_status on err
    TPS_EINVAL,

    TPS_WAITING = 1,    ///< Waiting to be run
    TPS_RUNNING,        ///< Currently being run
    TPS_KILLED,         ///< Killed
    TPS_RETURNED,       ///< Job finished
    TPS_STOPPED,        ///< Hit SIGSTOP, can be rerun with SIGCONT
} tp_job_status_t;

struct thread_pool;
typedef struct thread_pool* thread_pool_t;

/**
 * @brief creates a thread_pool with a specified amount of threads
 * if 0 is passed, the thread_pool will be created with the 
 * number of CPU's if it can be found, or will exit otherwise
 * 
 * @param num number of threads to create for the pool
 * @param attr attributes. NULL will use normal attributes
 * 
 * @return thread_pool* the thread_pool
 */
thread_pool_t thpool_init(unsigned num, thpool_attr_t attr _Nullable);

/**
 * @brief enqueues a job with the thread_pool
 * 
 * @param thpool the thread_pool to queue a job with
 * @param start_routine function to call
 * @param arg argument to pass
 * 
 */
thpool_id_t thpool_queue(thread_pool_t thpool, void* (*start_routine)(void*), void* arg, job_attr_t attr _Nullable);


/**
 * @brief Same as thpool_queue except returns a future, this just means that the job will never be freed 
 * by the thread pool but must be freed by the caller. This function is [[nodiscard]]
 * 
 * @param thpool the thread_pool to queue a job with
 * @param start_routine function to call
 * @param arg argument to pass
 * @param attr NULL will use basic job_attributes for a future. Passing attributes not suited for a future will set errno 
 * 
 * @return thpool_id_t pointer to future
 */
thpool_future_t thpool_async(thread_pool_t thpool, void* (*start_routine)(void*), void* arg, job_attr_t attr _Nullable);

/**
 * @brief passive blocking wait on all jobs to finish and for the queue to be empty
 * 
 * @param pool the thread_pool to wait on
 */
void thpool_wait(thread_pool_t pool);

/**
 * @brief blocking call waiting for all workers to exit, 
 * doesn't enqeue new jobs. Has the same effect as thpool_destroy_now 
 * if the pool has no active jobs and future jobs, for example after thpool_wait()
 * 
 * @param pool the thread_pool to destroy
 */
void thpool_destroy(thread_pool_t pool);

/**
 * @brief cleans up the thread_pools resources immediately, all currently
 * running jobs are killed. It is not safe to use pool after destruction
 * 
 * @param pool thread_pool to destroy
 */
void thpool_destroy_now(thread_pool_t pool);

/**
 * @brief blocking wait call on a specific job. If another tread is already waiting on this
 * job then errno will be set to EBUSY and -1 will be returned. 
 * 
 * @param pool the thread_pool which the job belongs to
 * @param future the job to wait on
 * 
 * @return void* value returned by the job
 */
void* thpool_await(thread_pool_t pool, thpool_future_t future);

/**
 * @brief changes the number of threads of the thread pool
 * can be given fewer threads than previously to remove threads
 * these threads will not be terminated immediately
 * but will wait until they finish their current job
 * 
 * @param pool thread_pool whos underlying number of threads should be modified
 * @param new_num number of threads the pool should have
 * @return int new number of threads of the pool, -1 on error
 */
int change_num_threads(thread_pool_t pool, int new_num);

/**
 * @brief gives the status of a worker thread. Returns tp_job_status_t::TPS_NOEXIST and sets errno
 * when the id does not reference an existing thread of that thread_pool
 * 
 * @param pool pool where the worker resides
 * @param id id of the thread, one from thpool_queue()
 * 
 * @return tp_job_status_t status enum of the job
 */
tp_job_status_t thpool_thread_status(thread_pool_t pool, thpool_id_t id);

/**
 * @brief kills a job. If the job has already finished the return will be TPS_RETURNED
 * 
 * @param pool pool the job is running on
 * @param id job id
 * @return tp_job_status_t previous status of the job after thpool_job_kill
 */
tp_job_status_t thpool_job_kill(thread_pool_t pool, thpool_id_t id);

/**
 * @brief stops the thread the job is being run on 
 * 
 * @param pool pool where the worker resides
 * @param id id of the thread to send SIGSTOP
 * 
 * @return tp_job_status_t previous status of the thread
 */
tp_job_status_t thpool_thread_stop(thread_pool_t pool, thpool_id_t id);

/**
 * @brief continues a stopped thread
 * 
 * @param pool pool which the job is running on
 * @param id job id
 * @return tp_job_status_t previous state of the thread, 
 * or TPS_EINVAL if the thread was not previously stopped
 */
tp_job_status_t thpool_thread_cont(thread_pool_t pool, thpool_id_t id);

/**
 * @brief releases a futures memory, using the future after destruction is undefinde
 * 
 * @param pool the pool the job was run on
 * @param future what will be destroyed
 */
void tp_future_destroy(thread_pool_t pool, thpool_future_t future);

/**
 * @brief Register a function to be called on exit of a job.
 * Similar to on_exit(3). Different is the first parameter of the
 * registered function which is void * instead of int, as jobs return 
 * void *
 * 
 * @param function function to be called on exit
 * @param arg argument to pass to function
 * 
 * @return returns 0 on success
 */
int thjob_on_exit(void (*function)(void *, void *), void * arg);

/**
 * @brief registers a function to be called on job termination.
 * uses the same stack as /thjob_on_exit. functions are called in reverse order
 * of their registration
 * 
 * @param function function to be called on exit
 * @return int 0 on success
 */
int thjob_atexit(void (*function)(void));

/**
 * @brief get calling jobs future
 * 
 * @return thpool_future_t 
 */
thpool_future_t thjob_self();

/**
 * @brief exits the current job, should only be called from a function
 * that would be run as a job by the thread pool. Otherwise the behavior is 
 * undefined
 * 
 * @return cannot return
 */
_Noreturn void thjob_exit();
