#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "local_time.h"
#include "volume.h"
#include <string.h>

/* "YYYY-MM-DD HH:MM:SS" of the local time of a UTC calendar */
static const char* local_of(int32_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    static char text[32];
    dmutc_calendar_t c;
    memset(&c, 0, sizeof(c));
    c.year = year;
    c.month = month;
    c.day = day;
    c.hour = hour;
    c.minute = minute;
    c.second = second;
    dmutc_time_t t;
    dmutc_calendar_t l;
    if (dmutc_from_calendar(&c, &t) != 0 || local_time(&t, &l) != 0)
        return "error";
    Dmod_SnPrintf(text, sizeof(text), "%04d-%02u-%02u %02u:%02u:%02u", (int)l.year, (unsigned)l.month,
                  (unsigned)l.day, (unsigned)l.hour, (unsigned)l.minute, (unsigned)l.second);
    return text;
}

DMOD_TEST_STEP(local_time_is_polish_summer_and_winter_time)
{
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 10, 17, 12, 0, 0), "2026-10-17 14:00:00") == 0);   /* CEST */
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 1, 15, 12, 0, 0), "2026-01-15 13:00:00") == 0);    /* CET */
}

DMOD_TEST_STEP(local_time_changes_on_the_last_sundays_at_one_utc)
{
    /* 2026: March 29th and October 25th */
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 3, 29, 0, 59, 59), "2026-03-29 01:59:59") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 3, 29, 1, 0, 0), "2026-03-29 03:00:00") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 10, 25, 0, 59, 59), "2026-10-25 02:59:59") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 10, 25, 1, 0, 0), "2026-10-25 02:00:00") == 0);
    /* 2027: March 28th, October 31st (a Sunday itself) */
    DMOD_TEST_EXPECT(strcmp(local_of(2027, 3, 28, 1, 0, 0), "2027-03-28 03:00:00") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2027, 10, 31, 0, 30, 0), "2027-10-31 02:30:00") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2027, 10, 31, 1, 30, 0), "2027-10-31 02:30:00") == 0);
}

DMOD_TEST_STEP(local_time_goes_into_the_next_year)
{
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 12, 31, 23, 30, 0), "2027-01-01 00:30:00") == 0);
}

/* A directory the test may write to */
#define VOLUME_DIR "/tmp"

DMOD_TEST_STEP(volume_starts_at_the_default_and_stays_in_range)
{
    (void)Dmod_FileRemove(VOLUME_DIR "/.volume");
    volume_t v = volume_new(VOLUME_DIR);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    DMOD_TEST_EXPECT(volume_level(v) == VOLUME_DEFAULT);
    DMOD_TEST_EXPECT(!volume_muted(v));

    volume_set(v, 30);
    DMOD_TEST_EXPECT(volume_level(v) == 30);
    volume_set(v, 250);
    DMOD_TEST_EXPECT(volume_level(v) == 100);
    volume_set(v, -5);
    DMOD_TEST_EXPECT(volume_level(v) == 0);
    volume_free(v);
}

DMOD_TEST_STEP(volume_mute_is_turned_off_by_a_new_level)
{
    volume_t v = volume_new(NULL);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    volume_toggle_mute(v);
    DMOD_TEST_EXPECT(volume_muted(v));
    DMOD_TEST_EXPECT(volume_level(v) == VOLUME_DEFAULT);        /* (the level is kept) */
    volume_toggle_mute(v);
    DMOD_TEST_EXPECT(!volume_muted(v));
    volume_toggle_mute(v);
    volume_set(v, 40);
    DMOD_TEST_EXPECT(!volume_muted(v));
    volume_free(v);
}

DMOD_TEST_STEP(volume_is_kept_between_starts)
{
    (void)Dmod_FileRemove(VOLUME_DIR "/.volume");
    volume_t v = volume_new(VOLUME_DIR);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    volume_set(v, 35);
    volume_free(v);                                             /* (writes what was not written) */

    v = volume_new(VOLUME_DIR);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    DMOD_TEST_EXPECT(volume_level(v) == 35);
    DMOD_TEST_EXPECT(!volume_muted(v));
    volume_free(v);
    (void)Dmod_FileRemove(VOLUME_DIR "/.volume");
}

DMOD_TEST_STEP(volume_without_a_device_is_not_applied)
{
    volume_t v = volume_new(NULL);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    DMOD_TEST_EXPECT(!volume_poll(v, 1000));                    /* (no audio device is served here) */
    volume_free(v);
}
