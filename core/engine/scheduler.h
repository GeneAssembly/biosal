
#ifndef BSAL_SCHEDULER_H
#define BSAL_SCHEDULER_H

#include <core/structures/map.h>

struct bsal_worker_pool;
struct bsal_message;
struct bsal_actor;
struct bsal_migration;

#define BSAL_SCHEDULER_REDUCTIONS_PER_WORKER 1024

struct bsal_scheduler {
    struct bsal_worker_pool *pool;
    struct bsal_map actor_affinities;
    struct bsal_map last_actor_received_messages;

    int worker_for_work;

};

void bsal_scheduler_init(struct bsal_scheduler *scheduler, struct bsal_worker_pool *pool);
void bsal_scheduler_destroy(struct bsal_scheduler *scheduler);

void bsal_scheduler_balance(struct bsal_scheduler *scheduler);
void bsal_scheduler_migrate(struct bsal_scheduler *scheduler, struct bsal_migration *migration);

int bsal_scheduler_get_actor_production(struct bsal_scheduler *scheduler, struct bsal_actor *actor);
void bsal_scheduler_update_actor_production(struct bsal_scheduler *scheduler, struct bsal_actor *actor);
int bsal_scheduler_get_actor_worker(struct bsal_scheduler *scheduler, int name);
void bsal_scheduler_set_actor_worker(struct bsal_scheduler *scheduler, int name, int worker_index);

int bsal_scheduler_select_worker_least_busy(
                struct bsal_scheduler *scheduler, struct bsal_message *message, int *worker_score);

#endif
