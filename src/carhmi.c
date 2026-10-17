#include "dmod.h"
#include "dmosi.h"
#include "libdmview.h"
#include "libsystemd.h"
#include "player.h"
#include "local_time.h"
#include "volume.h"
#include <errno.h>
#include <string.h>

/**
 * @brief carhmi - the car HMI demo: shows its view on a display while it runs,
 *        and plays the MP3 files of the SD card in it.
 *
 * Usage: carhmi [<view>] [<display>] [<music directory>]
 *
 * The view (assets/car_hmi.html, converted at build time) is a program of
 * its own: its pages, the climate controls, the animations and the touches
 * run in libdmview. carhmi claims the display for it (libdmview_claim()) and
 * talks to it through the claim: it sets what the player is at - the view's
 * properties track, playing, position, duration, progress, title, artist,
 * total, and the queue's rows: first, count, rowN.title / .artist / .time -,
 * and takes what was done - the events toggle, next, prev, select (a row)
 * and scroll. And the time: clock, "HH:MM" of dmutc's UTC in Polish time
 * (local_time.c), "--:--" while there is no RTC. And the volume (volume.c):
 * the properties volume (0 - 100) and muted (0 / 1), the events volume (the
 * new level) and mute.
 *
 * The queue is a window of ROWS tracks over all of them, at `first`: when
 * the list is scrolled to an end the view asks for more (scroll, +n / -n
 * tracks), the window moves and the view scrolls its rows back by as many,
 * so what is seen stays where it is. The player (player.c) plays
 * the files of <music directory>: by default `music` of the card the view is
 * on (/mnt/<card>/music), else of the first card that has one.
 *
 * It is started as a service (configs/carhmi.ini). The services start
 * together, so the view - on a card, mounted after this starts - and the
 * display are waited for.
 */

#define DEFAULT_VIEW    "/eviews/carhmi_view/carhmi.dmv"   /* dmod-boot: views of the flash modules */
#define CARD_VIEW       "views/carhmi_view/carhmi.dmv"      /* ... and of the SD card's, on any card */
#define WAIT_MS         100u
#define DISPLAY_WAIT_MS 10000u
#define VIEW_WAIT_MS    30000u
#define EVENT_WAIT_MS   250u
#define ROWS            20u             /* Of the view's queue: a window over the tracks */
#define CLOCK_MS        1000u           /* How often the RTC is read */

/*
 * The view, when none is given: the firmware's (DEFAULT_VIEW), else the one on a
 * card - automount mounts every card at /mnt/<label> (or /mnt/<node name>), so
 * each is looked at. false: none (yet)
 */
static bool find_view(char* view, size_t size)
{
    if (Dmod_FileAvailable(DEFAULT_VIEW))
    {
        Dmod_SnPrintf(view, size, "%s", DEFAULT_VIEW);
        return true;
    }
    bool found = false;
    void* mnt = Dmod_OpenDir("/mnt");
    const char* name;
    while (!found && mnt != NULL && (name = Dmod_ReadDir(mnt)) != NULL)
    {
        Dmod_SnPrintf(view, size, "/mnt/%s/%s", name, CARD_VIEW);
        found = Dmod_FileAvailable(view);
    }
    if (mnt != NULL)
        Dmod_CloseDir(mnt);
    return found;
}

/* `music` of the card the view is on, else of the first card in /mnt with one */
static void music_dir(const char* view, char* dir, size_t size)
{
    dir[0] = '\0';
    if (strncmp(view, "/mnt/", 5) == 0)
    {
        const char* end = strchr(view + 5, '/');
        if (end != NULL)
        {
            Dmod_SnPrintf(dir, size, "%.*s/music", (int)(end - view), view);
            void* d = Dmod_OpenDir(dir);
            if (d != NULL)
            {
                Dmod_CloseDir(d);
                return;
            }
        }
    }
    void* mnt = Dmod_OpenDir("/mnt");
    const char* name;
    while (mnt != NULL && (name = Dmod_ReadDir(mnt)) != NULL)
    {
        Dmod_SnPrintf(dir, size, "/mnt/%s/music", name);
        void* d = Dmod_OpenDir(dir);
        if (d != NULL)
        {
            Dmod_CloseDir(d);
            Dmod_CloseDir(mnt);
            return;
        }
    }
    if (mnt != NULL)
        Dmod_CloseDir(mnt);
    Dmod_SnPrintf(dir, size, "/mnt/music");
}

static void format_time(char* out, size_t size, uint32_t seconds)
{
    Dmod_SnPrintf(out, size, "%u:%02u", (unsigned)(seconds / 60u), (unsigned)(seconds % 60u));
}

/* The clock, into the view: read once a second, set when its text changed */
typedef struct
{
    Dmod_Timestamp_t    read_at;        /* Uptime, ms */
    char                text[8];        /* "HH:MM" */
    bool                shown;
} wall_t;

static void show_clock(uint32_t claim, wall_t* c)
{
    Dmod_Timestamp_t now = Dmod_GetUptime();
    if (c->shown && now != 0 && now - c->read_at < CLOCK_MS)
        return;                         /* (an uptime of 0: none, read every time) */
    char text[sizeof(c->text)];
    (void)local_time_text(text, sizeof(text));
    if (!c->shown || strcmp(text, c->text) != 0)
    {
        (void)libdmview_prop_set_string(claim, "clock", text);
        strcpy(c->text, text);
    }
    c->read_at = now;
    c->shown = true;
}

/* The volume, into the view: when it changed */
typedef struct
{
    int32_t     level;          /* -1: not shown yet */
    int32_t     muted;
} volume_shown_t;

static void show_volume(uint32_t claim, volume_t v, volume_shown_t* shown)
{
    int32_t level = volume_level(v), muted = volume_muted(v) ? 1 : 0;
    if (level != shown->level)
        (void)libdmview_prop_set_int(claim, "volume", level);
    if (muted != shown->muted)
        (void)libdmview_prop_set_int(claim, "muted", muted);
    shown->level = level;
    shown->muted = muted;
}

/* What the view says about the volume: true when the event was one of them */
static bool handle_volume(volume_t v, const libdmview_event_t* ev)
{
    if (strcmp(ev->name, "volume") == 0)
        volume_set(v, ev->value);
    else if (strcmp(ev->name, "mute") == 0)
        volume_toggle_mute(v);
    else
        return false;
    return true;
}

/* What the player is at, into the view: the track when it changed, the time every second */
typedef struct
{
    uint32_t    changes;
    int32_t     track;
    uint32_t    first;          /* Of the queue's window */
    uint32_t    second;
    uint32_t    duration;
    bool        playing;
    bool        shown;
} shown_t;

/* The playing track's row in the window, -1: not in it */
static int32_t row_of(const shown_t* v, int32_t track)
{
    return (track >= (int32_t)v->first && track < (int32_t)(v->first + ROWS)) ? track - (int32_t)v->first : -1;
}

/* The queue's window at `first` (as far as there are tracks): its rows, then where it is */
static void show_window(uint32_t claim, player_t p, shown_t* v, int64_t first)
{
    uint32_t total = player_count(p);
    int64_t last = (total > ROWS) ? (int64_t)(total - ROWS) : 0;
    first = (first < 0) ? 0 : (first > last) ? last : first;
    v->first = (uint32_t)first;
    uint32_t count = 0;
    for (uint32_t i = 0; i < ROWS; i++)
    {
        char name[24], time[16] = "";
        player_info_t t = { "", "", 0 };
        if (player_info(p, v->first + i, &t))
        {
            format_time(time, sizeof(time), t.duration_ms / 1000u);
            count++;
        }
        Dmod_SnPrintf(name, sizeof(name), "row%u.title", (unsigned)i);
        (void)libdmview_prop_set_string(claim, name, t.title);
        Dmod_SnPrintf(name, sizeof(name), "row%u.artist", (unsigned)i);
        (void)libdmview_prop_set_string(claim, name, t.artist);
        Dmod_SnPrintf(name, sizeof(name), "row%u.time", (unsigned)i);
        (void)libdmview_prop_set_string(claim, name, time);
    }
    (void)libdmview_prop_set_int(claim, "count", (int32_t)count);
    (void)libdmview_prop_set_int(claim, "track", row_of(v, v->track));
    (void)libdmview_prop_set_int(claim, "first", (int32_t)v->first);   /* (last: the view scrolls by it) */
}

static void show(uint32_t claim, player_t p, shown_t* v)
{
    player_state_t st;
    player_state(p, &st);
    if (!v->shown)
    {
        (void)libdmview_prop_set_int(claim, "total", (int32_t)player_count(p));
        show_window(claim, p, v, 0);
    }
    if (!v->shown || st.changes != v->changes)
    {
        if (!v->shown || st.track != v->track)
        {
            player_info_t t = { (player_count(p) == 0) ? "Brak plik\xC3\xB3w MP3" : "", "", 0 };
            if (st.track >= 0)
                (void)player_info(p, (uint32_t)st.track, &t);
            (void)libdmview_prop_set_string(claim, "title", t.title);
            (void)libdmview_prop_set_string(claim, "artist", t.artist);
            v->track = st.track;
            if (st.track >= 0 && row_of(v, st.track) < 0)
            {
                /* Out of the window (the next one, at its end): the window moves to it */
                int64_t first = (st.track < (int32_t)v->first) ? st.track : (int64_t)st.track - (int64_t)ROWS + 1;
                show_window(claim, p, v, first);
            }
            else
                (void)libdmview_prop_set_int(claim, "track", row_of(v, st.track));
        }
        if (!v->shown || st.playing != v->playing)
            (void)libdmview_prop_set_int(claim, "playing", st.playing ? 1 : 0);
        v->changes = st.changes;
        v->playing = st.playing;
    }
    uint32_t second = st.position_ms / 1000u, duration = st.duration_ms / 1000u;
    if (!v->shown || second != v->second || duration != v->duration)
    {
        (void)libdmview_prop_set_int(claim, "position", (int32_t)second);
        (void)libdmview_prop_set_int(claim, "duration", (int32_t)duration);
        (void)libdmview_prop_set_int(claim, "progress",
                                     (st.duration_ms > 0) ? (int32_t)((uint64_t)st.position_ms * 1000u / st.duration_ms) : 0);
        v->second = second;
        v->duration = duration;
    }
    v->shown = true;
}

/* What was done in the view */
static void handle(uint32_t claim, player_t p, const libdmview_event_t* ev, shown_t* v)
{
    if (strcmp(ev->name, "toggle") == 0)
        player_toggle(p);
    else if (strcmp(ev->name, "next") == 0)
        player_next(p, 1);
    else if (strcmp(ev->name, "prev") == 0)
        player_next(p, -1);
    else if (strcmp(ev->name, "select") == 0 && ev->value >= 0)
        player_play(p, v->first + (uint32_t)ev->value);
    else if (strcmp(ev->name, "scroll") == 0 && v->shown)
        show_window(claim, p, v, (int64_t)v->first + ev->value);     /* The list at an end: more of it */
}

int main(int argc, char* argv[])
{
    bool given = argc > 1 && argv[1][0] != '\0';
    char view[160];
    Dmod_SnPrintf(view, sizeof(view), "%s", given ? argv[1] : DEFAULT_VIEW);
    const char* display = (argc > 2 && argv[2][0] != '\0') ? argv[2] : NULL;

    dmosi_semaphore_t wakeup = dmosi_semaphore_create(0, 1);
    if (wakeup == NULL)
        return -ENOMEM;
    libsystemd_set_stop_semaphore(wakeup);

    int ret = -ENOENT;
    for (uint32_t waited = 0; !libsystemd_stop_requested(); waited += WAIT_MS)
    {
        if (given ? Dmod_FileAvailable(view) : find_view(view, sizeof(view)))
        {
            ret = 0;
            break;
        }
        if (waited >= VIEW_WAIT_MS)
        {
            if (given)
            {
                DMOD_LOG_ERROR("carhmi: no view %s\n", view);
            }
            else
            {
                DMOD_LOG_ERROR("carhmi: no view - neither %s nor /mnt/<card>/%s\n", DEFAULT_VIEW, CARD_VIEW);
            }
            Dmod_Printf("Usage: carhmi [<view>] [<display>] [<music directory>]\n");
            break;
        }
        (void)dmosi_semaphore_wait(wakeup, 1, (int32_t)WAIT_MS);
    }

    uint32_t claim = 0;
    if (ret == 0)
    {
        ret = -ENODEV;
        for (uint32_t waited = 0; !libsystemd_stop_requested(); waited += WAIT_MS)
        {
            ret = libdmview_claim(display, view, &claim);
            if (ret != -ENODEV || waited >= DISPLAY_WAIT_MS)
                break;
            (void)dmosi_semaphore_wait(wakeup, 1, (int32_t)WAIT_MS);
        }
        if (ret == 0)
        {
            char dir[128];
            if (argc > 3 && argv[3][0] != '\0')
                Dmod_SnPrintf(dir, sizeof(dir), "%s", argv[3]);
            else
                music_dir(view, dir, sizeof(dir));
            player_t p = player_new(dir);
            DMOD_LOG_INFO("carhmi: showing %s, %u MP3 file(s) of %s\n", view,
                          (unsigned)((p != NULL) ? player_count(p) : 0u), dir);
            shown_t shown;
            memset(&shown, 0, sizeof(shown));
            wall_t wall;
            memset(&wall, 0, sizeof(wall));
            volume_t volume = volume_new(dir);
            volume_shown_t volume_shown = { -1, -1 };
            bool volume_logged = false;
            if (p != NULL && player_count(p) > 0)
                player_play(p, 0);
            while (!libsystemd_stop_requested())
            {
                libdmview_event_t ev;
                int got = libdmview_wait_event(claim, &ev, (int32_t)EVENT_WAIT_MS);
                if (got == -ENOENT)
                    break;                      /* (the claim is gone - the display with it) */
                show_clock(claim, &wall);
                bool taken = false;
                if (volume != NULL)
                {
                    taken = got == 0 && handle_volume(volume, &ev);
                    if (volume_poll(volume, Dmod_GetUptime()) && !volume_logged)
                    {
                        DMOD_LOG_INFO("carhmi: volume %u%% set\n", (unsigned)volume_level(volume));
                        volume_logged = true;
                    }
                    show_volume(claim, volume, &volume_shown);
                }
                if (p == NULL)
                    continue;
                if (got == 0 && !taken)
                    handle(claim, p, &ev, &shown);
                show(claim, p, &shown);
            }
            volume_free(volume);
            player_free(p);
            (void)libdmview_release(claim);
        }
        else if (ret == -ENODEV)
        {
            DMOD_LOG_ERROR("carhmi: no display %s\n", (display != NULL) ? display : "is served by dmview");
        }
        else
        {
            DMOD_LOG_ERROR("carhmi: cannot claim the display (%d)\n", ret);
        }
    }

    libsystemd_set_stop_semaphore(NULL);
    dmosi_semaphore_destroy(wakeup);
    return ret;
}
