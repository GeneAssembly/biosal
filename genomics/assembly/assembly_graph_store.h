
#ifndef BSAL_ASSEMBLY_GRAPH_STORE_H
#define BSAL_ASSEMBLY_GRAPH_STORE_H

#include <engine/thorium/actor.h>

#include <genomics/data/dna_codec.h>

#include <genomics/data/coverage_distribution.h>

#include <core/structures/map_iterator.h>
#include <core/structures/map.h>

#include <core/system/memory_pool.h>

#define BSAL_ASSEMBLY_GRAPH_STORE_SCRIPT 0xc81a1596

/*
 * For ephemeral storage, see
 * http://docs.openstack.org/openstack-ops/content/storage_decision.html
 */
struct bsal_assembly_graph_store {
    struct bsal_map table;
    struct bsal_dna_codec transport_codec;
    struct bsal_dna_codec storage_codec;
    int kmer_length;
    int key_length_in_bytes;

    int customer;

    uint64_t received;
    uint64_t last_received;

    struct bsal_memory_pool persistent_memory;

    struct bsal_map coverage_distribution;
    struct bsal_map_iterator iterator;
    int source;
};

extern struct bsal_script bsal_assembly_graph_store_script;

void bsal_assembly_graph_store_init(struct bsal_actor *actor);
void bsal_assembly_graph_store_destroy(struct bsal_actor *actor);
void bsal_assembly_graph_store_receive(struct bsal_actor *actor, struct bsal_message *message);

void bsal_assembly_graph_store_print(struct bsal_actor *self);
void bsal_assembly_graph_store_push_data(struct bsal_actor *self, struct bsal_message *message);
void bsal_assembly_graph_store_yield_reply(struct bsal_actor *self, struct bsal_message *message);

#endif
