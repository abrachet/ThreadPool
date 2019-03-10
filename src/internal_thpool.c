/**
 * Alex Brachet-Mialot
 *
 * This file provides internal functions for the thread pool
 * These functions will eventually be almost all static but I am just
 * waiting until I figure the structure of the project out better before
 * It's not a namespace pollution issue as much as a small optimization
 */
#include "internal_thpool.h"
#include "thpool.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include <errno.h>
#include <assert.h>

// huge default of a half a second
#ifndef THPOOL_DEFAULT_TIMEOUT
#define THPOOL_DEFAULT_TIMEOUT 500
#endif

struct _opaque_job_attr_t __default_future_attr = {
    .return_needed = true,
    .free_returned = NULL,
    .unique_attr   = false
};

struct _opaque_job_attr_t __default_freeable_attr = {
    .return_needed = true,
    .free_returned = NULL,
    .unique_attr   = false
};

struct _opaque_thpool_attr_t __default_thpool_attr = {
    .timeout = THPOOL_DEFAULT_TIMEOUT
};


static void
delete_list(struct job_list_node* node)
{
    if (node == NULL)
        return;

    delete_list(node->next);
    job_destroy(node->job);
    (void) free(node);
}

int 
job_list_init(struct job_list* list)
{
    list->back = list->head = NULL;
    list->sem = 0;

    (void) pthread_cond_init(&list->cv, NULL);
    (void) pthread_mutex_init(&list->mutex, NULL);

    return 0;
}

void 
job_list_destroy(struct job_list* list)
{
    (void) pthread_mutex_destroy(&list->mutex);
    (void) pthread_cond_destroy(&list->cv);
    delete_list(list->head);
}

/// Assumes that the mutex has already been aquired
/// Does not try to aquire the mutex
static inline void
job_list_sem_post(struct job_list* list)
{
    // the deque was previously empty
    if (!list->sem)
        (void) pthread_cond_signal(&list->cv);

    list->sem++;
}

static int
job_list_sem_wait(struct job_list* list, unsigned milliseconds)
{
    if (!list->sem) {
        struct timespec ts;
        add_mili(&ts, milliseconds);
        int err = pthread_cond_timedwait(&list->cv, &list->mutex, &ts);
        if (err == -1) {
            /// TODO: Error handling back for the entire project

            return -1;
        }

        // if it is still 0, nothing was ever pushed
        if (!list->sem)
            return -1;
    } 
    
    list->sem--;
    return 0;
}

void 
job_list_push(struct job_list* list, struct job* job)
{
    struct job_list_node* new = malloc(sizeof(*new));
    new->job  = job;
    new->next = NULL;

    (void) pthread_mutex_lock(&list->mutex);

    job_list_sem_post(list);

    if (!list->back) {
        list->back = list->head = new;
    } else {
        list->back->next = new;
        list->back = new;
    }

    (void) pthread_mutex_unlock(&list->mutex);
}

void
job_list_push_front(struct job_list* list, struct job* job)
{
    struct job_list_node* new = malloc(sizeof(*new));
    new->job  = job;

    (void) pthread_mutex_lock(&list->mutex);

    job_list_sem_post(list);

    new->next = list->head;
    list->head = new;

    (void) pthread_mutex_unlock(&list->mutex);
}

struct job* 
job_list_pop(struct job_list* list, unsigned miliseconds)
{
    (void) pthread_mutex_lock(&list->mutex);

    if ( job_list_sem_wait(list, miliseconds) == -1 )
        return NULL;

    struct job_list_node* node = list->head;
    
    if (list->head == list->back)
        list->back = NULL;
    
    list->head = node->next;

    (void) pthread_mutex_unlock(&list->mutex);

    struct job* ret = node->job;

    (void) free(node);

    return ret;
}

struct job* 
job_list_pull(struct job_list* list, struct job* job)
{
    (void) pthread_mutex_lock(&list->mutex);

    if ( job_list_sem_wait(list, 1000) == -1 )
        return NULL;
    
    struct job_list_node* curr = list->head;
    for (; curr && curr->next && curr->next->job != job; curr = curr->next);

    if (!curr || curr->job != job)
        return NULL;

    curr->next = curr->next->next;

    free(curr);

    (void) pthread_mutex_unlock(&list->mutex);

    return job;
}



void 
job_init(struct job* job, void* (*start_routine) (void*), 
    void* arg, job_attr_t attr)
{
    (void) pthread_mutex_init(&job->mutex, JOB_MUTEX_ATTR);
    (void) pthread_mutex_init(&job->ret_mutex, NULL);

    job->start_routine = start_routine;
    job->arg = arg;

    job->thread_id = NULL;

    job->status = TPS_WAITING;

    job->attr = attr;

    (void) pthread_cond_init(&job->returned, JOB_COND_ATTR);
}


static inline void 
call_exit_funcs(struct job* job, void* ret)
{
    for (struct exit_stack* node = job->on_exit; node; ) {
        node->on_exit(ret, node->arg);
        struct exit_stack* temp = node;
        node = node->next;
        free(temp);
    }

}

void
job_return(struct job* job, void* ret)
{
    call_exit_funcs(job, ret);


    if ( !job_is_future(job) ) {
        job_destroy(job);
        return;
    }

    job->status = TPS_RETURNED;
    job->return_value = ret;

    (void) pthread_cond_signal(&job->returned);
}

void 
job_destroy(struct job* job)
{
    (void) pthread_mutex_destroy(&job->mutex);
    (void) pthread_cond_destroy(&job->returned);

    
    for (struct exit_stack* curr = job->on_exit; curr; ) {
        struct exit_stack* temp = curr;
        curr = curr->next;
        free(temp);
    }


    if (job->attr->free_returned)
        job->attr->free_returned(job->return_value);
    
    (void) free(job);
}

void 
add_mili(struct timespec* add, unsigned mili)
{
    add->tv_nsec += mili * 1000000;
}

void* worker(void*); 

/// TODO handle errors
/// TODO deal wtih pthread_attributes
struct thread_list* 
thread_list_init(struct thread_pool* tp, unsigned num)
{
    assert(num);

    struct thread_list* head = malloc(sizeof(*head));

    struct thread_list* curr = head;
    for (int i = 0; i < num; i++) {
        curr = curr->next = malloc(sizeof(*head));
        pthread_create(&curr->thread, NULL, &worker, tp);
    }
    
    return curr->next = head;
}

bool 
thpool_removing_threads(struct thread_pool* pool)
{
    if (pool->num_threads > pool->max_threads)
        return true;
    
    return (!pool->job_list.sem);
}

int 
job_register_on_exit(struct job* job, 
        void (*function)(void *, void *), void * arg)
{
    struct exit_stack* new = malloc(sizeof(struct exit_stack));
    if (!new)
        return ENOMEM;

    new->arg = arg;
    new->on_exit = function;
    new->next = job->on_exit;
    job->on_exit = new;

    return 0;
}


