#include "player.h"
#include "dmosi.h"
#include "dmmp3.h"
#include "libdmaudio.h"
#include "dmlist.h"
#include <errno.h>
#include <string.h>

/*
 * The player's thread decodes the current file and writes it into a stream
 * of libdmaudio; the write waits while the stream's buffer is full, so the
 * device's clock paces the decoding. The rest of carhmi only asks it to play,
 * pause, go on (under `lock`) and reads where it is.
 */

#define CHUNK_FRAMES    1152u           /* An MP3 frame */
#define BUFFER_MS       500u            /* Of the stream: the card's and the drawing's delays */
#define THREAD_STACK    4096u           /* (the decoder's memory is on the heap) */
#define THREAD_PRIORITY 2               /* Below the audio service (3), above the views (1) */
#define IDLE_MS         100u
#define TAGS_STACK      4096u
#define TAGS_PRIORITY   0               /* Only when nothing else wants the CPU (it mostly waits for the card) */
#define TAGS_IDLE_MS    1000u
#define WRITE_MS        200u

/*
 * A file: one block with its name (in the player's `dir`) - its path is made
 * when it is opened -, its tags once they are read (they are when it is
 * shown or played), each as long as it is
 */
typedef struct
{
    char*       title;          /* The tag's, else the file's name without .mp3 (NULL: not read yet) */
    char*       artist;         /* NULL: none */
    uint32_t    duration_ms;
    bool        read;           /* Its tags were read (under lock) */
    char        file[];         /* Its name */
} track_t;

struct player
{
    char*               dir;
    dmlist_context_t*   tracks;         /* track_t*, by name - made before the thread, not changed after */
    uint32_t            count;
    dmosi_mutex_t       lock;
    dmosi_semaphore_t   wake;
    dmosi_thread_t      thread;
    bool                quit;
    bool                done;           /* The thread is out */

    /* The tags' thread: reads them ahead of what is shown (player_prefetch()) */
    dmosi_thread_t      tags_thread;
    dmosi_semaphore_t   tags_wake;
    uint32_t            hint;           /* Where it reads from: the shown window's first track (under lock) */

    /* Asked of the thread (under lock) */
    int32_t             current;        /* -1: none */
    int32_t             request;        /* A track to start, -1: none */
    uint32_t            request_ms;     /* ... where in it */
    bool                playing;
    uint32_t            changes;

    /* The thread's, read under lock */
    uint32_t            position_ms;
    uint32_t            duration_ms;
};

/* ---- The files ---- */

static bool is_mp3(const char* name)
{
    size_t n = strlen(name);
    if (n < 5 || name[0] == '.')
        return false;
    const char* e = name + n - 4;
    return e[0] == '.' && (e[1] | 0x20) == 'm' && (e[2] | 0x20) == 'p' && e[3] == '3';
}

static void track_free(track_t* t)
{
    if (t == NULL)
        return;
    if (t->title != NULL)
        Dmod_Free(t->title);
    if (t->artist != NULL)
        Dmod_Free(t->artist);
    Dmod_Free(t);
}

static track_t* track_new(const char* name)
{
    size_t n = strlen(name);
    track_t* t = Dmod_Malloc(sizeof(*t) + n + 1U);
    if (t == NULL)
        return NULL;
    memset(t, 0, sizeof(*t));
    memcpy(t->file, name, n + 1U);
    return t;
}

/* Its path, for as long as it is opened (Dmod_Free() it); NULL: out of memory */
static char* track_path(player_t p, const track_t* t)
{
    size_t size = strlen(p->dir) + 1U + strlen(t->file) + 1U;
    char* path = Dmod_Malloc(size);
    if (path != NULL)
        Dmod_SnPrintf(path, size, "%s/%s", p->dir, t->file);
    return path;
}

static int by_name(const void* a, const void* b)
{
    return strcmp(((const track_t*)a)->file, ((const track_t*)b)->file);
}

static track_t* track_at(player_t p, uint32_t index)
{
    return (index < p->count) ? dmlist_get(p->tracks, index) : NULL;
}

static uint32_t scan(player_t p, const char* dir)
{
    void* d = Dmod_OpenDir(dir);
    if (d == NULL)
        return 0;
    const char* name;
    while ((name = Dmod_ReadDir(d)) != NULL)
    {
        if (!is_mp3(name))
            continue;
        track_t* t = track_new(name);
        if (t == NULL || !dmlist_push_back(p->tracks, t))
        {
            track_free(t);
            DMOD_LOG_ERROR("carhmi: no memory for the files of %s\n", dir);
            break;
        }
    }
    Dmod_CloseDir(d);
    (void)dmlist_sort(p->tracks, by_name);
    p->count = (uint32_t)dmlist_size(p->tracks);
    return p->count;
}

/* A text of a tag on the heap, as long as it is; NULL for none */
static char* tag_text(const char* text)
{
    return (text[0] != '\0') ? Dmod_StrDup(text) : NULL;
}

/* A track's tags, read once: outside the lock (the card), kept under it */
static void read_tags(player_t p, track_t* t)
{
    dmosi_mutex_lock(p->lock);
    bool read = t->read;
    dmosi_mutex_unlock(p->lock);
    if (read)
        return;
    dmmp3_info_t* info = Dmod_Malloc(sizeof(*info));     /* (its texts: 3 x 64 bytes - not on the stack) */
    char* path = track_path(p, t);
    bool ok = info != NULL && path != NULL && dmmp3_read_info(path, info) == 0;
    char* title = ok ? tag_text(info->title) : NULL;
    char* artist = ok ? tag_text(info->artist) : NULL;
    if (title == NULL)
    {
        /* Its file's name, without .mp3 */
        size_t n = strlen(t->file) - 4U;
        if ((title = Dmod_Malloc(n + 1U)) != NULL)
        {
            memcpy(title, t->file, n);
            title[n] = '\0';
        }
    }
    dmosi_mutex_lock(p->lock);
    if (!t->read)
    {
        t->read = true;
        t->title = title;
        t->artist = artist;
        t->duration_ms = ok ? info->duration_ms : 0;
        title = artist = NULL;
    }
    dmosi_mutex_unlock(p->lock);
    if (title != NULL)
        Dmod_Free(title);                           /* (read meanwhile by the other thread) */
    if (artist != NULL)
        Dmod_Free(artist);
    if (path != NULL)
        Dmod_Free(path);
    if (info != NULL)
        Dmod_Free(info);
}

/* ---- The thread ---- */

static void player_thread(void* arg)
{
    player_t p = arg;
    int16_t* pcm = Dmod_Malloc(CHUNK_FRAMES * 2u * sizeof(int16_t));
    dmmp3_t mp3 = NULL;
    libdmaudio_stream_t stream = NULL;
    libdmaudio_format_t format = { 0, 0, DMDRVI_AUDIO_FORMAT_S16_LE };
    bool paused = false;

    while (pcm != NULL)
    {
        while (dmosi_semaphore_wait(p->wake, 1, 0) == 0)
            ;                                       /* (what woke it: seen below) */
        dmosi_mutex_lock(p->lock);
        bool quit = p->quit;
        int32_t request = p->request;
        uint32_t request_ms = p->request_ms;
        bool playing = p->playing;
        p->request = -1;
        p->request_ms = 0;
        dmosi_mutex_unlock(p->lock);
        if (quit)
            break;

        if (request >= 0)
        {
            /* Another track: what the stream holds of the previous one is dropped */
            dmmp3_close(mp3);
            int status = 0;
            char* path = track_path(p, track_at(p, (uint32_t)request));
            mp3 = (path != NULL) ? dmmp3_open(path, &status) : NULL;
            dmmp3_info_t info;
            memset(&info, 0, sizeof(info));
            if (mp3 != NULL)
            {
                (void)dmmp3_get_info(mp3, &info);
                if (request_ms > 0 && dmmp3_seek(mp3, request_ms) != 0)
                    request_ms = 0;
            }
            else
                DMOD_LOG_ERROR("carhmi: cannot play %s (%d)\n", (path != NULL) ? path : "?", status);
            if (path != NULL)
                Dmod_Free(path);
            libdmaudio_close(stream);
            stream = NULL;
            if (mp3 != NULL)
            {
                format.sample_rate_hz = info.sample_rate_hz;
                format.channels = info.channels;
                libdmaudio_stream_options_t o = { BUFFER_MS, 100 };
                stream = libdmaudio_open(NULL, &format, &o, &status);
                if (stream == NULL)
                    DMOD_LOG_ERROR("carhmi: no audio device to play on (%d)\n", status);
                paused = false;
            }
            dmosi_mutex_lock(p->lock);
            p->position_ms = (mp3 != NULL) ? request_ms : 0;
            p->duration_ms = info.duration_ms;
            dmosi_mutex_unlock(p->lock);
        }

        if (stream != NULL && playing == paused)
        {
            /* Paused: what the stream holds waits in it */
            paused = !playing;
            (void)libdmaudio_set_paused(stream, paused);
        }
        if (mp3 == NULL || stream == NULL || !playing)
        {
            (void)dmosi_semaphore_wait(p->wake, 1, (int32_t)IDLE_MS);
            continue;
        }

        int32_t n = dmmp3_read(mp3, pcm, CHUNK_FRAMES);
        if (n <= 0)
        {
            /* Its end: what is buffered plays out, then the next one */
            (void)libdmaudio_drain(stream, 2000);
            dmmp3_close(mp3);
            mp3 = NULL;
            dmosi_mutex_lock(p->lock);
            if (p->request < 0 && p->count > 0)
            {
                p->current = (p->current + 1) % (int32_t)p->count;
                p->request = p->current;
                p->changes++;
            }
            dmosi_mutex_unlock(p->lock);
            continue;
        }
        const int16_t* at = pcm;
        while (n > 0)
        {
            int32_t w = libdmaudio_write(stream, at, (uint32_t)n, (int32_t)WRITE_MS);
            if (w == -ETIMEDOUT)
            {
                /* The stream is full and stays so (paused, or another track asked for): look again */
                dmosi_mutex_lock(p->lock);
                bool again = p->request < 0 && p->playing && !p->quit;
                dmosi_mutex_unlock(p->lock);
                if (!again)
                    break;
                continue;
            }
            if (w < 0)
            {
                DMOD_LOG_ERROR("carhmi: the audio device is gone (%d)\n", (int)w);
                libdmaudio_close(stream);
                stream = NULL;
                break;
            }
            at += w * format.channels;
            n -= w;
        }
        /* Where it is: what was decoded, less what the stream holds */
        libdmaudio_stream_status_t st;
        uint32_t held = 0;
        if (stream != NULL && libdmaudio_get_status(stream, &st) == 0 && format.sample_rate_hz != 0)
            held = (uint32_t)((uint64_t)st.buffered_frames * 1000u / format.sample_rate_hz);
        uint32_t told = dmmp3_tell(mp3);
        dmosi_mutex_lock(p->lock);
        p->position_ms = (told > held) ? told - held : 0;
        dmosi_mutex_unlock(p->lock);
    }

    dmmp3_close(mp3);
    libdmaudio_close(stream);
    if (pcm != NULL)
        Dmod_Free(pcm);
    dmosi_mutex_lock(p->lock);
    p->done = true;
    dmosi_mutex_unlock(p->lock);
}

/* ---- The tags' thread ---- */

/*
 * The track nearest to the hint whose tags are not read: the hint and after
 * it first - where the list goes when it is scrolled down -, then before
 * it; -1: all are read
 */
static int32_t next_unread(player_t p, uint32_t hint)
{
    for (uint32_t d = 0; d < p->count; d++)
    {
        uint32_t ahead = hint + d;
        if (ahead < p->count)
        {
            track_t* t = track_at(p, ahead);
            dmosi_mutex_lock(p->lock);
            bool read = t->read;
            dmosi_mutex_unlock(p->lock);
            if (!read)
                return (int32_t)ahead;
        }
        if (d < hint)
        {
            track_t* t = track_at(p, hint - 1U - d);
            dmosi_mutex_lock(p->lock);
            bool read = t->read;
            dmosi_mutex_unlock(p->lock);
            if (!read)
                return (int32_t)(hint - 1U - d);
        }
    }
    return -1;
}

/*
 * Reads the tracks' tags ahead of what is shown, so a window the list moves
 * to finds them in memory and is shown at once - not after a file of the
 * card a row
 */
static void tags_thread(void* arg)
{
    player_t p = arg;
    for (;;)
    {
        dmosi_mutex_lock(p->lock);
        bool quit = p->quit;
        uint32_t hint = p->hint;
        dmosi_mutex_unlock(p->lock);
        if (quit)
            break;
        int32_t index = next_unread(p, hint);
        if (index < 0)
            (void)dmosi_semaphore_wait(p->tags_wake, 1, (int32_t)TAGS_IDLE_MS);
        else
            read_tags(p, track_at(p, (uint32_t)index));
    }
}

/* ---- The player ---- */

player_t player_new(const char* dir)
{
    player_t p = Dmod_Malloc(sizeof(*p));
    if (p == NULL)
        return NULL;
    memset(p, 0, sizeof(*p));
    p->tracks = dmlist_create();
    p->dir = Dmod_StrDup(dir);
    p->lock = dmosi_mutex_create(false);
    p->wake = dmosi_semaphore_create(0, 8);
    p->tags_wake = dmosi_semaphore_create(0, 8);
    p->current = -1;
    p->request = -1;
    if (p->tracks == NULL || p->dir == NULL || p->lock == NULL || p->wake == NULL || p->tags_wake == NULL)
    {
        player_free(p);
        return NULL;
    }
    (void)scan(p, dir);
    p->thread = dmosi_thread_create(player_thread, p, THREAD_PRIORITY, THREAD_STACK, "carhmi_player",
                                    dmosi_process_current());
    p->tags_thread = (p->thread != NULL) ? dmosi_thread_create(tags_thread, p, TAGS_PRIORITY, TAGS_STACK, "carhmi_tags",
                                                                dmosi_process_current()) : NULL;
    if (p->thread == NULL || p->tags_thread == NULL)
    {
        player_free(p);
        return NULL;
    }
    return p;
}

void player_free(player_t p)
{
    if (p == NULL)
        return;
    if (p->thread != NULL)
    {
        dmosi_mutex_lock(p->lock);
        p->quit = true;
        dmosi_mutex_unlock(p->lock);
        (void)dmosi_semaphore_post(p->wake, 1);
        (void)dmosi_thread_join(p->thread);
        dmosi_thread_destroy(p->thread);
    }
    if (p->tags_thread != NULL)
    {
        dmosi_mutex_lock(p->lock);
        p->quit = true;
        dmosi_mutex_unlock(p->lock);
        (void)dmosi_semaphore_post(p->tags_wake, 1);
        (void)dmosi_thread_join(p->tags_thread);
        dmosi_thread_destroy(p->tags_thread);
    }
    if (p->wake != NULL)
        dmosi_semaphore_destroy(p->wake);
    if (p->tags_wake != NULL)
        dmosi_semaphore_destroy(p->tags_wake);
    if (p->lock != NULL)
        dmosi_mutex_destroy(p->lock);
    if (p->tracks != NULL)
    {
        track_t* t;
        while ((t = dmlist_pop_front(p->tracks)) != NULL)
            track_free(t);
        dmlist_destroy(p->tracks);
    }
    if (p->dir != NULL)
        Dmod_Free(p->dir);
    Dmod_Free(p);
}

uint32_t player_count(player_t p)
{
    return p->count;
}

bool player_info(player_t p, uint32_t index, player_info_t* info)
{
    track_t* t = track_at(p, index);
    if (t == NULL)
        return false;
    read_tags(p, t);
    dmosi_mutex_lock(p->lock);
    info->title = (t->title != NULL) ? t->title : t->file;
    info->artist = (t->artist != NULL) ? t->artist : "";
    info->duration_ms = t->duration_ms;
    dmosi_mutex_unlock(p->lock);
    return true;
}

/* The thread takes what it was asked at once (it takes the posts at the top of its loop) */
static void wake(player_t p)
{
    (void)dmosi_semaphore_post(p->wake, 1);
}

void player_prefetch(player_t p, uint32_t first)
{
    if (p == NULL)
        return;
    dmosi_mutex_lock(p->lock);
    bool other = p->hint != first;
    p->hint = first;
    dmosi_mutex_unlock(p->lock);
    if (other)
        (void)dmosi_semaphore_post(p->tags_wake, 1);
}

char* player_path(player_t p, uint32_t index)
{
    return (p != NULL && index < p->count) ? track_path(p, track_at(p, index)) : NULL;
}

const char* player_file(player_t p, uint32_t index)
{
    track_t* t = (p != NULL) ? track_at(p, index) : NULL;
    return (t != NULL) ? t->file : NULL;
}

int32_t player_find(player_t p, const char* file)
{
    for (uint32_t i = 0; p != NULL && file != NULL && i < p->count; i++)
    {
        if (strcmp(track_at(p, i)->file, file) == 0)
            return (int32_t)i;
    }
    return -1;
}

void player_play_at(player_t p, uint32_t index, uint32_t ms, bool playing)
{
    if (index >= p->count)
        return;
    dmosi_mutex_lock(p->lock);
    p->current = (int32_t)index;
    p->request = (int32_t)index;
    p->request_ms = ms;
    p->playing = playing;
    p->changes++;
    dmosi_mutex_unlock(p->lock);
    wake(p);
}

void player_play(player_t p, uint32_t index)
{
    player_play_at(p, index, 0, true);
}

void player_toggle(player_t p)
{
    dmosi_mutex_lock(p->lock);
    bool start = p->current < 0 && p->count > 0;
    p->playing = !p->playing || start;
    if (start)
    {
        p->current = 0;
        p->request = 0;
    }
    p->changes++;
    dmosi_mutex_unlock(p->lock);
    wake(p);
}

void player_next(player_t p, int32_t step)
{
    if (p->count == 0)
        return;
    dmosi_mutex_lock(p->lock);
    int32_t n = (int32_t)p->count;
    int32_t i = (p->current < 0) ? 0 : (((p->current + step) % n) + n) % n;
    dmosi_mutex_unlock(p->lock);
    player_play(p, (uint32_t)i);
}

void player_state(player_t p, player_state_t* state)
{
    dmosi_mutex_lock(p->lock);
    state->track = p->current;
    state->playing = p->playing;
    state->position_ms = p->position_ms;
    state->duration_ms = p->duration_ms;
    state->changes = p->changes;
    dmosi_mutex_unlock(p->lock);
}
