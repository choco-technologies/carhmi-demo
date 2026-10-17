#include "local_time.h"
#include "dmod.h"
#include <string.h>

#define HOUR    3600

/* The last Sunday of a month (of 31 days: March, October) at 01:00 UTC. 0, else dmutc's error */
static int last_sunday(int32_t year, uint8_t month, int64_t* seconds)
{
    dmutc_calendar_t c;
    memset(&c, 0, sizeof(c));
    c.year = year;
    c.month = month;
    c.day = 31;
    c.hour = 1;
    dmutc_time_t t;
    int rc = dmutc_from_calendar(&c, &t);
    if (rc == 0)
        rc = dmutc_to_calendar(&t, &c);         /* Its weekday: Monday 1 .. Sunday 7 */
    if (rc == 0)
        *seconds = t.seconds - (int64_t)(c.weekday % 7u) * 24 * HOUR;
    return rc;
}

int local_time(const dmutc_time_t* utc, dmutc_calendar_t* local)
{
    int rc = dmutc_to_calendar(utc, local);
    int64_t begin = 0, end = 0;
    if (rc == 0)
        rc = last_sunday(local->year, 3, &begin);
    if (rc == 0)
        rc = last_sunday(local->year, 10, &end);
    if (rc == 0)
    {
        dmutc_time_t t = *utc;
        t.seconds += (utc->seconds >= begin && utc->seconds < end) ? 2 * HOUR : HOUR;
        rc = dmutc_to_calendar(&t, local);
    }
    if (rc != 0)
        memset(local, 0, sizeof(*local));
    return rc;
}

int local_time_text(char* out, size_t size)
{
    dmutc_time_t now;
    dmutc_calendar_t c;
    int rc = dmutc_get(&now);
    if (rc == 0)
        rc = local_time(&now, &c);
    if (rc == 0)
        Dmod_SnPrintf(out, size, "%02u:%02u", (unsigned)c.hour, (unsigned)c.minute);
    else
        Dmod_SnPrintf(out, size, "--:--");
    return rc;
}
