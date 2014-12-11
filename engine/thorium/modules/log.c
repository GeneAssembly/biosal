
#include "log.h"

#include <engine/thorium/actor.h>

#include <core/system/timer.h>
#include <core/system/memory_pool.h>
#include <core/system/debugger.h>
#include <core/helpers/bitmap.h>

#include <stdio.h>

#define OPERATION_ENABLE    99
#define OPERATION_DISABLE   2

void thorium_actor_log_private(struct thorium_actor *self, int level, const char *format,
                va_list arguments);

void thorium_actor_change_log_level(struct thorium_actor *self, int operation,
                struct thorium_message *message);

void thorium_actor_log_implementation(struct thorium_actor *self, const char *format, ...)
{
    va_list arguments;
    int level;

    /*
     * Ideally, we would call thorium_actor_log_with_level() directly.
     * However, it is unclear how to call a variadic function from a
     * variadic function.
     *
     * @see http://stackoverflow.com/questions/150543/forward-an-invocation-of-a-variadic-function-in-c
     * @see http://c-faq.com/varargs/handoff.html
     */
    level = LOG_LEVEL_DEFAULT;

    va_start(arguments, format);
    thorium_actor_log_private(self, level, format, arguments);
    va_end(arguments);
}

void thorium_actor_log_with_level_implementation(struct thorium_actor *self, int level, const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    thorium_actor_log_private(self, level, format, arguments);
    va_end(arguments);
}

void thorium_actor_log_private(struct thorium_actor *self, int level, const char *format,
                va_list arguments)
{
    FILE *stream;
    char *script_name;
    int name;
    struct core_timer timer;
    va_list arguments2;

    /*
     * For the time, we adopt the format used by LTTng:
     * [18:10:27.684304496]
     */
    char time_string[21];
    time_t raw_time;
    struct tm timeinfo;
    int hour;
    int minute;
    int second;
    uint64_t raw_nanosecond;
    int nanosecond;
    int required;
    char *buffer;
    int offset;
    struct core_memory_pool *memory_pool;

    if (!thorium_actor_get_flag(self, level)) {
        return;
    }

    /*
     * va_copy is available in C 1999.
     */
    va_copy(arguments2, arguments);

    /*
     * \see http://www.cplusplus.com/reference/ctime/localtime/
     */
    time(&raw_time);

    /*
     * localtime is not thread safe.
     * But localtime_r is.
     *
     * \see http://pubs.opengroup.org/onlinepubs/9699919799/functions/localtime.html
     */

    localtime_r(&raw_time, &timeinfo);
    hour = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    second = timeinfo.tm_sec;

    core_timer_init(&timer);
    raw_nanosecond = core_timer_get_nanoseconds(&timer);
    core_timer_destroy(&timer);
    nanosecond = raw_nanosecond % (1000 * 1000 * 1000);

    /*
     * \see http://www.cplusplus.com/reference/ctime/tm/
     */
    sprintf(time_string, "[%02d:%02d:%02d.%09d]",
                    hour, minute, second, nanosecond);

    name = thorium_actor_name(self);
    script_name = thorium_actor_script_name(self);

    stream = stderr;

    /*
     * \see http://stackoverflow.com/questions/1516370/wrapper-printf-function-that-filters-according-to-user-preferences
     */

    required = 0;

    required += snprintf(NULL, 0, "%s ACTOR %s %d : ",
                    time_string, script_name, name);
    required += vsnprintf(NULL, 0, format, arguments);
    required += snprintf(NULL, 0, "\n");

    /*
     * null character.
     */
    required += 1;

    memory_pool = thorium_actor_get_memory_pool(self,
                    MEMORY_POOL_NAME_ABSTRACT_ACTOR);

    CORE_DEBUGGER_ASSERT(memory_pool != NULL);

    buffer = core_memory_pool_allocate(memory_pool, required);
    offset = 0;

    offset += sprintf(buffer + offset, "%s ACTOR %s %d : ",
                    time_string, script_name, name);
    offset += vsprintf(buffer + offset, format, arguments2);
    offset += sprintf(buffer + offset, "\n");

    CORE_DEBUGGER_ASSERT(offset + 1 == required);

    fwrite(buffer, sizeof(char), offset, stream);

    core_memory_pool_free(memory_pool, buffer);

    va_end(arguments2);
}

void thorium_actor_enable_log_level(struct thorium_actor *self, struct thorium_message *message)
{
    thorium_actor_change_log_level(self, OPERATION_ENABLE, message);
}

void thorium_actor_disable_log_level(struct thorium_actor *self, struct thorium_message *message)
{
    thorium_actor_change_log_level(self, OPERATION_DISABLE, message);
}

void thorium_actor_change_log_level(struct thorium_actor *self, int operation,
                struct thorium_message *message)
{
    int log_level;
    int count;

    count = thorium_message_count(message);

    CORE_DEBUGGER_ASSERT(count == sizeof(log_level));

    if (count < (int)sizeof(log_level))
        return;

    thorium_message_unpack_int(message, 0, &log_level);

    /*
     * Validate the log level.
     */
    if (!(log_level == LOG_LEVEL_DEFAULT)) {
        return;
    }

    if (operation == OPERATION_ENABLE) {
        thorium_actor_set_flag(self, log_level);
    } else if (operation == OPERATION_DISABLE) {
        thorium_actor_clear_flag(self, log_level);
    }
}
