
#ifndef BSAL_STRING_H
#define BSAL_STRING_H

struct bsal_string {

    char *data;
};

void bsal_string_init(struct bsal_string *self, const char *data);
void bsal_string_destroy(struct bsal_string *self);

void bsal_string_append(struct bsal_string *self, const char *data);
void bsal_string_prepend(struct bsal_string *self, const char *data);
char *bsal_string_get(struct bsal_string *self);

void bsal_string_combine(struct bsal_string *self, const char *data, int operation);

int bsal_string_pack_size(struct bsal_string *self);
int bsal_string_pack(struct bsal_string *self, void *buffer);
int bsal_string_unpack(struct bsal_string *self, void *buffer);
int bsal_string_pack_unpack(struct bsal_string *self, int operation, void *buffer);

int bsal_string_length(struct bsal_string *self);

#endif
