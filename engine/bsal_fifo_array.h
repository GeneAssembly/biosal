

#ifndef _BSAL_FIFO_ARRAY_H
#define _BSAL_FIFO_ARRAY_H

/*
 * sizeof(char) is always 1 byte in C 1999
 * \see http://stackoverflow.com/questions/2215445/are-there-machines-where-sizeofchar-1
 * \see https://sites.google.com/site/kjellhedstrom2/threadsafecircularqueue
 * \see http://www.codeproject.com/Articles/43510/Lock-Free-Single-Producer-Single-Consumer-Circular
 */
struct bsal_fifo_array {
    int units;
    int bytes_per_unit;
    void *array;
    volatile int consumer_head;
    volatile int producer_tail;
};

void bsal_fifo_array_construct(struct bsal_fifo_array *fifo, int units, int bytes_per_unit);
void bsal_fifo_array_destruct(struct bsal_fifo_array *fifo);
int bsal_fifo_array_push(struct bsal_fifo_array *fifo, void *item);
int bsal_fifo_array_pop(struct bsal_fifo_array *fifo, void *item);

void *bsal_fifo_array_unit(struct bsal_fifo_array *fifo, int position);
int bsal_fifo_array_bytes(struct bsal_fifo_array *fifo);
int bsal_fifo_array_empty(struct bsal_fifo_array *fifo);
int bsal_fifo_array_full(struct bsal_fifo_array *fifo);

#endif


