
#include "map_helper.h"

#include <core/structures/map.h>

#include <stdlib.h>

int bsal_map_get_int(struct bsal_map *self, void *key)
{
    int *bucket;
    int value;

    value = -1;
    bucket = (int *)bsal_map_get(self, key);

    if (bucket != NULL) {
        value = *bucket;
    }

    return value;
}
