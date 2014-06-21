
#include "mega_block.h"

#include <stdio.h>
#include <inttypes.h>

void bsal_mega_block_init(struct bsal_mega_block *self, int file, uint64_t offset, uint64_t entries,
                uint64_t entries_from_start)
{
    self->file = file;
    self->offset = offset;
    self->entries = entries;
    self->entries_from_start = entries_from_start;

}

void bsal_mega_block_destroy(struct bsal_mega_block *self)
{
    self->file = 0;
    self->offset = 0;
    self->entries = 0;
}

void bsal_mega_block_print(struct bsal_mega_block *self)
{
    printf("MEGA BLOCK file %d offset %" PRIu64 " entries %" PRIu64 " %" PRIu64 "\n", self->file,
                    self->offset, self->entries, self->entries_from_start);
}

uint64_t bsal_mega_block_get_entries_from_start(struct bsal_mega_block *self)
{
    return self->entries_from_start;
}
