
#ifndef BSAL_COVERAGE_DISTRIBUTION_H
#define BSAL_COVERAGE_DISTRIBUTION_H

#include <core/engine/actor.h>

#include <core/structures/map.h>

#define BSAL_COVERAGE_DISTRIBUTION_SCRIPT 0xfdec2b1e

struct bsal_coverage_distribution {
    struct bsal_map distribution;
    int expected;
    int actual;
};

#define BSAL_PUSH_DATA 0x00005c27
#define BSAL_PUSH_DATA_REPLY 0x00004874
#define BSAL_SET_EXPECTED_MESSAGES 0x00004878
#define BSAL_SET_EXPECTED_MESSAGES_REPLY 0x00007e2f

#define BSAL_COVERAGE_DISTRIBUTION_DEFAULT_OUTPUT "coverage_distribution.txt"

extern struct bsal_script bsal_coverage_distribution_script;

void bsal_coverage_distribution_init(struct bsal_actor *actor);
void bsal_coverage_distribution_destroy(struct bsal_actor *actor);
void bsal_coverage_distribution_receive(struct bsal_actor *actor, struct bsal_message *message);

void bsal_coverage_distribution_write_distribution(struct bsal_actor *self);

#endif
