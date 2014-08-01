
#ifndef _GC_RATIO_CALCULATOR_H_
#define _GC_RATIO_CALCULATOR_H_

#include <biosal.h>

#define GC_RATIO_CALCULATOR_SCRIPT 0x74c20114

struct gc_ratio_calculator {
    struct bsal_vector spawners;
};

extern struct bsal_script gc_ratio_calculator_script;

void gc_ratio_calculator_init(struct bsal_actor *actor);
void gc_ratio_calculator_destroy(struct bsal_actor *actor);
void gc_ratio_calculator_receive(struct bsal_actor *actor, struct bsal_message *message);

#endif /* _GC_RATIO_CALCULATOR_H_ */
