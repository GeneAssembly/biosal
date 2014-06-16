
#ifndef BSAL_VECTOR_HELPER_H
#define BSAL_VECTOR_HELPER_H

#include <stdint.h>

struct bsal_vector;

void bsal_vector_helper_print_int(struct bsal_vector *self);
void bsal_vector_helper_set_int(struct bsal_vector *self, int64_t index, int value);
void bsal_vector_helper_push_back_int(struct bsal_vector *self, int value);
int bsal_vector_helper_at_as_int(struct bsal_vector *self, int64_t index);
char *bsal_vector_helper_at_as_char_pointer(struct bsal_vector *self, int64_t index);
void *bsal_vector_helper_at_as_void_pointer(struct bsal_vector *self, int64_t index);

void bsal_vector_helper_sort_int(struct bsal_vector *self);
int bsal_vector_helper_compare_int(const void *a, const void *b);

typedef int (*bsal_compare_fn_t)(
    const void *value1,
    const void *value2
);

void bsal_vector_helper_sort(struct bsal_vector *self,
                bsal_compare_fn_t compare);

void bsal_vector_helper_quicksort(struct bsal_vector *self,
                int64_t first, int64_t last, bsal_compare_fn_t compare,
                void *saved_pivot_value);

int64_t bsal_vector_helper_select_pivot(struct bsal_vector *self,
                int64_t first, int64_t last, bsal_compare_fn_t compare);

int64_t bsal_vector_helper_partition(struct bsal_vector *self,
                int64_t first, int64_t last, bsal_compare_fn_t compare,
                void *saved_pivot_value);
void bsal_vector_helper_swap(struct bsal_vector *self,
                int64_t index1, int64_t index2);
void bsal_vector_helper_sort_int(struct bsal_vector *self);

#endif
