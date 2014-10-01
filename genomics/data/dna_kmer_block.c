
#include "dna_kmer_block.h"

#include "dna_kmer.h"

#include <core/structures/vector_iterator.h>
#include <core/system/packer.h>

#include <core/system/debugger.h>

#include <stdlib.h>

/*
#define BIOSAL_DNA_KMER_BLOCK_DEBUG
*/

void biosal_dna_kmer_block_init(struct biosal_dna_kmer_block *self, int kmer_length, int source_index, int kmers)
{
    self->source_index = source_index;
    self->kmer_length = kmer_length;
    biosal_vector_init(&self->kmers, sizeof(struct biosal_dna_kmer));

    biosal_vector_reserve(&self->kmers, kmers);
}

void biosal_dna_kmer_block_destroy(struct biosal_dna_kmer_block *self,
                struct biosal_memory_pool *memory)
{
    struct biosal_vector_iterator iterator;
    struct biosal_dna_kmer *kmer;

    /* destroy kmers
     */
    biosal_vector_iterator_init(&iterator, &self->kmers);

    while (biosal_vector_iterator_has_next(&iterator)) {

        biosal_vector_iterator_next(&iterator, (void **)&kmer);

        biosal_dna_kmer_destroy(kmer, memory);
    }

    biosal_vector_iterator_destroy(&iterator);
    self->source_index = -1;
    self->kmer_length = -1;
    biosal_vector_destroy(&self->kmers);
}

void biosal_dna_kmer_block_add_kmer(struct biosal_dna_kmer_block *self, struct biosal_dna_kmer *kmer,
                struct biosal_memory_pool *memory, struct biosal_dna_codec *codec)
{
    struct biosal_dna_kmer copy;

    biosal_dna_kmer_init_copy(&copy, kmer, self->kmer_length, memory, codec);

    biosal_vector_push_back(&self->kmers, &copy);
}

int biosal_dna_kmer_block_pack_size(struct biosal_dna_kmer_block *self, struct biosal_dna_codec *codec)
{
    return biosal_dna_kmer_block_pack_unpack(self, NULL, BIOSAL_PACKER_OPERATION_PACK_SIZE, NULL, codec);
}

int biosal_dna_kmer_block_pack(struct biosal_dna_kmer_block *self, void *buffer,
                struct biosal_dna_codec *codec)
{
    return biosal_dna_kmer_block_pack_unpack(self, buffer, BIOSAL_PACKER_OPERATION_PACK, NULL, codec);
}

int biosal_dna_kmer_block_unpack(struct biosal_dna_kmer_block *self, void *buffer, struct biosal_memory_pool *memory,
                struct biosal_dna_codec *codec)
{

#ifdef BIOSAL_DNA_KMER_BLOCK_DEBUG
    BIOSAL_DEBUG_MARKER("unpacking block");
#endif

    return biosal_dna_kmer_block_pack_unpack(self, buffer, BIOSAL_PACKER_OPERATION_UNPACK, memory, codec);
}

int biosal_dna_kmer_block_pack_unpack(struct biosal_dna_kmer_block *self, void *buffer,
                int operation, struct biosal_memory_pool *memory, struct biosal_dna_codec *codec)
{
    struct biosal_packer packer;
    int offset;
    int elements;
    int i = 0;
    struct biosal_dna_kmer *kmer;
    struct biosal_dna_kmer new_kmer;

    biosal_packer_init(&packer, operation, buffer);

#ifdef BIOSAL_DNA_KMER_BLOCK_DEBUG
    BIOSAL_DEBUG_MARKER("pack unpack 1");
#endif

    biosal_packer_process(&packer, &self->kmer_length, sizeof(self->kmer_length));
    biosal_packer_process(&packer, &self->source_index, sizeof(self->source_index));

    if (operation != BIOSAL_PACKER_OPERATION_UNPACK) {
        elements = biosal_vector_size(&self->kmers);
    }

#ifdef BIOSAL_DNA_KMER_BLOCK_DEBUG
    BIOSAL_DEBUG_MARKER("pack unpack 2");
#endif

    biosal_packer_process(&packer, &elements, sizeof(elements));

    if (operation == BIOSAL_PACKER_OPERATION_UNPACK) {

        biosal_dna_kmer_block_init(self, self->kmer_length, self->source_index, elements);
    }

#ifdef BIOSAL_DNA_KMER_BLOCK_DEBUG
    printf("DEBUG kmer_length %d source_index %d elements %d\n", self->kmer_length,
                    self->source_index, elements);
#endif

    offset = biosal_packer_get_byte_count(&packer);
    biosal_packer_destroy(&packer);

    /* do the rest manually
     */

#ifdef BIOSAL_DNA_KMER_BLOCK_DEBUG
    BIOSAL_DEBUG_MARKER("pack unpack 3");
#endif

    if (operation == BIOSAL_PACKER_OPERATION_UNPACK) {

        for (i = 0; i < elements; i++) {
            offset += biosal_dna_kmer_pack_unpack(&new_kmer, (char *)buffer + offset, operation,
                            self->kmer_length, memory, codec);

            biosal_dna_kmer_block_add_kmer(self, &new_kmer, memory, codec);

            biosal_dna_kmer_destroy(&new_kmer, memory);
        }
    } else {
        for (i = 0; i < elements; i++) {
            kmer = (struct biosal_dna_kmer *)biosal_vector_at(&self->kmers, i);

            offset += biosal_dna_kmer_pack_unpack(kmer, (char *)buffer + offset, operation,
                            self->kmer_length, memory, codec);
        }
    }

#ifdef BIOSAL_DNA_KMER_BLOCK_DEBUG
    BIOSAL_DEBUG_MARKER("pack unpack EXIT");
#endif

    return offset;
}

int biosal_dna_kmer_block_source_index(struct biosal_dna_kmer_block *self)
{
    return self->source_index;
}

struct biosal_vector *biosal_dna_kmer_block_kmers(struct biosal_dna_kmer_block *self)
{
    return &self->kmers;
}

int biosal_dna_kmer_block_size(struct biosal_dna_kmer_block *self)
{
    return biosal_vector_size(&self->kmers);
}

void biosal_dna_kmer_block_init_empty(struct biosal_dna_kmer_block *self)
{
    biosal_dna_kmer_block_init(self, -1, -1, -1);
}
