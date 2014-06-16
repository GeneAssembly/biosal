
#include "coverage_distribution.h"

#include <helpers/actor_helper.h>
#include <helpers/vector_helper.h>
#include <helpers/message_helper.h>

#include <structures/map_iterator.h>
#include <structures/vector_iterator.h>

#include <stdio.h>
#include <stdint.h>

#include <inttypes.h>

struct bsal_script bsal_coverage_distribution_script = {
    .name = BSAL_COVERAGE_DISTRIBUTION_SCRIPT,
    .init = bsal_coverage_distribution_init,
    .destroy = bsal_coverage_distribution_destroy,
    .receive = bsal_coverage_distribution_receive,
    .size = sizeof(struct bsal_coverage_distribution)
};

void bsal_coverage_distribution_init(struct bsal_actor *self)
{
    struct bsal_coverage_distribution *concrete_actor;

    concrete_actor = (struct bsal_coverage_distribution *)bsal_actor_concrete_actor(self);

    bsal_map_init(&concrete_actor->distribution, sizeof(int), sizeof(uint64_t));

#ifdef BSAL_COVERAGE_DISTRIBUTION_DEBUG
    printf("DISTRIBUTION IS READY\n");
#endif
    concrete_actor->actual = 0;
    concrete_actor->expected = 0;
}

void bsal_coverage_distribution_destroy(struct bsal_actor *self)
{
    struct bsal_coverage_distribution *concrete_actor;

    concrete_actor = (struct bsal_coverage_distribution *)bsal_actor_concrete_actor(self);

    bsal_map_destroy(&concrete_actor->distribution);
}

void bsal_coverage_distribution_receive(struct bsal_actor *self, struct bsal_message *message)
{
    int tag;
    struct bsal_map map;
    struct bsal_map_iterator iterator;
    int *coverage_from_message;
    uint64_t *count_from_message;
    int *coverage;
    uint64_t *count;
    void *buffer;
    struct bsal_vector coverage_values;
    struct bsal_coverage_distribution *concrete_actor;
    int name;
    int source;

    name = bsal_actor_name(self);
    source = bsal_message_source(message);
    concrete_actor = (struct bsal_coverage_distribution *)bsal_actor_concrete_actor(self);
    tag = bsal_message_tag(message);
    buffer = bsal_message_buffer(message);

    if (tag == BSAL_PUSH_DATA) {

        bsal_map_unpack(&map, buffer);

        bsal_map_iterator_init(&iterator, &map);

        printf("distribution actor/%d receives coverage data from actor/%d, %d entries\n",
                        name, source, (int)bsal_map_size(&map));

        while (bsal_map_iterator_has_next(&iterator)) {

            bsal_map_iterator_next(&iterator, (void **)&coverage_from_message,
                            (void **)&count_from_message);

            printf("DEBUG DATA %d %d\n", (int)*coverage_from_message, (int)*count_from_message);

            count = bsal_map_get(&concrete_actor->distribution, coverage_from_message);

            if (count == NULL) {

                count = bsal_map_add(&concrete_actor->distribution, coverage_from_message);

                (*count) = 0;
            }

            (*count) += (*count_from_message);
        }

        bsal_map_iterator_destroy(&iterator);

        bsal_map_destroy(&map);

        bsal_actor_helper_send_reply_empty(self, BSAL_PUSH_DATA_REPLY);

        concrete_actor->actual++;

        if (concrete_actor->expected != 0 && concrete_actor->expected == concrete_actor->actual) {

            printf("received everything %d/%d\n", concrete_actor->actual, concrete_actor->expected);

            bsal_coverage_distribution_write_distribution(self);

            bsal_actor_helper_send_to_supervisor_empty(self, BSAL_SET_EXPECTED_MESSAGES_REPLY);
        }
    } else if (tag == BSAL_ACTOR_ASK_TO_STOP) {

        bsal_map_iterator_init(&iterator, &concrete_actor->distribution);

        bsal_vector_init(&coverage_values, sizeof(int));

        while (bsal_map_iterator_has_next(&iterator)) {

            bsal_map_iterator_next(&iterator, (void **)&coverage,
                            (void **)&count);

            bsal_vector_push_back(&coverage_values, coverage);
        }

        bsal_vector_helper_sort_int(&coverage_values);

        bsal_map_iterator_destroy(&iterator);

        bsal_vector_destroy(&coverage_values);

        bsal_actor_helper_ask_to_stop(self, message);

    } else if (tag == BSAL_SET_EXPECTED_MESSAGES) {

        bsal_message_helper_unpack_int(message, 0, &concrete_actor->expected);
    }
}

void bsal_coverage_distribution_write_distribution(struct bsal_actor *self)
{
    struct bsal_map_iterator iterator;
    int *coverage;
    uint64_t *count;
    struct bsal_coverage_distribution *concrete_actor;
    struct bsal_vector coverage_values;
    struct bsal_vector_iterator vector_iterator;

    concrete_actor = (struct bsal_coverage_distribution *)bsal_actor_concrete_actor(self);
    bsal_map_iterator_init(&iterator, &concrete_actor->distribution);
    bsal_vector_init(&coverage_values, sizeof(int));

    printf("map size %d\n", (int)bsal_map_size(&concrete_actor->distribution));

    while (bsal_map_iterator_has_next(&iterator)) {
        bsal_map_iterator_next(&iterator, (void **)&coverage, (void **)&count);

        bsal_vector_push_back(&coverage_values, coverage);
    }

    bsal_map_iterator_destroy(&iterator);
    bsal_vector_helper_sort_int(&coverage_values);

    bsal_vector_iterator_init(&vector_iterator, &coverage_values);

    printf("Coverage\tFrequency\n");

    while (bsal_vector_iterator_has_next(&vector_iterator)) {

        bsal_vector_iterator_next(&vector_iterator, (void **)coverage);

        count = (uint64_t *)bsal_map_get(&concrete_actor->distribution, coverage);

        printf("COVERAGE DISTRIBUTION %d\t%" PRIu64 "\n",
                        *coverage,
                        *count);
    }

    bsal_vector_destroy(&coverage_values);
    bsal_vector_iterator_destroy(&vector_iterator);
}


