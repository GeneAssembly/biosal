
#ifndef _BSAL_FASTQ_INPUT_H
#define _BSAL_FASTQ_INPUT_H

#include <input/input.h>
#include <input/input_vtable.h>

#include <stdio.h>

struct bsal_fastq_input {
    FILE *descriptor;
    int dummy;
};

struct bsal_input_vtable bsal_fastq_input_vtable;

void bsal_fastq_input_init(struct bsal_input *input);
void bsal_fastq_input_destroy(struct bsal_input *input);
int bsal_fastq_input_get_sequence(struct bsal_input *input,
                struct bsal_dna_sequence *sequence);
int bsal_fastq_input_detect(struct bsal_input *input);

#endif
