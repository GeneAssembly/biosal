
#ifndef THORIUM_MULTIPLEXED_BUFFER_H
#define THORIUM_MULTIPLEXED_BUFFER_H

#include <stdint.h>

#ifdef THORIUM_MULTIPLEXED_BUFFER_PREDICT_MESSAGE_COUNT
#define PREDICTION_EVENT_COUNT 16
#endif

/*
 * A multiplexed buffer.
 */
struct thorium_multiplexed_buffer {
    void *buffer_;
    uint64_t timestamp_;
    int current_size_;
    int maximum_size_;
    int message_count_;
    int timeout_;

#ifdef THORIUM_MULTIPLEXED_BUFFER_PREDICT_MESSAGE_COUNT
    int predicted_message_count_;
    int prediction_iterator;
    int prediction_ages[PREDICTION_EVENT_COUNT];
    int prediction_message_count[PREDICTION_EVENT_COUNT];
    int prediction_buffer_sizes[PREDICTION_EVENT_COUNT];
#endif
};

void thorium_multiplexed_buffer_init(struct thorium_multiplexed_buffer *self,
                int maximum_size, int timeout);
void thorium_multiplexed_buffer_destroy(struct thorium_multiplexed_buffer *self);

void thorium_multiplexed_buffer_print(struct thorium_multiplexed_buffer *self);
uint64_t thorium_multiplexed_buffer_time(struct thorium_multiplexed_buffer *self);
int thorium_multiplexed_buffer_current_size(struct thorium_multiplexed_buffer *self);
int thorium_multiplexed_buffer_maximum_size(struct thorium_multiplexed_buffer *self);

void thorium_multiplexed_buffer_append(struct thorium_multiplexed_buffer *self,
                int count, void *buffer);
int thorium_multiplexed_buffer_required_size(struct thorium_multiplexed_buffer *self,
                int count);
void thorium_multiplexed_buffer_set_time(struct thorium_multiplexed_buffer *self,
                uint64_t time);

void *thorium_multiplexed_buffer_buffer(struct thorium_multiplexed_buffer *self);
void thorium_multiplexed_buffer_set_buffer(struct thorium_multiplexed_buffer *self,
                void *buffer);

void thorium_multiplexed_buffer_reset(struct thorium_multiplexed_buffer *self);

void thorium_multiplexed_buffer_profile(struct thorium_multiplexed_buffer *self, uint64_t time);

int thorium_multiplexed_buffer_timeout(struct thorium_multiplexed_buffer *self);

#endif
