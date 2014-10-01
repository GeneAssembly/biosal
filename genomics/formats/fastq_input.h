
#ifndef BIOSAL_FASTQ_INPUT_H
#define BIOSAL_FASTQ_INPUT_H

#include <genomics/formats/input_format.h>
#include <genomics/formats/input_format_interface.h>

#include <core/file_storage/input/buffered_reader.h>

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>

/*
 * FastQ driver.
 */
struct biosal_fastq_input {
    struct biosal_buffered_reader reader;
    char *buffer;

    int has_first;
};

extern struct biosal_input_format_interface biosal_fastq_input_operations;

void biosal_fastq_input_init(struct biosal_input_format *self);
void biosal_fastq_input_destroy(struct biosal_input_format *self);
uint64_t biosal_fastq_input_get_sequence(struct biosal_input_format *self,
                char *sequence);
int biosal_fastq_input_detect(struct biosal_input_format *self);
uint64_t biosal_fastq_input_get_offset(struct biosal_input_format *self);

int biosal_fastq_input_is_identifier(struct biosal_input_format *self, const char *line);
int biosal_fastq_input_is_identifier_mock(struct biosal_input_format *self, const char *line);

#endif
