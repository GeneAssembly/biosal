
#include "log.h"

#include <engine/thorium/actor.h>

#include <core/system/timer.h>

#include <stdio.h>

void thorium_actor_log(struct thorium_actor *self, const char *format, ...)
{
    va_list arguments;
    FILE *stream;
    char *script_name;
    int name;
    struct core_timer timer;

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

    stream = stdout;

    /*
     * \see http://stackoverflow.com/questions/1516370/wrapper-printf-function-that-filters-according-to-user-preferences
     */

    va_start(arguments, format);

    fprintf(stream, "%s [ACTOR_LOG] %s %d ... ",
                    time_string, script_name, name);

    vfprintf(stream, format, arguments);

    fprintf(stream, "\n");

    fflush(stream);

    va_end(arguments);
}
