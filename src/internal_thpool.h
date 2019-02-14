#pragma once

#include <pthread.h>
#include <semaphore.h>
// from my testing it looks like at O0 clang produces better code
// with atomics. O2 looks the same as gcc. But still, interesting to note
#include <stdatomic.h> // _Atomic typedef types

#include <stdbool.h>

#include "thpool.h"

#define NORMAL_EXIT ((void*)748)

extern struct _opaque_job_attr_t    __default_future_attr;
extern struct _opaque_job_attr_t    __default_freeable_attr;
extern struct _opaque_thpool_attr_t __default_thpool_attr;

/**
 * @brief describes a job, exists in its thread_pools::job_vec for the lifetime of the 
 * threadpool
 */
struct job {
    pthread_mutex_t     mutex;          ///< protects entire job

    void* (*start_routine) (void*);     ///< Start routine of the job
    void*               arg;            ///< Argument to pass to start_routine
    
    pthread_t           thread_id;      ///< pthread_t of the thread it is currently running on, NULL if not running
    tp_job_status_t     status;         ///< Jobs current status

    job_attr_t          attr;           ///< Attributes

    pthread_mutex_t     ret_mutex;      ///< Mutex used with the cond variable. 
                                        ///< This doesn't protect the entire mutex,
                                        ///< only used for the condition variable
    pthread_cond_t      returned;       ///< Signaled on jobs exit, successful or otherwise
    void*               return_value;   ///< Value returned from pthread_exit(), tp_thread_exit(), or normal return
};


struct job_list_node {
    struct job_list_node*   next;
    struct job*             job;  
};


/**
 * @brief deque of jobs
 * 
 * I have to implement my own semaphore 
 */
struct job_list {
    pthread_mutex_t       mutex;    ///< Protects insertions and deletions
    pthread_cond_t        cv;       ///<
    atomic_uint           sem;      ///< 
    struct job_list_node* head;     ///< Queue of jobs
    struct job_list_node* back;     ///< Pointer to the last element
};

/**
 * @brief circularly linked list
 * 
 */
struct thread_list {
    struct thread_list* next;    
    pthread_t           thread;
};

/**
 * @brief Creates a list of size num
 * 
 * @param tp thread_pool pointer passed to the worker threads
 * @param num number of nodes to create
 * 
 * @return struct thread_list* head of the list
 */
struct thread_list* thread_list_init(struct thread_pool* tp, unsigned num);

/**
 * @brief
 * 
 */
typedef struct thread_pool {
    pthread_mutex_t         mutex;          ///< Undecided if this will be used

    struct thread_list*     threads;        ///< Threads owned by the thread_pool

    thpool_attr_t           attr;           ///< Pools Attributes

    atomic_uint             num_threads;    ///< Current number of threads (can in some cases be larger than max_threads).
                                            ///< This happens in special cases only like when SIGSTOP is signaled and
                                            ///< 'IMDCONT' is defined
    unsigned                max_threads;    ///< Maximum number allowed from thpool_init
    unsigned                idle_threads;   ///< Number of idle threads not currently executing jobs

    struct job_list         job_list;       ///< Queue of jobs
} thread_pool;

///////////////////////////////////////////////
///////////////////////////////////////////////
///////////// struct job routines /////////////
///////////////////////////////////////////////
///////////////////////////////////////////////

#define JOB_MUTEX_ATTR NULL
#define JOB_COND_ATTR  NULL

#define job_is_future(job) (job)->attr->return_needed

/**
 * @brief Creates a new struct job
 * 
 * @param job Pointer to job struct who's fields should be initialized
 */

/**
 * @brief 
 * 
 * @param job job to construct
 * @param start_routine 
 * @param arg 
 * @param attr attributes
 */
void job_init(struct job* job, void* (*start_routine) (void*), void* arg, job_attr_t attr);

/**
 * @brief signals the condition variable that it has returned, sets its status accordingly
 * and assigns its return value
 * 
 * @param job the finishing job
 * @param ret return value of the job
 */
void job_return(struct job* job, void* ret);

/**
 * @brief destroys a job and releases its resources if possible. It is considered possible
 * if the job is not a future. job_destroy() does not consider wether or not the 
 * job has already been run. That is the job of the caller.
 * 
 * @param job job to destroy
 */
void job_destroy(struct job* job);

///////////////////////////////////////////////
///////////////////////////////////////////////
////////// struct job_list routines ///////////
///////////////////////////////////////////////
///////////////////////////////////////////////

/**
 * @brief Creates the job_list
 * 
 * @param list pointer to the list who's values should be initialized
 */
#define job_list_init(list)             \
do {                                    \
    sem_init(&(list)->sem, true, false);\
    (list)->head = NULL;                \
} while(0)


/**
 * @brief Free's all job's in the list. Should only be called when the pool
 * is getting destroyed
 * 
 * @param vec vector who's resources should be free'd
 */
void job_list_destroy(struct job_list* list);

/**
 * @brief pushes a job to the list
 * 
 * @param list 
 * @param job 
 */
void job_list_push(struct job_list* list, struct job* job);

/**
 * @brief returns the next job in the queue, or NULL if the request has timed out.
 * The wait time 
 * 
 * @param list list to pop from
 * @param miliseconds miliseconds to wait for
 * @return struct job* the job from the list
 */
struct job* job_list_pop(struct job_list* list, unsigned miliseconds);


/// probably should just make this a macro
/**
 * @brief add's mili miliseconds to add from the current time
 * 
 * @param add timespec to be created
 * @param mili number of miliseconds from the current system time
 */
void add_mili(struct timespec* add, unsigned mili);
