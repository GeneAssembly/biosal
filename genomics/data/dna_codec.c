
#include "dna_codec.h"

#include <genomics/helpers/dna_helper.h>

#include <core/structures/vector.h>

#include <core/helpers/vector_helper.h>
#include <core/system/memory.h>

#include <string.h>
#include <stdio.h>

#include <stdint.h>

#define BITS_PER_NUCLEOTIDE 2
#define BITS_PER_BYTE 8

#define BIOSAL_DNA_CODEC_MINIMUM_NODE_COUNT_FOR_TWO_BIT 2

/*
 * Fast implemention of reverse complement in place
 * in 2-bit format.
 */
/*
*/
#define FAST_REVERSE_COMPLEMENT

#define MEMORY_CODEC 0x9687799e

void biosal_dna_codec_init(struct biosal_dna_codec *self)
{
    /* 4 * 2 = 8 bits = 1 byte
     */
    self->block_length = 4;

    self->use_two_bit_encoding = 0;

#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_ENCODER
    biosal_memory_pool_init(&self->pool, 0, MEMORY_CODEC);

    biosal_map_init(&self->encoding_lookup_table, self->block_length, 1);
    biosal_map_set_memory_pool(&self->encoding_lookup_table, &self->pool);
#endif

#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_DECODER
    biosal_map_init(&self->decoding_lookup_table, 1, self->block_length);
    biosal_map_set_memory_pool(&self->decoding_lookup_table, &self->pool);

    biosal_dna_codec_generate_blocks(self);
#endif

#ifdef BIOSAL_DNA_CODEC_FORCE_TWO_BIT_ENCODING_DISABLE_000
    biosal_dna_codec_enable_two_bit_encoding(self);
#endif
}

void biosal_dna_codec_destroy(struct biosal_dna_codec *self)
{
#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_ENCODER
    biosal_map_destroy(&self->encoding_lookup_table);
#endif
#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_DECODER
    biosal_map_destroy(&self->decoding_lookup_table);

    biosal_memory_pool_destroy(&self->pool);
#endif

    self->block_length = 0;
}

#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_ENCODER
void biosal_dna_codec_generate_blocks(struct biosal_dna_codec *self)
{
    char *block;

    block = biosal_memory_allocate(self->block_length + 1, MEMORY_CODEC);

    biosal_dna_codec_generate_block(self, -1, 'X', block);

    biosal_memory_free(block, MEMORY_CODEC);
}

void biosal_dna_codec_generate_block(struct biosal_dna_codec *self, int position, char symbol,
                char *block)
{
    char buffer[10];
    void *bucket;

#ifdef BIOSAL_DNA_CODEC_DEBUG
    printf("DEBUG position %d symbol %c\n", position, symbol);
#endif

    if (position >= 0) {
        block[position] = symbol;
    }

    position++;

    if (position < self->block_length && symbol != '\0') {

        biosal_dna_codec_generate_block(self, position, 'A', block);
        biosal_dna_codec_generate_block(self, position, 'T', block);
        biosal_dna_codec_generate_block(self, position, 'G', block);
        biosal_dna_codec_generate_block(self, position, 'C', block);
        /*biosal_dna_codec_generate_block(self, position, '\0', block);*/
    } else {

        while (position < self->block_length + 1) {

            block[position] = '\0';
            position++;
        }

        biosal_dna_codec_encode_default(self, self->block_length, block, buffer);

#ifdef BIOSAL_DNA_CODEC_DEBUG
        printf("BLOCK %s (%d) value %d\n", block, self->block_length, (int)buffer[0]);
#endif

        bucket = biosal_map_add(&self->encoding_lookup_table, block);
        biosal_memory_copy(bucket, buffer, 1);

        bucket = biosal_map_add(&self->decoding_lookup_table, buffer);
        biosal_memory_copy(bucket, block, self->block_length);
    }

}
#endif

int biosal_dna_codec_encoded_length(struct biosal_dna_codec *self, int length_in_nucleotides)
{
    if (self->use_two_bit_encoding) {
        return biosal_dna_codec_encoded_length_default(self, length_in_nucleotides);
    }

    return length_in_nucleotides + 1;
}

int biosal_dna_codec_encoded_length_default(struct biosal_dna_codec *self, int length_in_nucleotides)
{
    int bits;
    int bytes;

    bits = length_in_nucleotides * BITS_PER_NUCLEOTIDE;

    /* padding
     */
    bits += (BITS_PER_BYTE - (bits % BITS_PER_BYTE));

    bytes = bits / BITS_PER_BYTE;

    return bytes;
}

void biosal_dna_codec_encode(struct biosal_dna_codec *self,
                int length_in_nucleotides, char *dna_sequence, void *encoded_sequence)
{
    if (self->use_two_bit_encoding) {

#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_ENCODER
        biosal_dna_codec_encode_with_blocks(self, length_in_nucleotides, dna_sequence, encoded_sequence);
#else
        biosal_dna_codec_encode_default(self, length_in_nucleotides, dna_sequence, encoded_sequence);
#endif
    } else {
        strcpy(encoded_sequence, dna_sequence);
    }
}

#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_ENCODER
void biosal_dna_codec_encode_with_blocks(struct biosal_dna_codec *self,
                int length_in_nucleotides, char *dna_sequence, void *encoded_sequence)
{

#if 0
    biosal_dna_codec_encode_default(length_in_nucleotides, dna_sequence, encoded_sequence);
#endif
    int position;
    uint8_t byte;
    char data[5];
    int remaining;
    char *block;
    int byte_position;

    byte_position = 0;
    position = 0;

#ifdef BIOSAL_DNA_CODEC_DEBUG
    printf("DEBUG encoding %s\n", dna_sequence);
#endif

    while (position < length_in_nucleotides) {


        remaining = length_in_nucleotides - position;
        block = dna_sequence + position;

#ifdef BIOSAL_DNA_CODEC_DEBUG
        printf("DEBUG position %d remaining %d block_length %d\n",
                        position, remaining, self->block_length);
#endif

        /* a buffer is required when less than block size
         * nucleotides remain
         */
        if (remaining < self->block_length) {

            /* A is 00, so it is the same as nothing
             */
            memset(data, 'A', self->block_length);
            biosal_memory_copy(data, block, remaining);
            block = data;
        }

#ifdef BIOSAL_DNA_CODEC_DEBUG
        printf("DEBUG block %c%c%c%c\n", block[0], block[1], block[2], block[3]);
#endif

        byte = *(char *)biosal_map_get(&self->encoding_lookup_table, block);
        ((char *)encoded_sequence)[byte_position] = byte;

        byte_position++;
        position += self->block_length;
    }
}
#endif

void biosal_dna_codec_encode_default(struct biosal_dna_codec *codec,
                int length_in_nucleotides, char *dna_sequence, void *encoded_sequence)
{
    int i;

    int encoded_length;

    encoded_length = biosal_dna_codec_encoded_length(codec, length_in_nucleotides);

#ifdef BIOSAL_DNA_CODEC_DEBUG
    printf("DEBUG encoding %s %d nucleotides, encoded_length %d\n", dna_sequence, length_in_nucleotides,
                    encoded_length);
#endif

    i = 0;

    /*
     * Set the tail to 0 before doing anything.
     */
    ((uint8_t*)encoded_sequence)[encoded_length - 1] = 0;

    while (i < length_in_nucleotides) {

        biosal_dna_codec_set_nucleotide(codec, encoded_sequence, i, dna_sequence[i]);

        i++;
    }
}

void biosal_dna_codec_decode(struct biosal_dna_codec *codec,
                int length_in_nucleotides, void *encoded_sequence, char *dna_sequence)
{
    if (codec->use_two_bit_encoding) {
#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_DECODER
        biosal_dna_codec_decode_with_blocks(codec, length_in_nucleotides, encoded_sequence, dna_sequence);
#else
        biosal_dna_codec_decode_default(codec, length_in_nucleotides, encoded_sequence, dna_sequence);

#endif
    } else {
        strcpy(dna_sequence, encoded_sequence);
    }
}

#ifdef BIOSAL_DNA_CODEC_USE_TWO_BIT_BLOCK_DECODER
void biosal_dna_codec_decode_with_blocks(struct biosal_dna_codec *self,
                int length_in_nucleotides, void *encoded_sequence, char *dna_sequence)
{
    char byte;
    int nucleotide_position;
    int encoded_position;
    int remaining;
    void *encoded_block;
    int to_copy;

    encoded_position = 0;
    nucleotide_position = 0;

    while (nucleotide_position < length_in_nucleotides) {

        remaining = length_in_nucleotides - nucleotide_position;
        byte = ((char *)encoded_sequence)[encoded_position];
        encoded_block = biosal_map_get(&self->decoding_lookup_table, &byte);
        to_copy = self->block_length;

        if (to_copy > remaining) {
            to_copy = remaining;
        }
        biosal_memory_copy(dna_sequence + nucleotide_position, encoded_block, to_copy);

        nucleotide_position += self->block_length;
        encoded_position++;
    }

    dna_sequence[length_in_nucleotides] = '\0';
}
#endif

void biosal_dna_codec_decode_default(struct biosal_dna_codec *codec, int length_in_nucleotides, void *encoded_sequence, char *dna_sequence)
{
    int i;

    i = 0;

    while (i < length_in_nucleotides) {

        dna_sequence[i] = biosal_dna_codec_get_nucleotide(codec, encoded_sequence, i);

        i++;
    }

    dna_sequence[length_in_nucleotides] = '\0';
}

void biosal_dna_codec_set_nucleotide(struct biosal_dna_codec *self,
                void *encoded_sequence, int index, char nucleotide)
{
    int bit_index;
    int byte_index;
    int bit_index_in_byte;
    uint64_t old_byte_value;
    uint64_t mask;
    uint64_t new_byte_value;
    char *sequence;

    if (!self->use_two_bit_encoding) {

        sequence = encoded_sequence;

        sequence[index] = nucleotide;

        return;
    }

    bit_index = index * BITS_PER_NUCLEOTIDE;
    byte_index = bit_index / BITS_PER_BYTE;
    bit_index_in_byte = bit_index % BITS_PER_BYTE;

#ifdef BIOSAL_DNA_CODEC_DEBUG
    printf("index %d nucleotide %c bit_index %d byte_index %d bit_index_in_byte %d\n",
                        index, nucleotide, bit_index, byte_index, bit_index_in_byte);

    if (nucleotide == BIOSAL_NUCLEOTIDE_SYMBOL_C) {
    }
#endif

    old_byte_value = ((uint8_t*)encoded_sequence)[byte_index];

    new_byte_value = old_byte_value;

    /*
     * Remove the bits set to 1
     */

    mask = BIOSAL_NUCLEOTIDE_CODE_T;
    mask <<= bit_index_in_byte;
    mask = ~mask;

    new_byte_value &= mask;

    /* Now, apply the real mask
     */
    mask = biosal_dna_codec_get_code(nucleotide);

#ifdef BIOSAL_DNA_CODEC_DEBUG
    if (nucleotide == BIOSAL_NUCLEOTIDE_SYMBOL_C) {
        printf("DEBUG code is %d\n", (int)mask);
    }
#endif

    mask <<= bit_index_in_byte;

    new_byte_value |= mask;

#ifdef BIOSAL_DNA_CODEC_DEBUG
    printf("old: %d new: %d\n", (int)old_byte_value, (int)new_byte_value);

    if (nucleotide == BIOSAL_NUCLEOTIDE_SYMBOL_C) {

    }
#endif

    ((uint8_t *)encoded_sequence)[byte_index] = new_byte_value;
}

uint64_t biosal_dna_codec_get_code(char nucleotide)
{
    switch (nucleotide) {

        case BIOSAL_NUCLEOTIDE_SYMBOL_A:
            return BIOSAL_NUCLEOTIDE_CODE_A;
        case BIOSAL_NUCLEOTIDE_SYMBOL_T:
            return BIOSAL_NUCLEOTIDE_CODE_T;
        case BIOSAL_NUCLEOTIDE_SYMBOL_C:
            return BIOSAL_NUCLEOTIDE_CODE_C;
        case BIOSAL_NUCLEOTIDE_SYMBOL_G:
            return BIOSAL_NUCLEOTIDE_CODE_G;
    }

    return BIOSAL_NUCLEOTIDE_CODE_A;
}

char biosal_dna_codec_get_nucleotide(struct biosal_dna_codec *codec, void *encoded_sequence, int index)
{
    int code;

    code = biosal_dna_codec_get_nucleotide_code(codec, encoded_sequence, index);

    return biosal_dna_codec_get_nucleotide_from_code(code);
}

int biosal_dna_codec_get_nucleotide_code(struct biosal_dna_codec *codec, void *encoded_sequence, int index)
{
    int bit_index;
    int byte_index;
    int bit_index_in_byte;
    uint64_t byte_value;
    uint64_t code;
    char symbol;

    if (!codec->use_two_bit_encoding) {
        symbol = ((char *)encoded_sequence)[index];

        return biosal_dna_codec_get_code(symbol);
    }

    bit_index = index * BITS_PER_NUCLEOTIDE;
    byte_index = bit_index / BITS_PER_BYTE;
    bit_index_in_byte = bit_index % BITS_PER_BYTE;

    byte_value = ((uint8_t *)encoded_sequence)[byte_index];

    code = (byte_value << (8 * BITS_PER_BYTE - BITS_PER_NUCLEOTIDE - bit_index_in_byte)) >> (8 * BITS_PER_BYTE - BITS_PER_NUCLEOTIDE);

#ifdef BIOSAL_DNA_CODEC_DEBUG
    printf("code %d\n", (int)code);
#endif

    return code;
}

char biosal_dna_codec_get_nucleotide_from_code(uint64_t code)
{
    switch (code) {
        case BIOSAL_NUCLEOTIDE_CODE_A:
            return BIOSAL_NUCLEOTIDE_SYMBOL_A;
        case BIOSAL_NUCLEOTIDE_CODE_T:
            return BIOSAL_NUCLEOTIDE_SYMBOL_T;
        case BIOSAL_NUCLEOTIDE_CODE_C:
            return BIOSAL_NUCLEOTIDE_SYMBOL_C;
        case BIOSAL_NUCLEOTIDE_CODE_G:
            return BIOSAL_NUCLEOTIDE_SYMBOL_G;
    }

    return BIOSAL_NUCLEOTIDE_SYMBOL_A;
}

/* this would be diccult to do because the padding at the end is not
 * always a multiple of block_length
 */
void biosal_dna_codec_reverse_complement_in_place(struct biosal_dna_codec *codec,
                int length_in_nucleotides, void *encoded_sequence)
{
#ifdef FAST_REVERSE_COMPLEMENT
    int encoded_length;
    int i;
    uint64_t byte_value;
    int middle;
    char left_nucleotide;
    int left;
    int right;
    char right_nucleotide;
    int tail;
    char blank;
    int total_length;

    /* Abort if the 2 bit encoding is not being used.
     */
    if (!codec->use_two_bit_encoding) {
        biosal_dna_helper_reverse_complement_in_place(encoded_sequence);
        return;
    }


#if 0
    char *sequence;

    sequence = b212sal_memory_allocate(length_in_nucleotides + 1);
    biosal_dna_codec_decode(codec, length_in_nucleotides, encoded_sequence, sequence);
    printf("INPUT: %s\n", sequence);
    biosal_memory_free(sequence);
#endif

    encoded_length = biosal_dna_codec_encoded_length(codec, length_in_nucleotides);

    i = 0;

    /* Complement all the nucleotides
     */
    while (i < encoded_length) {
        byte_value = ((uint8_t*)encoded_sequence)[i];

        /*
         * \see http://stackoverflow.com/questions/6508585/how-to-use-inverse-in-c
         */
        byte_value = ~byte_value;
        ((uint8_t*)encoded_sequence)[i] = byte_value;

        ++i;
    }
#if 0
#endif

    /*
     * Reverse the order
     */
    i = 0;
    middle = length_in_nucleotides / 2;
    while (i < middle) {
        left = i;
        left_nucleotide = biosal_dna_codec_get_nucleotide(codec, encoded_sequence, left);

#if 0
        printf("%i %c\n", i, left_nucleotide);
#endif

        right = length_in_nucleotides - 1 - i;
        right_nucleotide = biosal_dna_codec_get_nucleotide(codec, encoded_sequence, right);
/*
        printf("left %d %c right %d %c\n", left, left_nucleotide,
                        right, right_nucleotide);
*/
        biosal_dna_codec_set_nucleotide(codec, encoded_sequence, left, right_nucleotide);
        biosal_dna_codec_set_nucleotide(codec, encoded_sequence, right, left_nucleotide);

        ++i;
    }

    /*
     * Fix the tail.
     */

    total_length = encoded_length * 4;

    tail = total_length - length_in_nucleotides;

#if 0
    printf("length_in_nucleotides %d\n", length_in_nucleotides);

    printf("Padding tail %d\n", tail);
#endif

    if (tail != 0) {
        i = 0;
        blank = BIOSAL_NUCLEOTIDE_SYMBOL_A;
        while (i < tail) {
            biosal_dna_codec_set_nucleotide(codec, encoded_sequence, length_in_nucleotides + i, blank);
            ++i;
        }
    }

#if 0
    sequence = bs2al_memory_allocate(length_in_nucleotides + 1);
    biosal_dna_codec_decode(codec, length_in_nucleotides, encoded_sequence, sequence);
    printf("INPUT after: %s\n", sequence);
    biosal_memory_free(sequence);
#endif


#else
    char *sequence;

    sequence = biosal_memory_allocate(length_in_nucleotides + 1);

    biosal_dna_codec_decode(codec, length_in_nucleotides, encoded_sequence, sequence);

    biosal_dna_helper_reverse_complement_in_place(sequence);

    biosal_dna_codec_encode(codec, length_in_nucleotides, sequence, encoded_sequence);

    biosal_memory_free(sequence);
#endif
}

void biosal_dna_codec_enable_two_bit_encoding(struct biosal_dna_codec *codec)
{
    codec->use_two_bit_encoding = 1;
}

void biosal_dna_codec_disable_two_bit_encoding(struct biosal_dna_codec *codec)
{
    codec->use_two_bit_encoding = 0;
}

int biosal_dna_codec_is_canonical(struct biosal_dna_codec *codec,
                int length_in_nucleotides, void *encoded_sequence)
{
    int i;
    char nucleotide;
    char other_nucleotide;

    i = 0;

    while (i < length_in_nucleotides) {

        nucleotide = biosal_dna_codec_get_nucleotide(codec, encoded_sequence, i);

        other_nucleotide = biosal_dna_codec_get_nucleotide(codec, encoded_sequence,
                        length_in_nucleotides - 1 - i);

        other_nucleotide = biosal_dna_helper_complement_nucleotide(other_nucleotide);

        /* It is canonical
         */
        if (nucleotide < other_nucleotide) {
            return 1;
        }

        /* It is not canonical
         */
        if (other_nucleotide < nucleotide) {
            return 0;
        }

        /* So far, the sequence is identical to its
         * reverse complement.
         */
        ++i;
    }

    /* The sequences are equal, so it is canonical.
     */
    return 1;
}

int biosal_dna_codec_get_complement(int code)
{
    if (code == BIOSAL_NUCLEOTIDE_CODE_A) {
        return BIOSAL_NUCLEOTIDE_CODE_T;

    } else if (code == BIOSAL_NUCLEOTIDE_CODE_C) {
        return BIOSAL_NUCLEOTIDE_CODE_G;

    } else if (code == BIOSAL_NUCLEOTIDE_CODE_G) {
        return BIOSAL_NUCLEOTIDE_CODE_C;

    } else if (code == BIOSAL_NUCLEOTIDE_CODE_T) {
        return BIOSAL_NUCLEOTIDE_CODE_A;
    }

    /*
     * This statement is not reachable.
     */
    return -1;
}

void biosal_dna_codec_mutate_as_child(struct biosal_dna_codec *self,
                int length_in_nucleotides, void *encoded_sequence, int last_code)
{
    int i;
    char nucleotide;
    int limit;

    limit = length_in_nucleotides - 1;

    for (i = 0 ; i < limit ; i++) {

        nucleotide = biosal_dna_codec_get_nucleotide(self, encoded_sequence, i + 1);
        biosal_dna_codec_set_nucleotide(self, encoded_sequence, i, nucleotide);
    }

    nucleotide = biosal_dna_codec_get_nucleotide_from_code(last_code);

    biosal_dna_codec_set_nucleotide(self, encoded_sequence, length_in_nucleotides - 1,
                    nucleotide);
}

int biosal_dna_codec_must_use_two_bit_encoding(struct biosal_dna_codec *self,
                int node_count)
{
    return node_count >= BIOSAL_DNA_CODEC_MINIMUM_NODE_COUNT_FOR_TWO_BIT;
}

void biosal_dna_codec_mutate_as_parent(struct biosal_dna_codec *self,
                int length_in_nucleotides, void *encoded_sequence, int first_code)
{
    char nucleotide;
    int old_position;
    int new_position;

    /*
     *  [---------parent ----------]
     *    [---------current----------]
     */

    old_position = length_in_nucleotides - 2;
    new_position = length_in_nucleotides - 1;

    while (old_position >= 0) {

        nucleotide = biosal_dna_codec_get_nucleotide(self, encoded_sequence, old_position);
        biosal_dna_codec_set_nucleotide(self, encoded_sequence, new_position, nucleotide);

        --old_position;
        --new_position;
    }

    nucleotide = biosal_dna_codec_get_nucleotide_from_code(first_code);

    biosal_dna_codec_set_nucleotide(self, encoded_sequence, 0, nucleotide);
}


