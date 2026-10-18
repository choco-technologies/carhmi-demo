#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "local_time.h"
#include "volume.h"
#include "covers.h"
#include "player.h"
#include "playstate.h"
#include "dmosi.h"
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

/*
 * The covers of tests/fixtures/music (made with ffmpeg): a.mp3 with a 120 x 80
 * JPEG (ID3v2.3), b.mp3 without a picture, c.mp3 with a 48 x 48 PNG (ID3v2.4)
 */
#define COVERS_DIR      "/tmp/carhmi-covers"
#define COVERS_SIZES    "-40x40-k0-0-40x40-r5.dmvi -100x100-b10.dmvi"
#define COVERS_WAIT_MS  20000u

/* The key of a track's cover once it is made; 0: not in time */
static uint32_t wait_cover(covers_t c, uint32_t index)
{
    for (uint32_t waited = 0; waited < COVERS_WAIT_MS; waited += 20u)
    {
        uint32_t key = covers_key(c, index);
        if (key != 0)
            return key;
        dmosi_thread_sleep(20);
    }
    return 0;
}

/* The size of a .dmvi of the cover `key`; false: no such file */
static bool cover_size(uint32_t key, const char* tail, uint32_t* w, uint32_t* h)
{
    char path[96];
    uint8_t header[16];
    Dmod_SnPrintf(path, sizeof(path), "%s/%08x%s", COVERS_DIR, (unsigned)key, tail);
    void* f = Dmod_FileOpen(path, "rb");
    bool ok = f != NULL && Dmod_FileRead(header, 1, sizeof(header), f) == sizeof(header) && memcmp(header, "DMVI", 4) == 0;
    if (f != NULL)
        Dmod_FileClose(f);
    *w = ok ? (uint32_t)(header[12] | (header[13] << 8)) : 0u;
    *h = ok ? (uint32_t)(header[14] | (header[15] << 8)) : 0u;
    return ok;
}

static void remove_covers(uint32_t key)
{
    char path[96];
    Dmod_SnPrintf(path, sizeof(path), "%s/%08x-40x40-k0-0-40x40-r5.dmvi", COVERS_DIR, (unsigned)key);
    (void)Dmod_FileRemove(path);
    Dmod_SnPrintf(path, sizeof(path), "%s/%08x-100x100-b10.dmvi", COVERS_DIR, (unsigned)key);
    (void)Dmod_FileRemove(path);
}

DMOD_TEST_STEP(covers_are_made_at_the_sizes_of_the_view)
{
    player_t p = player_new(CARHMI_FIXTURES "/music");
    DMOD_TEST_EXPECT(p != NULL && player_count(p) == 3);
    if (p == NULL)
        return;
    covers_t c = covers_new(p, COVERS_DIR, COVERS_SIZES);
    DMOD_TEST_EXPECT(c != NULL);
    if (c == NULL)
    {
        player_free(p);
        return;
    }
    covers_want(c, 1, false);
    covers_want(c, 2, false);
    covers_want(c, 0, true);                                    /* Before the others */
    uint32_t jpeg = wait_cover(c, 0), png = wait_cover(c, 2);
    DMOD_TEST_EXPECT(jpeg != 0 && png != 0 && jpeg != png);
    dmosi_thread_sleep(200);
    DMOD_TEST_EXPECT(covers_key(c, 1) == 0);                    /* No picture */
    DMOD_TEST_EXPECT(covers_key(c, 7) == 0);                    /* No track */

    /* Each size: the middle of the 120 x 80 picture covering it, resized to it, cut, blurred */
    uint32_t w = 0, h = 0;
    DMOD_TEST_EXPECT(cover_size(jpeg, "-40x40-k0-0-40x40-r5.dmvi", &w, &h) && w == 40 && h == 40);
    DMOD_TEST_EXPECT(cover_size(jpeg, "-100x100-b10.dmvi", &w, &h) && w == 100 && h == 100);   /* (blurred: scaled up) */
    DMOD_TEST_EXPECT(cover_size(png, "-40x40-k0-0-40x40-r5.dmvi", &w, &h) && w == 40 && h == 40);
    char name[64];
    DMOD_TEST_EXPECT(covers_name_size(c) == strlen(COVERS_DIR) + 10U);
    covers_name(c, jpeg, name, sizeof(name));
    char expected[64];
    Dmod_SnPrintf(expected, sizeof(expected), "%s/%08x", COVERS_DIR, (unsigned)jpeg);
    DMOD_TEST_EXPECT(strcmp(name, expected) == 0);
    covers_free(c);

    /* Made once: the next start finds them */
    c = covers_new(p, COVERS_DIR, COVERS_SIZES);
    DMOD_TEST_EXPECT(c != NULL);
    if (c != NULL)
    {
        covers_want(c, 0, true);
        DMOD_TEST_EXPECT(wait_cover(c, 0) == jpeg);
        covers_free(c);
    }
    remove_covers(jpeg);
    remove_covers(png);
    player_free(p);
}

DMOD_TEST_STEP(covers_need_sizes_they_know)
{
    player_t p = player_new(CARHMI_FIXTURES "/music");
    DMOD_TEST_EXPECT(p != NULL);
    DMOD_TEST_EXPECT(covers_new(p, COVERS_DIR, "") == NULL);
    DMOD_TEST_EXPECT(covers_new(p, COVERS_DIR, "-40y40.dmvi -b4") == NULL);
    covers_t c = covers_new(p, COVERS_DIR, "-40x40.dmvi nonsense -40x40.dmvi");     /* The one it knows, once */
    DMOD_TEST_EXPECT(c != NULL);
    covers_free(c);
    covers_free(NULL);
    player_free(p);
}

/* Where the music was: kept in /tmp/.playing */
#define PLAYSTATE_DIR   "/tmp"

static bool kept(const char* file, uint32_t position_ms, bool playing)
{
    playstate_t s = playstate_new(PLAYSTATE_DIR);
    const char* f = NULL;
    uint32_t ms = 0;
    bool on = false;
    bool ok = s != NULL && playstate_saved(s, &f, &ms, &on) && strcmp(f, file) == 0 && ms == position_ms && on == playing;
    if (!ok)
        Dmod_Printf("    kept: %s at %u ms, %d\n", (f != NULL) ? f : "-", (unsigned)ms, (int)on);
    playstate_free(s);
    return ok;
}

DMOD_TEST_STEP(playstate_keeps_the_track_and_where_it_was)
{
    (void)Dmod_FileRemove(PLAYSTATE_DIR "/.playing");
    playstate_t s = playstate_new(PLAYSTATE_DIR);
    const char* f = NULL;
    uint32_t ms = 0;
    bool on = false;
    DMOD_TEST_EXPECT(s != NULL && !playstate_saved(s, &f, &ms, &on));          /* Nothing kept yet */
    playstate_update(s, NULL, 0, false, 500);                                   /* (no track: nothing) */
    playstate_update(s, "Żółć - a song.mp3", 12345, true, 1000);                /* Another track: at once */
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 12000, true));                   /* (seconds) */
    playstate_update(s, "Żółć - a song.mp3", 20000, true, 1000 + PLAYSTATE_SAVE_MS - 1u);
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 12000, true));                   /* Not yet */
    playstate_update(s, "Żółć - a song.mp3", 21000, true, 1000 + PLAYSTATE_SAVE_MS);
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 21000, true));                   /* Every PLAYSTATE_SAVE_MS */
    playstate_update(s, "Żółć - a song.mp3", 22000, false, 1000 + PLAYSTATE_SAVE_MS + 10u);
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 22000, false));                  /* Paused: at once */
    playstate_update(s, "Żółć - a song.mp3", 23000, false, 1000 + PLAYSTATE_SAVE_MS + 20u);
    playstate_free(s);                                                          /* What was not written */
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 23000, false));
    (void)Dmod_FileRemove(PLAYSTATE_DIR "/.playing");
    playstate_free(NULL);
}

DMOD_TEST_STEP(player_finds_a_track_by_its_file)
{
    player_t p = player_new(CARHMI_FIXTURES "/music");
    DMOD_TEST_EXPECT(p != NULL);
    if (p == NULL)
        return;
    DMOD_TEST_EXPECT(player_find(p, "c.mp3") == 2);
    DMOD_TEST_EXPECT(player_find(p, "a.mp3") == 0);
    DMOD_TEST_EXPECT(player_find(p, "gone.mp3") == -1);
    DMOD_TEST_EXPECT(player_find(p, NULL) == -1);
    DMOD_TEST_EXPECT(player_file(p, 1) != NULL && strcmp(player_file(p, 1), "b.mp3") == 0);
    DMOD_TEST_EXPECT(player_file(p, 3) == NULL);
    player_free(p);
}
