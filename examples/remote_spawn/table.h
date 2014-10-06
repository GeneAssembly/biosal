
#ifndef _TABLE_H
#define _TABLE_H

#include <biosal.h>

#define SCRIPT_TABLE 0x53f9c43c

struct table {
    int received;
    int done;
    struct core_vector spawners;
};

#define ACTION_TABLE_DIE 0x00003391
#define ACTION_TABLE_DIE2 0x000005e0
#define ACTION_TABLE_NOTIFY 0x000026ea

extern struct thorium_script table_script;

#endif
