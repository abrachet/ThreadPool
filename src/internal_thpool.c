#include "internal_thpool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <semaphore.h>

static void
delete_list(struct job_list_node* node)
{
    if (node == NULL)
        return;

    delete_list(node->next);
    job_destroy(node->job);
    free(node);
}

void 
job_list_destroy(struct job_list* list)
{
    pthread_mutex_destroy(list->mutex);
    pthread_cond_destroy(list->cv);
    delete_list(list->head);
}



void 
job_list_push(struct job_list* list, struct job* job)
{
    struct job_list_node* new = malloc(sizeof(*new));
    new->job  = job;
    new->next = 0;

    pthread_mutex_lock(&list->mutex);

    if (!list->back) {
        list->head = new;
        

        sem_post(&list->sem);
    } else {
        list->back->next = new;
    }

    pthread_mutex_unlock(&list->mutex);
}

struct job* 
job_list_pop(struct job_list* list)
{
    
}
