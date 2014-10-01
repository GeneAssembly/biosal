
#include "dna_helper.h"

#include <string.h>

void biosal_dna_helper_reverse_complement_in_place(char *sequence)
{
    int position;
    int length;
    int middle;
    char saved;
    int position2;

    length = strlen(sequence);
    middle = length / 2;

    position = 0;

    while (position < length) {
        sequence[position] = biosal_dna_helper_complement_nucleotide(sequence[position]);
        position++;
    }

    position = 0;

    while (position < middle) {
        position2 = length - 1 - position;
        saved = sequence[position2];
        sequence[position2] = sequence[position];
        sequence[position] = saved;
        position++;
    }
}

void biosal_dna_helper_reverse_complement(char *sequence1, char *sequence2)
{
    int length;
    int position1;
    int position2;
    char nucleotide1;
    char nucleotide2;

    length = strlen(sequence1);

    position1 = length - 1;
    position2 = 0;

    while (position2 < length) {

        nucleotide1 = sequence1[position1];
        nucleotide2 = biosal_dna_helper_complement_nucleotide(nucleotide1);

        sequence2[position2] = nucleotide2;

        position2++;
        position1--;
    }

    sequence2[position2] = '\0';

#ifdef BIOSAL_DNA_KMER_DEBUG
    printf("%s and %s\n", sequence1, sequence2);
#endif
}

#define BIOSAL_DNA_HELPER_FAST

char biosal_dna_helper_complement_nucleotide(char nucleotide)
{
#ifdef BIOSAL_DNA_HELPER_FAST
    if (nucleotide == 'A') {
        return 'T';
    } else if (nucleotide == 'G') {
        return 'C';
    } else if (nucleotide == 'T') {
        return 'A';
    } else /* if (nucleotide == 'C') */ {
        return 'G';
    }
    return nucleotide;
#else
    switch (nucleotide) {
        case 'A':
            return 'T';
        case 'T':
            return 'A';
        case 'C':
            return 'G';
        case 'G':
            return 'C';
        default:
            return nucleotide;
    }

    return nucleotide;
#endif
}

void biosal_dna_helper_normalize(char *sequence)
{
    int length;
    int position;

    length = strlen(sequence);

    position = 0;

    while (position < length) {

        sequence[position] = biosal_dna_helper_normalize_nucleotide(sequence[position]);

        position++;
    }
}

char biosal_dna_helper_normalize_nucleotide(char nucleotide)
{
    char default_value;

    default_value = 'A';

    switch (nucleotide) {
        case 't':
            return 'T';
        case 'a':
            return 'A';
        case 'g':
            return 'G';
        case 'c':
            return 'C';
        case 'T':
            return 'T';
        case 'A':
            return 'A';
        case 'G':
            return 'G';
        case 'C':
            return 'C';
        case 'n':
            return default_value;
        case '.':
            return default_value;
        default:
            return default_value;
    }

    return default_value;
}

void biosal_dna_helper_set_lower_case(char *sequence, int start, int end)
{
    char old_character;
    char new_character;
    int i;

    for (i = start; i <= end; ++i) {
        old_character = sequence[i];
        new_character = old_character;
        if (old_character == 'A')
            new_character = 'a';
        else if (old_character == 'T')
            new_character = 't';
        else if (old_character == 'C')
            new_character = 'c';
        else if (old_character == 'G')
            new_character = 'g';

        sequence[i] = new_character;
    }
}
