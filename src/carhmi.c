#include "dmod.h"
#include "dmosi.h"
#include "libdmview.h"
#include "libsystemd.h"
#include "player.h"
#include "local_time.h"
#include "volume.h"
#include "covers.h"
#include "playstate.h"
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
 * and takes what was done - the events toggle, next, prev, select (a row),
 * scroll and jump. And the time: clock, "HH:MM" of dmutc's UTC in Polish time
 * (local_time.c), "--:--" while there is no RTC. And the volume (volume.c):
 * the properties volume (0 - 100) and muted (0 / 1), the events volume (the
 * new level) and mute. And the album covers (covers.c): the properties cover
 * (the playing track's) and rowN.cover name the images the view shows - a
 * cover once it is made, else the view's own; what to make the view says
 * (cover.sizes, row0.cover.sizes); covers.done of covers.total tracks are
 * done. Where it was in the music is kept next to
 * it (playstate.c): after a reset the same track goes on from there, the
 * queue's window at it.
 *
 * The queue is a window of ROWS tracks over all of them, at `first`: when
 * the list is scrolled to an end the view asks for more (scroll, +n / -n
 * tracks), the window moves and the view scrolls its rows back by as many,
 * so what is seen stays where it is. When carhmi moves it itself - at the
 * start, to the playing track out of it - the track has LEAD rows before it
 * in the window (what is before it is there to be scrolled to) and the view
 * scrolls it to the top; so does a track the view's position bar was
 * dragged to (jump, the track). The player (player.c) plays
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
#define LEAD            10u             /* Rows before the track the window is moved to (as far as there are) */
#define CLOCK_MS        1000u           /* How often the RTC is read */
#define COVERS_DIR      ".covers"       /* Of the music directory: the covers made */
#define PROP_TEXT       256u            /* Bytes of a property's text read from the view, at most */
#define COVER_PLACES    (ROWS + 1u)     /* The playing track's cover, the rows' */
#define NOT_SHOWN       0xFFFFFFFFu

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
    player_prefetch(p, v->first);

    /* Every row's tags first (read from the card when they are not yet), then
     * the whole window at once: the rows and where it is together, so the
     * view never shows the new rows at the old place */
    for (uint32_t i = 0; i < ROWS; i++)
    {
        player_info_t t;
        (void)player_info(p, v->first + i, &t);
    }
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
        v->track = st.track;                        /* (its row: what the view scrolls to the top) */
        show_window(claim, p, v, (int64_t)st.track - (int64_t)LEAD);
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
                show_window(claim, p, v, (int64_t)st.track - (int64_t)LEAD);
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

/* The covers, into the view: at each place the key shown (0: the view's own image) */
typedef struct
{
    covers_t    covers;
    char*       name;                   /* A cover's name, for the view (covers_name_size()) */
    char*       own;                    /* The view's own image: what its property cover starts with */
    uint32_t    keys[COVER_PLACES];     /* NOT_SHOWN: nothing set yet */
    int32_t     done;                   /* Tracks done (covers.done), -1: not shown yet */
} covers_shown_t;

/* A property's text, on the heap (Dmod_Free() it); NULL: the view has no such property */
static char* prop_text(uint32_t claim, const char* name)
{
    char* text = Dmod_Malloc(PROP_TEXT);
    if (text != NULL && libdmview_prop_get_string(claim, name, text, PROP_TEXT) != 0)
    {
        Dmod_Free(text);
        text = NULL;
    }
    return text;
}

/* The covers of the tracks, in `music`/COVERS_DIR at the sizes the view shows them at; false: none (an older view) */
static bool start_covers(uint32_t claim, player_t p, const char* music, covers_shown_t* cs)
{
    memset(cs, 0, sizeof(*cs));
    for (uint32_t i = 0; i < COVER_PLACES; i++)
        cs->keys[i] = NOT_SHOWN;
    cs->done = -1;
    char* sizes = prop_text(claim, "cover.sizes");
    char* rows = prop_text(claim, "row0.cover.sizes");          /* (the rows are alike) */
    cs->own = prop_text(claim, "cover");
    size_t dir_size = strlen(music) + sizeof("/" COVERS_DIR);
    char* dir = Dmod_Malloc(dir_size);
    char* all = (sizes != NULL) ? Dmod_Malloc(strlen(sizes) + 1U + ((rows != NULL) ? strlen(rows) : 0u) + 1U) : NULL;
    if (dir != NULL && all != NULL && cs->own != NULL && p != NULL)
    {
        Dmod_SnPrintf(dir, dir_size, "%s/%s", music, COVERS_DIR);
        strcpy(all, sizes);
        if (rows != NULL)
        {
            strcat(all, " ");
            strcat(all, rows);
        }
        cs->covers = covers_new(p, dir, all);
        cs->name = (cs->covers != NULL) ? Dmod_Malloc(covers_name_size(cs->covers)) : NULL;
        if (cs->covers != NULL)
            DMOD_LOG_INFO("carhmi: covers in %s (%s)\n", dir, all);
    }
    if (sizes != NULL)
        Dmod_Free(sizes);
    if (rows != NULL)
        Dmod_Free(rows);
    if (dir != NULL)
        Dmod_Free(dir);
    if (all != NULL)
        Dmod_Free(all);
    if (cs->name == NULL)
    {
        covers_free(cs->covers);
        cs->covers = NULL;
    }
    return cs->covers != NULL;
}

static void stop_covers(covers_shown_t* cs)
{
    covers_free(cs->covers);
    if (cs->name != NULL)
        Dmod_Free(cs->name);
    if (cs->own != NULL)
        Dmod_Free(cs->own);
}

/* The cover of `track` (-1: none) at a place, asked for (`first`: before the others) */
static void show_cover(uint32_t claim, covers_shown_t* cs, uint32_t place, const char* prop, int64_t track, bool first)
{
    uint32_t key = 0;
    if (track >= 0)
    {
        covers_want(cs->covers, (uint32_t)track, first);
        key = covers_key(cs->covers, (uint32_t)track);
    }
    if (key == cs->keys[place])
        return;
    if (key != 0)
        covers_name(cs->covers, key, cs->name, covers_name_size(cs->covers));
    (void)libdmview_prop_set_string(claim, prop, (key != 0) ? cs->name : cs->own);
    cs->keys[place] = key;
}

/* The playing track's cover, the rows', and how many of the tracks are done */
static void show_covers(uint32_t claim, player_t p, const shown_t* v, covers_shown_t* cs)
{
    if (cs->covers == NULL || !v->shown)
        return;
    int32_t done = (int32_t)covers_done(cs->covers);
    if (cs->done < 0)
        (void)libdmview_prop_set_int(claim, "covers.total", (int32_t)player_count(p));
    if (done != cs->done)
        (void)libdmview_prop_set_int(claim, "covers.done", done);
    cs->done = done;
    show_cover(claim, cs, 0, "cover", v->track, true);
    uint32_t total = player_count(p);
    for (uint32_t i = 0; i < ROWS; i++)
    {
        char name[24];
        uint32_t track = v->first + i;
        Dmod_SnPrintf(name, sizeof(name), "row%u.cover", (unsigned)i);
        show_cover(claim, cs, 1u + i, name, (track < total) ? (int64_t)track : -1, false);
    }
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
    else if (strcmp(ev->name, "jump") == 0 && v->shown)
        show_window(claim, p, v, (int64_t)ev->value - (int64_t)LEAD); /* The position bar dragged: that track, at the top */
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
            covers_shown_t covers;
            (void)start_covers(claim, p, dir, &covers);
            volume_t volume = volume_new(dir);
            volume_shown_t volume_shown = { -1, -1 };
            bool volume_logged = false;
            playstate_t resume = playstate_new(dir);
            const char* file = NULL;
            uint32_t position_ms = 0;
            bool playing = true;
            int32_t start = -1;
            if (p != NULL && playstate_saved(resume, &file, &position_ms, &playing) && (start = player_find(p, file)) >= 0)
            {
                DMOD_LOG_INFO("carhmi: on with %s at %u s\n", file, (unsigned)(position_ms / 1000u));
                player_play_at(p, (uint32_t)start, position_ms, playing);
            }
            else if (p != NULL && player_count(p) > 0)
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
                show_covers(claim, p, &shown, &covers);
                player_state_t st;
                player_state(p, &st);
                playstate_update(resume, (st.track >= 0) ? player_file(p, (uint32_t)st.track) : NULL, st.position_ms,
                                 st.playing, Dmod_GetUptime());
            }
            /* The threads are asked to stop all at once, then waited for: the waits overlap (the covers' one
             * finishes the cover it is on, the tags' one the file it reads) */
            covers_stop(covers.covers);
            player_stop(p);
            stop_covers(&covers);
            playstate_free(resume);
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
