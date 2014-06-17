
#ifndef BSAL_DNA_SEQUENCE_H
#define BSAL_DNA_SEQUENCE_H

#include <stdint.h>

struct bsal_dna_sequence {
    void *encoded_data;
    int length_in_nucleotides;
    int64_t pair;
};

void bsal_dna_sequence_init(struct bsal_dna_sequence *sequence,
                char *data);
void bsal_dna_sequence_destroy(struct bsal_dna_sequence *sequence);

int bsal_dna_sequence_unpack(struct bsal_dna_sequence *sequence,
                void *buffer);
int bsal_dna_sequence_pack(struct bsal_dna_sequence *sequence,
                void *buffer);
int bsal_dna_sequence_pack_size(struct bsal_dna_sequence *sequence);
int bsal_dna_sequence_pack_unpack(struct bsal_dna_sequence *sequence,
                void *buffer, int operation);

void bsal_dna_sequence_print(struct bsal_dna_sequence *sequence);

int bsal_dna_sequence_length(struct bsal_dna_sequence *self);

void bsal_dna_sequence_get_sequence(struct bsal_dna_sequence *self, char *sequence);
void bsal_dna_sequence_init_same_data(struct bsal_dna_sequence *self,
                struct bsal_dna_sequence *other);
void bsal_dna_sequence_init_copy(struct bsal_dna_sequence *self,
                struct bsal_dna_sequence *other);

#endif
