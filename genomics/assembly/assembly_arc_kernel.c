
#include "assembly_arc_kernel.h"

#include <genomics/kernels/dna_kmer_counter_kernel.h>

#include <stdio.h>

struct bsal_script bsal_assembly_arc_kernel_script = {
    .identifier = BSAL_ASSEMBLY_ARC_KERNEL_SCRIPT,
    .name = "bsal_assembly_arc_kernel",
    .init = bsal_assembly_arc_kernel_init,
    .destroy = bsal_assembly_arc_kernel_destroy,
    .receive = bsal_assembly_arc_kernel_receive,
    .size = sizeof(struct bsal_assembly_arc_kernel)
};

void bsal_assembly_arc_kernel_init(struct bsal_actor *self)
{
    struct bsal_assembly_arc_kernel *concrete_self;

    concrete_self = (struct bsal_assembly_arc_kernel *)bsal_actor_concrete_actor(self);

    concrete_self->kmer_length = -1;

    bsal_actor_add_route(self, BSAL_ACTOR_ASK_TO_STOP,
                    bsal_actor_ask_to_stop);

    bsal_actor_add_route(self, BSAL_SET_KMER_LENGTH,
                    bsal_assembly_arc_kernel_set_kmer_length);

    printf("%s/%d is now active\n",
                    bsal_actor_script_name(self),
                    bsal_actor_name(self));
}

void bsal_assembly_arc_kernel_destroy(struct bsal_actor *self)
{
    struct bsal_assembly_arc_kernel *concrete_self;

    concrete_self = (struct bsal_assembly_arc_kernel *)bsal_actor_concrete_actor(self);

    concrete_self->kmer_length = -1;
}

void bsal_assembly_arc_kernel_receive(struct bsal_actor *self, struct bsal_message *message)
{
    bsal_actor_use_route(self, message);
}

void bsal_assembly_arc_kernel_set_kmer_length(struct bsal_actor *self, struct bsal_message *message)
{
    struct bsal_assembly_arc_kernel *concrete_self;

    concrete_self = (struct bsal_assembly_arc_kernel *)bsal_actor_concrete_actor(self);

    bsal_message_unpack_int(message, 0, &concrete_self->kmer_length);

    bsal_actor_send_reply_empty(self, BSAL_SET_KMER_LENGTH_REPLY);
}
