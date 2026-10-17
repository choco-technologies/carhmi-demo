#include "volume.h"
#include "libdmaudio.h"
#include <string.h>

#define FILE_NAME   ".volume"

struct volume
{
    char*               path;           /* The file, NULL: nothing is kept */
    uint8_t             level;
    bool                muted;
    bool                applied;        /* The device has level and muted */
    bool                dirty;          /* Level is not in the file */
    Dmod_Timestamp_t    changed_at;
};

/* The level the file says, else VOLUME_DEFAULT */
static uint8_t read_level(const char* path)
{
    uint32_t level = VOLUME_DEFAULT;
    void* f = Dmod_FileOpen(path, "rb");
    if (f != NULL)
    {
        char text[8];
        size_t n = Dmod_FileRead(text, 1, sizeof(text), f);
        Dmod_FileClose(f);
        uint32_t value = 0;
        size_t digits = 0;
        while (digits < n && text[digits] >= '0' && text[digits] <= '9')
            value = value * 10u + (uint32_t)(text[digits++] - '0');
        if (digits > 0 && value <= 100u)
            level = value;
    }
    return (uint8_t)level;
}

static void write_level(const char* path, uint8_t level)
{
    void* f = Dmod_FileOpen(path, "wb");
    if (f == NULL)
        return;                         /* (a read-only card: the volume is not kept) */
    char text[8];
    int n = Dmod_SnPrintf(text, sizeof(text), "%u\n", (unsigned)level);
    if (n > 0)
        (void)Dmod_FileWrite(text, 1, (size_t)n, f);
    Dmod_FileClose(f);
}

volume_t volume_new(const char* dir)
{
    volume_t v = Dmod_Malloc(sizeof(*v));
    if (v == NULL)
        return NULL;
    memset(v, 0, sizeof(*v));
    v->level = VOLUME_DEFAULT;
    if (dir != NULL)
    {
        size_t size = strlen(dir) + sizeof("/" FILE_NAME);
        v->path = Dmod_Malloc(size);
        if (v->path != NULL)
        {
            Dmod_SnPrintf(v->path, size, "%s/%s", dir, FILE_NAME);
            v->level = read_level(v->path);
        }
    }
    return v;
}

void volume_free(volume_t v)
{
    if (v == NULL)
        return;
    if (v->dirty && v->path != NULL)
        write_level(v->path, v->level);
    Dmod_Free(v->path);
    Dmod_Free(v);
}

uint8_t volume_level(volume_t v)
{
    return v->level;
}

bool volume_muted(volume_t v)
{
    return v->muted;
}

void volume_set(volume_t v, int32_t level)
{
    uint8_t clamped = (level < 0) ? 0 : (level > 100) ? 100 : (uint8_t)level;
    if (clamped == v->level && !v->muted)
        return;
    v->level = clamped;
    v->muted = false;
    v->applied = false;
    v->dirty = true;
    v->changed_at = Dmod_GetUptime();
}

void volume_toggle_mute(volume_t v)
{
    v->muted = !v->muted;
    v->applied = false;
}

bool volume_poll(volume_t v, Dmod_Timestamp_t now)
{
    if (!v->applied)
    {
        /* The device (NULL: the first one) may not be served yet */
        v->applied = libdmaudio_device_set_volume(NULL, v->level) == 0 &&
                     libdmaudio_device_set_mute(NULL, v->muted) == 0;
    }
    if (v->dirty && v->path != NULL && now - v->changed_at >= VOLUME_SAVE_MS)
    {
        write_level(v->path, v->level);
        v->dirty = false;
    }
    return v->applied;
}
