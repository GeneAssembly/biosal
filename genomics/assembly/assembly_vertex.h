
#ifndef _BSAL_ASSEMBLY_VERTEX_H_
#define _BSAL_ASSEMBLY_VERTEX_H_

#include "assembly_connectivity.h"

#include <genomics/data/dna_codec.h>

typedef int coverage_t;

#define BSAL_VERTEX_STATE_UNUSED 0
#define BSAL_VERTEX_STATE_USED 1
#define BSAL_VERTEX_STATE_TIP 2
#define BSAL_VERTEX_STATE_BUBBLE 3

/*
 * Attributes of an assembly vertex
 */
struct bsal_assembly_vertex {

    int coverage_depth;
    char state;
    int first_actor;

    /*
     * Connectivity.
     */
    struct bsal_assembly_connectivity connectivity;
};

void bsal_assembly_vertex_init(struct bsal_assembly_vertex *self);
void bsal_assembly_vertex_init_copy(struct bsal_assembly_vertex *self,
                struct bsal_assembly_vertex *vertex);
void bsal_assembly_vertex_destroy(struct bsal_assembly_vertex *self);

int bsal_assembly_vertex_coverage_depth(struct bsal_assembly_vertex *self);
void bsal_assembly_vertex_increase_coverage_depth(struct bsal_assembly_vertex *self, int value);

int bsal_assembly_vertex_child_count(struct bsal_assembly_vertex *self);
void bsal_assembly_vertex_add_child(struct bsal_assembly_vertex *self, int symbol_code);
void bsal_assembly_vertex_delete_child(struct bsal_assembly_vertex *self, int symbol_code);
int bsal_assembly_vertex_get_child(struct bsal_assembly_vertex *self, int index);

int bsal_assembly_vertex_parent_count(struct bsal_assembly_vertex *self);
void bsal_assembly_vertex_add_parent(struct bsal_assembly_vertex *self, int symbol_code);
void bsal_assembly_vertex_delete_parent(struct bsal_assembly_vertex *self, int symbol_code);
int bsal_assembly_vertex_get_parent(struct bsal_assembly_vertex *self, int index);

void bsal_assembly_vertex_print(struct bsal_assembly_vertex *self);

int bsal_assembly_vertex_pack_size(struct bsal_assembly_vertex *self);
int bsal_assembly_vertex_pack(struct bsal_assembly_vertex *self, void *buffer);
int bsal_assembly_vertex_unpack(struct bsal_assembly_vertex *self, void *buffer);
int bsal_assembly_vertex_pack_unpack(struct bsal_assembly_vertex *self, int operation, void *buffer);

void bsal_assembly_vertex_invert_arcs(struct bsal_assembly_vertex *self);

void bsal_assembly_vertex_set_state(struct bsal_assembly_vertex *self, int state);
int bsal_assembly_vertex_state(struct bsal_assembly_vertex *self);

void bsal_assembly_vertex_set_first_actor(struct bsal_assembly_vertex *self, int first_actor);
int bsal_assembly_vertex_first_actor(struct bsal_assembly_vertex *self);

#endif
