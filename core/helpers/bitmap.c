
#include "bitmap.h"

int core_bitmap_get_bit_uint8_t(uint8_t *self, int index)
{
    uint64_t bitmap;

    bitmap = *self;

    return core_bitmap_get_bit_uint64_t(&bitmap, index);
}

void core_bitmap_set_bit_value_uint8_t(uint8_t *self, int index, int value)
{
    uint64_t bitmap;

    bitmap = *self;

    core_bitmap_set_bit_value_uint64_t(&bitmap, index, value);

    *self = bitmap;
}

int core_bitmap_get_bit_uint32_t(uint32_t *self, int index)
{
    uint64_t bitmap;

    bitmap = *self;

    return core_bitmap_get_bit_uint64_t(&bitmap, index);
}

void core_bitmap_set_bit_value_uint32_t(uint32_t *self, int index, int value)
{
    uint64_t bitmap;

    bitmap = *self;

    core_bitmap_set_bit_value_uint64_t(&bitmap, index, value);

    *self = bitmap;
}

int core_bitmap_get_bit_uint64_t(uint64_t *self, int index)
{
    int bit_value;
    uint64_t bitmap;

    bitmap = *self;

    bit_value = (bitmap << (63 - index)) >> 63;

    return bit_value;
}

void core_bitmap_set_bit_value_uint64_t(uint64_t *self, int index, int value)
{
    uint64_t bitmap;
    uint64_t filter;

    bitmap = *self;

    if (value == CORE_BIT_ONE){
        bitmap |= (value << index);

        /* set bit to 0 */
    } else if (value == CORE_BIT_ZERO) {
        filter = CORE_BIT_ONE;
        filter <<= index;
        filter =~ filter;
        bitmap &= filter;
    }

    *self = bitmap;
}

void core_bitmap_set_bit_uint32_t(uint32_t *self, int index)
{
    core_bitmap_set_bit_value_uint32_t(self, index, CORE_BIT_ONE);
}

void core_bitmap_clear_bit_uint32_t(uint32_t *self, int index)
{
    core_bitmap_set_bit_value_uint32_t(self, index, CORE_BIT_ZERO);
}
