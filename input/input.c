
#include "input.h"
#include "input_vtable.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void bsal_input_init(struct bsal_input *input, void *pointer,
                struct bsal_input_vtable *vtable, char *file)
{
    bsal_input_init_fn_t handler;
    FILE *descriptor;

    input->pointer = pointer;
    input->vtable = vtable;
    input->sequences = 0;
    input->file = file;
    input->error = BSAL_INPUT_ERROR_NONE;

    descriptor = fopen(input->file, "r");

    if (descriptor == NULL) {
        input->error = BSAL_INPUT_ERROR_NOT_FOUND;
        return;
    } else {
        fclose(descriptor);
    }

    handler = bsal_input_vtable_get_init(input->vtable);
    handler(input);
}

void bsal_input_destroy(struct bsal_input *input)
{
    bsal_input_destroy_fn_t handler;

    if (bsal_input_valid(input)) {
        handler = bsal_input_vtable_get_destroy(input->vtable);
        handler(input);
    }

    input->pointer = NULL;
    input->vtable = NULL;
    input->sequences = -1;
    input->file = NULL;
}

int bsal_input_get_sequence(struct bsal_input *input,
                struct bsal_dna_sequence *sequence)
{
    bsal_input_get_sequence_fn_t handler;
    int value;

    if (!bsal_input_valid(input)) {
        return 0;
    }

    handler = bsal_input_vtable_get_get_sequence(input->vtable);
    value = handler(input, sequence);

    if (value) {
        input->sequences++;
    }

#ifdef BSAL_INPUT_DEBUG
    if (input->sequences % 10000000 == 0) {
        printf("DEBUG bsal_input_get_sequence %i\n",
                        input->sequences);
    }
#endif

    return value;
}

int bsal_input_size(struct bsal_input *input)
{
    return input->sequences;
}

char *bsal_input_file(struct bsal_input *input)
{
    return input->file;
}

void *bsal_input_pointer(struct bsal_input *input)
{
    return input->pointer;
}

int bsal_input_valid(struct bsal_input *input)
{
    return bsal_input_error(input) == BSAL_INPUT_ERROR_NONE;
}

int bsal_input_error(struct bsal_input *input)
{
    return input->error;
}

int bsal_input_detect(struct bsal_input *input)
{
    bsal_input_detect_fn_t handler;

    if (!bsal_input_valid(input)) {
        return 0;
    }

    handler = bsal_input_vtable_get_detect(input->vtable);
    return handler(input);
}

int bsal_input_has_suffix(struct bsal_input *input, const char *suffix)
{
    int position_in_file;
    int position_in_suffix;

    position_in_file = strlen(input->file) - 1;
    position_in_suffix = strlen(suffix) - 1;

    while (position_in_file >= 0 && position_in_suffix >= 0) {
        if (input->file[position_in_file] != suffix[position_in_suffix]) {
            return 0;
        }

        position_in_file--;
        position_in_suffix--;
    }

    if (position_in_suffix < 0) {
        return 1;
    }

    return 0;
}
