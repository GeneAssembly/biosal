
#include "mock.h"
#include "buddy.h"

#include <stdio.h>

struct bsal_actor_vtable mock_vtable = {
    .init = mock_init,
    .destroy = mock_destroy,
    .receive = mock_receive
};

void mock_init(struct bsal_actor *actor)
{
    struct mock *mock;

    mock = (struct mock *)bsal_actor_actor(actor);
    mock->value = 42;
    mock->tasks = 0;
    mock->children[0] = -1;
    mock->children[1] = -1;
    mock->children[2] = -1;
}

void mock_destroy(struct bsal_actor *actor)
{
    struct mock *mock;

    mock = (struct mock *)bsal_actor_actor(actor);
    mock->value = -1;
}

void mock_receive(struct bsal_actor *actor, struct bsal_message *message)
{
    int tag;

    tag = bsal_message_tag(message);

    /* printf("mock_receive %i\n", tag); */

    if (tag == BSAL_START) {
        mock_start(actor, message);
    } else if (tag == MOCK_DIE) {
        mock_try_die(actor, message);
    } else if (tag == MOCK_NEW_CONTACTS) {
        mock_add_contacts(actor, message);
    } else if (tag == MOCK_NEW_CONTACTS_OK) {
        mock_send_death(actor, message);
    }
}

void mock_add_contacts(struct bsal_actor *actor, struct bsal_message *message)
{
    int source;
    int remote_actor;
    char *buffer;

    source = bsal_message_source(message);
    buffer = bsal_message_buffer(message);
    remote_actor = ((int*)buffer)[0];

    printf("mock_receive remote friend is %i\n",
                        remote_actor);

    bsal_message_set_tag(message, MOCK_NEW_CONTACTS_OK);
    bsal_actor_send(actor, source, message);

    /*
    bsal_message_set_tag(buffer, BUDDY_DIE);
    bsal_actor_send(actor, remote_actor, message);
    */
}

void mock_send_death(struct bsal_actor *actor, struct bsal_message *message)
{
    int source;

    source = bsal_message_source(message);
    printf("mock_receive killing %i\n", source);

    bsal_message_set_tag(message, MOCK_DIE);
    bsal_actor_send(actor, source, message);

    mock_try_die(actor, message);
}

void mock_try_die(struct bsal_actor *actor, struct bsal_message *message)
{
    struct mock *mock;

    mock = (struct mock *)bsal_actor_actor(actor);
    mock->tasks++;

    if (mock->tasks == 2) {
        mock_die(actor, message);
    }
}

void mock_die(struct bsal_actor *actor, struct bsal_message *message)
{
    int name;
    int source;

    name = bsal_actor_name(actor);
    source = bsal_message_source(message);

    printf("mock_die actor %i dies (MOCK_DIE from %i)\n", name, source);

    bsal_actor_die(actor);
}

void mock_start(struct bsal_actor *actor, struct bsal_message *message)
{
    struct mock *mock;
    int source;
    int name;
    int destination;
    int tag;
    int size;
    int next;

    tag = bsal_message_tag(message);
    source = bsal_message_source(message);
    destination = bsal_message_destination(message);

    name = bsal_actor_name(actor);
    mock = (struct mock *)bsal_actor_actor(actor);

    printf("mock_start Actor #%i (value: %i) received message (tag: %i BSAL_START)"
                    " from source %i, destination %i\n",
            name, mock->value, tag, source, destination);

    mock_spawn_children(actor);

    size = bsal_actor_size(actor);

    next = (name + 1) % size;

    printf("[mock_start] %i next is %i, sending MOCK_DIE to it\n", name, next);

    struct bsal_message message2;
    bsal_message_init(&message2, MOCK_NEW_CONTACTS, -1, -1, 3 * sizeof(int),
                    (char *)mock->children);
    bsal_actor_send(actor, next, &message2);
    bsal_message_destroy(&message2);
}

void mock_spawn_children(struct bsal_actor *actor)
{
    int total;
    struct mock *mock;
    struct bsal_message message;
    int i;

    total = 1;

    mock = (struct mock *)bsal_actor_actor(actor);
    bsal_message_init(&message, BUDDY_DIE, -1, -1, 0, NULL);

    for (i = 0; i <total; i++) {

        struct buddy *buddy_actor = mock->buddy_actors + i;
        int name;
        int tag;

        name = bsal_actor_spawn(actor, buddy_actor, &buddy_vtable);
        tag = bsal_message_tag(&message);

        printf("mock_spawn_children sending tag %i BUDDY_DIE to %i\n",
                        tag, name);

        bsal_actor_send(actor, name, &message);

        mock->children[i] = name;
    }

    bsal_message_destroy(&message);
}
