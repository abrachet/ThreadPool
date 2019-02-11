/**
 * @file thpool.h
 * @author Alex Brachet (abrahcet@purdue.edu)
 * @brief 
 * @version 0.1
 * @date 2019-02-05
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#pragma once

#include <stdint.h>

typedef int thpool_id_t;
typedef void* thpool_future_t;

typedef struct job_attr_t {
    bool return_needed;     ///< If true, once the job returns the job should not be destroyed
                            ///< true signifies that this is a future, and that the caller wants it to exist
} job_attr_t;

typedef struct thpool_attr_t {
    uint32_t    timeout;        ///< How long the queue should wait for new jobs before removing excess threads
} thpool_attr_t;

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
typedef struct thread_pool thread_pool;

/**
 * @brief creates a thread_pool with a specified amount of threads
 * if 0 is passed, the thread_pool will be created with the 
 * number of CPU's if it can be found, or will exit otherwise
 * 
 * @param num number of threads to create for the pool
 * 
 * @return thread_pool* the thread_pool
 */
thread_pool* thpool_init(unsigned num);

/**
 * @brief enqueues a job with the thread_pool
 * 
 * @param thpool the thread_pool to queue a job with
 * @param start_routine function to call
 * @param arg argument to pass
 * 
 */
void thpool_queue(thread_pool* thpool, void* (*start_routine)(void*), void* arg);


/**
 * @brief Same as thpool_queue except returns a future, this just means that the job will never be freed 
 * by the thread pool but must be freed by the caller. This function is [[nodiscard]]
 * 
 * @param thpool the thread_pool to queue a job with
 * @param start_routine function to call
 * @param arg argument to pass
 * @return thpool_id_t pointer to future
 */
thpool_future_t thpool_async(thread_pool* thpool, void* (*start_routine)(void*), void* arg);

/**
 * @brief blocking wait on all jobs
 * 
 * @param pool the thread_pool to wait for all processes to complete
 */
void thpool_wait(thread_pool* pool);

/**
 * @brief blocking call waiting for all workers to exit, 
 * doesn't enqeue new jobs. Has the same effect as thpool_destroy_now 
 * if the pool has no active jobs and future jobs, for example after thpool_wait()
 * 
 * @param pool the thread_pool to destroy
 */
void thpool_destroy(thread_pool* pool);

/**
 * @brief cleans up the thread_pools resources immediately, all currently
 * running jobs are killed. It is not safe to use pool after destruction
 * 
 * @param pool thread_pool to destroy
 */
void thpool_destroy_now(thread_pool* pool);

/**
 * @brief blocking wait call on a specific job
 * 
 * @param pool the thread_pool which the job belongs to
 * @param id id of the job from that pool
 * 
 * @return void* value returned by the job
 */
void* thpool_await(thread_pool* pool, thpool_id_t id);


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
int change_num_threads(thread_pool* pool, int new_num);

/**
 * @brief gives the status of a worker thread. Returns tp_job_status_t::TPS_NOEXIST and sets errno
 * when the id does not reference an existing thread of that thread_pool
 * 
 * @param pool pool where the worker resides
 * @param id id of the thread, one from thpool_queue()
 * 
 * @return tp_job_status_t status enum of the job
 */
tp_job_status_t thp_thread_status(thread_pool* pool, thpool_id_t id);

/**
 * @brief 
 * 
 * @param pool pool where the worker resides
 * @param id id of the thread to send SIGSTOP
 * @return tp_job_status_t previous status of the thread
 */
tp_job_status_t thp_thread_stop(thread_pool* pool, thpool_id_t id);
