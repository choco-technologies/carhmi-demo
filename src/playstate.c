#include "playstate.h"
#include <string.h>

#define FILE_NAME   ".playing"
#define MAX_FILE    1024u               /* Bytes of the file read, at most */

/*
 * The file: "<second> <playing: 0 / 1>\n<the track's file name>\n"
 */
struct playstate
{
    char*               path;           /* The file, NULL: nothing is kept */
    char*               file;           /* The track's file name, NULL: none */
    uint32_t            position_ms;
    bool                playing;
    bool                saved;          /* What was read: file, position_ms, playing */
    bool                dirty;          /* What is not in the file yet */
    Dmod_Timestamp_t    written_at;
};

static void read_state(playstate_t s)
{
    void* f = Dmod_FileOpen(s->path, "rb");
    char* text = (f != NULL) ? Dmod_Malloc(MAX_FILE + 1U) : NULL;
    size_t n = (text != NULL) ? Dmod_FileRead(text, 1, MAX_FILE, f) : 0;
    if (f != NULL)
        Dmod_FileClose(f);
    if (text == NULL)
        return;
    text[n] = '\0';
    uint32_t second = 0, playing = 0;
    const char* p = text;
    while (*p >= '0' && *p <= '9')
        second = second * 10u + (uint32_t)(*p++ - '0');
    if (*p == ' ')
        p++;
    if (*p == '0' || *p == '1')
        playing = (uint32_t)(*p++ - '0');
    const char* name = (*p == '\n') ? p + 1 : NULL;
    const char* end = (name != NULL) ? strchr(name, '\n') : NULL;
    if (end != NULL && end > name && (s->file = Dmod_Malloc((size_t)(end - name) + 1U)) != NULL)
    {
        memcpy(s->file, name, (size_t)(end - name));
        s->file[end - name] = '\0';
        s->position_ms = second * 1000u;
        s->playing = playing != 0;
        s->saved = true;
    }
    Dmod_Free(text);
}

static void write_state(playstate_t s, Dmod_Timestamp_t now)
{
    s->written_at = now;
    s->dirty = false;
    if (s->path == NULL || s->file == NULL)
        return;
    void* f = Dmod_FileOpen(s->path, "wb");
    if (f == NULL)
        return;                         /* (a read-only card: nothing is kept) */
    char head[24];
    int n = Dmod_SnPrintf(head, sizeof(head), "%u %u\n", (unsigned)(s->position_ms / 1000u), s->playing ? 1u : 0u);
    if (n > 0)
        (void)Dmod_FileWrite(head, 1, (size_t)n, f);
    (void)Dmod_FileWrite(s->file, 1, strlen(s->file), f);
    (void)Dmod_FileWrite("\n", 1, 1, f);
    Dmod_FileClose(f);
}

playstate_t playstate_new(const char* dir)
{
    playstate_t s = Dmod_Malloc(sizeof(*s));
    if (s == NULL)
        return NULL;
    memset(s, 0, sizeof(*s));
    if (dir != NULL)
    {
        size_t size = strlen(dir) + sizeof("/" FILE_NAME);
        if ((s->path = Dmod_Malloc(size)) != NULL)
        {
            Dmod_SnPrintf(s->path, size, "%s/%s", dir, FILE_NAME);
            read_state(s);
        }
    }
    return s;
}

void playstate_free(playstate_t s)
{
    if (s == NULL)
        return;
    if (s->dirty)
        write_state(s, Dmod_GetUptime());
    if (s->file != NULL)
        Dmod_Free(s->file);
    if (s->path != NULL)
        Dmod_Free(s->path);
    Dmod_Free(s);
}

bool playstate_saved(playstate_t s, const char** file, uint32_t* position_ms, bool* playing)
{
    if (s == NULL || !s->saved)
        return false;
    *file = s->file;
    *position_ms = s->position_ms;
    *playing = s->playing;
    return true;
}

void playstate_update(playstate_t s, const char* file, uint32_t position_ms, bool playing, Dmod_Timestamp_t now)
{
    if (s == NULL || file == NULL)
        return;
    bool other = s->file == NULL || strcmp(s->file, file) != 0;
    if (other)
    {
        char* copy = Dmod_StrDup(file);
        if (copy == NULL)
            return;
        if (s->file != NULL)
            Dmod_Free(s->file);
        s->file = copy;
        s->saved = false;               /* (what was read is gone) */
    }
    bool changed = other || playing != s->playing;
    s->dirty = s->dirty || changed || position_ms / 1000u != s->position_ms / 1000u;
    s->position_ms = position_ms;
    s->playing = playing;
    if (changed || (s->dirty && playing && now - s->written_at >= PLAYSTATE_SAVE_MS))
        write_state(s, now);
}
