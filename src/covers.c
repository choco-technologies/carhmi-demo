#include "covers.h"
#include "dmosi.h"
#include "dmmp3.h"
#include "dmimg.h"
#include "libtodmvi.h"
#include <errno.h>
#include <string.h>

/*
 * Two threads make the covers: one the tracks asked for (the one asked first
 * before the others), as fast as the views let it, the other the rest of the
 * tracks one after another, only when nothing else wants the CPU - so the
 * whole card's covers are there after a while. A cover: the picture is read
 * into memory (and hashed: its key) and decoded once, as small as the sizes
 * let it be - at the decoder's smallest scale, then averaged down while it
 * is decoded (a blurred size needs half of its side). Each size the directory does not have yet is resized from it (the
 * middle that covers its aspect) and made by libtodmvi - blurred, cut,
 * rounded, written to a temporary file and renamed: a reader never sees half
 * of one. The rest of carhmi only asks and reads what is known; a stop asked
 * meanwhile ends the decoding (and the sizes) where it is.
 */

#define THREAD_STACK    16384u          /* (the decoders' and libtodmvi's images are on the heap) */
#define SHOWN_PRIORITY  1               /* What is shown: as the views (time sliced with them), below the player (2) */
#define REST_PRIORITY   0               /* The rest of the tracks: only when nothing else runs */
#define IDLE_MS         1000u
#define WAKE_MAX        1024u           /* Posts the thread is woken by, at most: one for each track asked */
#define KEY_DIGITS      8u              /* Of a key in a name: 32 bits in hex */
#define MAX_DECODED     (512u * 1024u)  /* Pixels of a decoded picture (averaged down), at most: 8 bytes each */

/* What is known of a track's cover */
#define COVER_UNKNOWN   0u
#define COVER_WANTED    1u
#define COVER_MADE      2u
#define COVER_NONE      3u              /* No picture, or one that cannot be made */
#define COVER_MAKING    4u              /* A thread makes it */

#define WORKERS         2u              /* What is asked for (SHOWN_PRIORITY), the rest (REST_PRIORITY) */

/* A thread making covers */
typedef struct
{
    struct covers*      covers;
    bool                rest;           /* The tracks nobody asked for, else the ones asked for */
    dmosi_thread_t      thread;
} worker_t;

/* A size the view shows the covers at: the end of its files' names, how it is made */
typedef struct
{
    char*               tail;           /* "-88x88-k0-0-88x88-r11.dmvi" */
    uint16_t            width;          /* Scaled to cover it (0: as it is) */
    uint16_t            height;
    libtodmvi_options_t options;        /* ... then blurred, cut, rounded */
} cover_size_t;

struct covers
{
    player_t            player;
    char*               dir;
    cover_size_t*       sizes;
    uint32_t            size_count;
    uint32_t            count;          /* Of the tracks */
    uint8_t*            states;         /* COVER_*, by track (under lock) */
    uint32_t*           keys;           /* ... of the COVER_MADE ones */
    int32_t             first;          /* Asked first, -1: none */
    uint32_t            next_rest;      /* Where the rest is gone through from */
    bool                quit;
    dmosi_mutex_t       lock;
    dmosi_semaphore_t   wake;           /* Something asked for */
    worker_t            workers[WORKERS];
};

/* ---- The sizes ---- */

/* A number at p (before end); NULL: none there */
static const char* number(const char* p, const char* end, uint16_t* value)
{
    uint32_t v = 0;
    const char* start = p;
    while (p < end && *p >= '0' && *p <= '9' && v <= 0xFFFFu)
        v = v * 10u + (uint32_t)(*p++ - '0');
    if (p == start || v > 0xFFFFu)
        return NULL;
    *value = (uint16_t)v;
    return p;
}

/* "<w>x<h>" at p */
static const char* dimensions(const char* p, const char* end, uint16_t* w, uint16_t* h)
{
    p = number(p, end, w);
    if (p == NULL || p >= end || *p != 'x')
        return NULL;
    return number(p + 1, end, h);
}

/*
 * The end of a file's name as todmvs makes it: -<w>x<h> scaled, -b<n>
 * blurred, -k<x>-<y>-<w>x<h> cut, -r<n> rounded, then .dmvi
 */
static bool parse_size(const char* tail, cover_size_t* s)
{
    size_t n = strlen(tail);
    if (n < 5 || strcmp(tail + n - 5, ".dmvi") != 0)
        return false;
    const char* p = tail;
    const char* end = tail + n - 5;
    s->width = s->height = 0;
    memset(&s->options, 0, sizeof(s->options));
    s->options.format = LIBTODMVI_FORMAT_AUTO;
    while (p != NULL && p < end)
    {
        if (*p++ != '-' || p >= end)
            return false;
        char kind = *p;
        if (kind >= '0' && kind <= '9')
            p = dimensions(p, end, &s->width, &s->height);
        else if (kind == 'b')
            p = number(p + 1, end, &s->options.blur);
        else if (kind == 'r')
            p = number(p + 1, end, &s->options.radius);
        else if (kind == 'k')
        {
            p = number(p + 1, end, &s->options.crop_x);
            p = (p != NULL && p < end && *p == '-') ? number(p + 1, end, &s->options.crop_y) : NULL;
            p = (p != NULL && p < end && *p == '-') ? dimensions(p + 1, end, &s->options.crop_w, &s->options.crop_h) : NULL;
        }
        else
            return false;
    }
    s->options.max_width = s->width;
    s->options.max_height = s->height;
    s->options.compression = "";        /* Not packed: FastLZ's table (32 kB) is on the stack */
    return p != NULL;
}

/* The sizes of `sizes` (once each); false: none, out of memory */
static bool read_sizes(covers_t c, const char* sizes)
{
    uint32_t n = 1;
    for (const char* p = sizes; *p != '\0'; p++)
        n += (*p == ' ') ? 1u : 0u;
    if ((c->sizes = Dmod_Malloc(n * sizeof(cover_size_t))) == NULL)
        return false;
    memset(c->sizes, 0, n * sizeof(cover_size_t));
    const char* p = sizes;
    while (*p != '\0')
    {
        const char* end = strchr(p, ' ');
        size_t length = (end != NULL) ? (size_t)(end - p) : strlen(p);
        char* tail = (length > 0) ? Dmod_Malloc(length + 1U) : NULL;
        if (tail != NULL)
        {
            memcpy(tail, p, length);
            tail[length] = '\0';
            bool known = false;
            for (uint32_t i = 0; i < c->size_count && !known; i++)
                known = strcmp(c->sizes[i].tail, tail) == 0;
            cover_size_t* s = &c->sizes[c->size_count];
            if (!known && parse_size(tail, s))
            {
                s->tail = tail;
                c->size_count++;
            }
            else
            {
                if (!known)
                    DMOD_LOG_WARN("carhmi: a size of the covers not known: %s\n", tail);
                Dmod_Free(tail);
            }
        }
        p += length;
        while (*p == ' ')
            p++;
    }
    return c->size_count > 0;
}

/* ---- Making a cover ---- */

/* The picture of a tag, in memory: what the decoder reads */
typedef struct
{
    const uint8_t*  data;
    uint32_t        size;
    uint32_t        at;
} memory_t;

static int32_t memory_read(void* ctx, void* buffer, size_t size)
{
    memory_t* m = ctx;
    uint32_t n = m->size - m->at;
    if (size < n)
        n = (uint32_t)size;
    memcpy(buffer, m->data + m->at, n);
    m->at += n;
    return (int32_t)n;
}

static int memory_seek(void* ctx, uint32_t offset)
{
    memory_t* m = ctx;
    if (offset > m->size)
        return -EINVAL;
    m->at = offset;
    return 0;
}

/* FNV-1a; never 0 (0: no key) */
static uint32_t hash(const uint8_t* data, uint32_t size)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; i++)
        h = (h ^ data[i]) * 16777619u;
    return (h != 0) ? h : 1u;
}

/* The file of a size of the cover `key` (Dmod_Free() it) */
static char* size_path(covers_t c, uint32_t key, const cover_size_t* s)
{
    size_t size = strlen(c->dir) + 1U + KEY_DIGITS + strlen(s->tail) + 1U;
    char* path = Dmod_Malloc(size);
    if (path != NULL)
        Dmod_SnPrintf(path, size, "%s/%08x%s", c->dir, (unsigned)key, s->tail);
    return path;
}

/* Whether the directory has every size of the cover `key` */
static bool all_made(covers_t c, uint32_t key)
{
    for (uint32_t i = 0; i < c->size_count; i++)
    {
        char* path = size_path(c, key, &c->sizes[i]);
        bool there = path != NULL && Dmod_FileAvailable(path);
        if (path != NULL)
            Dmod_Free(path);
        if (!there)
            return false;
    }
    return true;
}

/* The picture of the track's tag, into memory (Dmod_Free() it); NULL: none */
static uint8_t* read_picture(covers_t c, uint32_t index, uint32_t* size)
{
    char* path = player_path(c->player, index);
    dmmp3_picture_t picture;
    uint8_t* data = NULL;
    if (path != NULL && dmmp3_find_picture(path, &picture) == 0 && picture.size > 0 && picture.size <= COVERS_MAX_PICTURE)
    {
        void* f = Dmod_FileOpen(path, "rb");
        data = (f != NULL) ? Dmod_Malloc(picture.size) : NULL;
        if (data != NULL && (Dmod_FileSeek(f, (Dmod_FileOffset_t)picture.offset, DMOD_SEEK_SET) != 0 ||
                             Dmod_FileRead(data, 1, picture.size, f) != picture.size))
        {
            Dmod_Free(data);
            data = NULL;
        }
        if (f != NULL)
            Dmod_FileClose(f);
        *size = picture.size;
    }
    if (path != NULL)
        Dmod_Free(path);
    return data;
}

/* Whether the thread is asked to stop (read without the lock: only a hint, the lock tells for sure) */
static bool quitting(covers_t c)
{
    return *(volatile bool*)&c->quit;
}

/*
 * The decoded picture, averaged down while it is decoded: `factor` x
 * `factor` of its pixels (at the decoder's scale) into one - the sums of
 * their channels first, then, in place, their averages
 */
typedef struct
{
    covers_t    covers;
    uint16_t*   sums;                   /* a, r, g, b of each pixel (at most 8 x 8 x 255) */
    uint32_t*   pixels;                 /* The averages, where the sums were */
    uint32_t    width, height;          /* Averaged */
    uint32_t    decoded_w, decoded_h;
    uint32_t    factor;                 /* 1, 2, 4, 8 */
} canvas_t;

static int put_block(void* ctx, const dmimg_block_t* b)
{
    canvas_t* cv = ctx;
    if (quitting(cv->covers))
        return -ECANCELED;
    for (uint32_t y = 0; y < b->height && b->y + y < cv->decoded_h; y++)
    {
        const uint32_t* src = b->pixels + (size_t)y * b->stride;
        uint16_t* row = cv->sums + (size_t)((b->y + y) / cv->factor) * cv->width * 4u;
        for (uint32_t x = 0; x < b->width && b->x + x < cv->decoded_w; x++)
        {
            uint32_t p = src[x];
            uint16_t* sum = row + (size_t)((b->x + x) / cv->factor) * 4u;
            sum[0] = (uint16_t)(sum[0] + (p >> 24));
            sum[1] = (uint16_t)(sum[1] + ((p >> 16) & 0xFFu));
            sum[2] = (uint16_t)(sum[2] + ((p >> 8) & 0xFFu));
            sum[3] = (uint16_t)(sum[3] + (p & 0xFFu));
        }
    }
    return 0;
}

/* The sums into averages: a pixel's 4 bytes where its 8 were (going forward, nothing is read after it is written) */
static void average(canvas_t* cv)
{
    uint32_t f = cv->factor;
    cv->pixels = (uint32_t*)cv->sums;
    for (uint32_t ty = 0; ty < cv->height; ty++)
    {
        uint32_t rows = (cv->decoded_h - ty * f < f) ? cv->decoded_h - ty * f : f;
        for (uint32_t tx = 0; tx < cv->width; tx++)
        {
            uint32_t cols = (cv->decoded_w - tx * f < f) ? cv->decoded_w - tx * f : f, n = rows * cols;
            size_t i = (size_t)ty * cv->width + tx;
            const uint16_t* sum = cv->sums + i * 4u;
            uint32_t v = 0;
            for (uint32_t k = 0; k < 4; k++)
            {
                uint32_t c = (sum[k] + n / 2u) / n;
                v = (v << 8) | ((c < 255u) ? c : 255u);     /* (an interlaced PNG draws some pixels twice) */
            }
            cv->pixels[i] = v;
        }
    }
}

/* The side of the picture's middle the sizes need: a sharp size its own, a blurred one half of it (the blur hides the rest) */
static uint32_t needed(covers_t c, const dmimg_info_t* info)
{
    uint32_t need = 0;
    for (uint32_t i = 0; i < c->size_count; i++)
    {
        const cover_size_t* s = &c->sizes[i];
        uint32_t side = (s->width > s->height) ? s->width : s->height;
        if (side == 0)
            side = (info->width > info->height) ? info->width : info->height;
        if (s->options.blur != 0)
            side = (side + 1u) / 2u;
        need = (side > need) ? side : need;
    }
    return need;
}

/*
 * The picture decoded as small as the sizes let it be: at the decoder's
 * smallest scale, then averaged down, as long as its shorter side is not
 * shorter than needed(); 0 or an error
 */
static int decode(covers_t c, const uint8_t* data, uint32_t size, canvas_t* cv)
{
    memory_t m = { data, size, 0 };
    dmimg_input_t input = { memory_read, memory_seek, &m, size };
    dmimg_info_t info;
    int status = 0;
    dmimg_t image = dmimg_open(&input, &info, &status);
    if (image == NULL)
        return (status != 0) ? status : -ENOTSUP;
    uint32_t need = needed(c, &info);
    uint8_t scale = 0;
    for (uint8_t n = 1; n <= DMIMG_MAX_SCALE; n++)
    {
        uint32_t w = DMIMG_SCALED(info.width, n), h = DMIMG_SCALED(info.height, n);
        if ((info.scales & DMIMG_SCALE(n)) != 0 && ((w < h) ? w : h) >= need)
            scale = n;
    }
    cv->covers = c;
    cv->decoded_w = DMIMG_SCALED(info.width, scale);
    cv->decoded_h = DMIMG_SCALED(info.height, scale);
    uint32_t shorter = (cv->decoded_w < cv->decoded_h) ? cv->decoded_w : cv->decoded_h;
    cv->factor = 1;
    while (cv->factor < 8u && shorter / (cv->factor * 2u) >= need)
        cv->factor *= 2u;
    cv->width = (cv->decoded_w + cv->factor - 1u) / cv->factor;
    cv->height = (cv->decoded_h + cv->factor - 1u) / cv->factor;
    size_t bytes = (size_t)cv->width * cv->height * 4u * sizeof(uint16_t);
    if ((uint64_t)cv->width * cv->height > MAX_DECODED)
        status = -E2BIG;
    else if ((cv->sums = Dmod_Malloc(bytes)) == NULL)
        status = -ENOMEM;
    else
    {
        memset(cv->sums, 0, bytes);
        status = dmimg_decode(image, scale, put_block, cv);
    }
    dmimg_close(image);
    if (status == 0)
        average(cv);
    return status;
}

/*
 * The middle of the picture that covers w x h (as the view places it),
 * resized to w x h: each pixel the average of what it covers (at least
 * one pixel - a blurred size may be larger than the picture)
 */
static uint32_t* resize(const canvas_t* cv, uint32_t tw, uint32_t th)
{
    uint32_t x = 0, y = 0, w = cv->width, h = cv->height;
    if ((uint64_t)w * th > (uint64_t)h * tw)
    {
        w = (uint32_t)(((uint64_t)h * tw + th / 2u) / th);
        x = (cv->width - w) / 2u;
    }
    else
    {
        h = (uint32_t)(((uint64_t)w * th + tw / 2u) / tw);
        y = (cv->height - h) / 2u;
    }
    uint32_t* out = (w > 0 && h > 0) ? Dmod_Malloc((size_t)tw * th * sizeof(uint32_t)) : NULL;
    for (uint32_t ty = 0; out != NULL && ty < th; ty++)
    {
        uint32_t y0 = y + (uint32_t)((uint64_t)ty * h / th), y1 = y + (uint32_t)((uint64_t)(ty + 1u) * h / th);
        y1 = (y1 > y0) ? y1 : y0 + 1u;
        for (uint32_t tx = 0; tx < tw; tx++)
        {
            uint32_t x0 = x + (uint32_t)((uint64_t)tx * w / tw), x1 = x + (uint32_t)((uint64_t)(tx + 1u) * w / tw);
            x1 = (x1 > x0) ? x1 : x0 + 1u;
            uint32_t sum[4] = { 0, 0, 0, 0 }, n = (x1 - x0) * (y1 - y0);
            for (uint32_t sy = y0; sy < y1; sy++)
            {
                const uint32_t* row = cv->pixels + (size_t)sy * cv->width;
                for (uint32_t sx = x0; sx < x1; sx++)
                {
                    uint32_t p = row[sx];
                    sum[0] += p >> 24;
                    sum[1] += (p >> 16) & 0xFFu;
                    sum[2] += (p >> 8) & 0xFFu;
                    sum[3] += p & 0xFFu;
                }
            }
            out[(size_t)ty * tw + tx] = (((sum[0] + n / 2u) / n) << 24) | (((sum[1] + n / 2u) / n) << 16) |
                                        (((sum[2] + n / 2u) / n) << 8) | ((sum[3] + n / 2u) / n);
        }
    }
    return out;
}

/* A size of the decoded picture: resized to it, then blurred, cut, rounded (libtodmvi) */
static int make_size(covers_t c, const canvas_t* cv, uint32_t key, const cover_size_t* s)
{
    uint32_t w = (s->width != 0) ? s->width : cv->width, h = (s->height != 0) ? s->height : cv->height;
    uint32_t* pixels = resize(cv, w, h);
    char* path = (pixels != NULL) ? size_path(c, key, s) : NULL;
    int ret = (path == NULL) ? -ENOMEM : libtodmvi_convert_pixels(pixels, w, h, w, path, &s->options, NULL);
    if (pixels != NULL)
        Dmod_Free(pixels);
    if (path != NULL)
        Dmod_Free(path);
    return ret;
}

/* The cover of a track: its key, when every size of it is there */
static bool make(covers_t c, uint32_t index, uint32_t* key)
{
    Dmod_Timestamp_t start = Dmod_GetUptime();
    uint32_t size = 0;
    uint8_t* data = read_picture(c, index, &size);
    if (data == NULL)
        return false;
    *key = hash(data, size);
    if (all_made(c, *key))
    {
        Dmod_Free(data);
        return true;                    /* (another track of the album) */
    }
    Dmod_Timestamp_t read = Dmod_GetUptime();
    canvas_t cv;
    memset(&cv, 0, sizeof(cv));
    int ret = decode(c, data, size, &cv);
    Dmod_Free(data);
    Dmod_Timestamp_t decoded = Dmod_GetUptime();
    for (uint32_t i = 0; ret == 0 && i < c->size_count && !quitting(c); i++)
    {
        char* path = size_path(c, *key, &c->sizes[i]);
        bool there = path != NULL && Dmod_FileAvailable(path);
        if (path != NULL)
            Dmod_Free(path);
        if (!there)
            ret = make_size(c, &cv, *key, &c->sizes[i]);
    }
    if (cv.sums != NULL)
        Dmod_Free(cv.sums);
    if (ret != 0 || quitting(c))
    {
        if (ret != -ECANCELED && !quitting(c))
            DMOD_LOG_WARN("carhmi: the cover of track %u cannot be made (%d)\n", (unsigned)index, ret);
        return false;
    }
    DMOD_LOG_INFO("carhmi: cover %08x of track %u: %u bytes read in %u ms, %u x %u (1/%u) in %u ms, made in %u ms\n",
                  (unsigned)*key, (unsigned)index, (unsigned)size, (unsigned)(read - start), (unsigned)cv.decoded_w,
                  (unsigned)cv.decoded_h, (unsigned)cv.factor, (unsigned)(decoded - read), (unsigned)(Dmod_GetUptime() - decoded));
    return true;
}

/* ---- The thread ---- */

/* The decoders of the pictures (dmimg finds the enabled ones) */
static void load_decoders(void)
{
    static const char names[][12] = { "dmimg_jpeg", "dmimg_png" };     /* (text, not pointers: a module's data is not relocated) */
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        bool ok = (Dmod_GetModuleContext(names[i]) != NULL || Dmod_LoadModuleByName(names[i]) != NULL) &&
                  (Dmod_IsModuleEnabled(names[i]) || Dmod_EnableModule(names[i], false, NULL));
        if (!ok)
            DMOD_LOG_WARN("carhmi: no %s - its covers are not shown\n", names[i]);
    }
}

/* The track to make next (under the lock): the one asked first, else the first asked; -1: none */
static int32_t next_wanted(covers_t c)
{
    if (c->first >= 0 && c->states[c->first] == COVER_WANTED)
        return c->first;
    for (uint32_t i = 0; i < c->count; i++)
        if (c->states[i] == COVER_WANTED)
            return (int32_t)i;
    return -1;
}

/* ... of the rest: the next track nobody asked for, in their order; -1: all done */
static int32_t next_rest(covers_t c)
{
    for (; c->next_rest < c->count; c->next_rest++)
        if (c->states[c->next_rest] == COVER_UNKNOWN)
            return (int32_t)c->next_rest;
    return -1;
}

/*
 * A thread: the tracks asked for (SHOWN_PRIORITY: they are seen soon), or
 * the rest (REST_PRIORITY: from what no one else wants of the CPU) - every
 * cover made once is there the next time. A track is one thread's while it
 * is made (COVER_MAKING).
 */
static void worker_thread(void* arg)
{
    worker_t* w = arg;
    covers_t c = w->covers;
    for (;;)
    {
        dmosi_mutex_lock(c->lock);
        bool quit = c->quit;
        int32_t index = quit ? -1 : w->rest ? next_rest(c) : next_wanted(c);
        if (index >= 0)
            c->states[index] = COVER_MAKING;
        dmosi_mutex_unlock(c->lock);
        if (quit)
            break;
        if (index < 0)
        {
            if (w->rest)
                dmosi_thread_sleep(IDLE_MS);
            else
                (void)dmosi_semaphore_wait(c->wake, 1, (int32_t)IDLE_MS);
            continue;
        }
        uint32_t key = 0;
        bool made = make(c, (uint32_t)index, &key);
        if (quitting(c))
            break;                      /* (stopped halfway: nothing is known) */
        dmosi_mutex_lock(c->lock);
        c->states[index] = made ? COVER_MADE : COVER_NONE;
        c->keys[index] = made ? key : 0u;
        dmosi_mutex_unlock(c->lock);
    }
}

/* ---- API ---- */

covers_t covers_new(player_t p, const char* dir, const char* sizes)
{
    if (p == NULL || dir == NULL || sizes == NULL)
        return NULL;
    covers_t c = Dmod_Malloc(sizeof(*c));
    if (c == NULL)
        return NULL;
    memset(c, 0, sizeof(*c));
    c->player = p;
    c->first = -1;
    c->count = player_count(p);
    c->dir = Dmod_StrDup(dir);
    c->lock = dmosi_mutex_create(false);
    c->wake = dmosi_semaphore_create(0, WAKE_MAX);
    size_t n = (c->count > 0) ? c->count : 1u;
    c->states = Dmod_Malloc(n);
    c->keys = Dmod_Malloc(n * sizeof(uint32_t));
    if (c->dir == NULL || c->lock == NULL || c->wake == NULL || c->states == NULL || c->keys == NULL || !read_sizes(c, sizes))
    {
        covers_free(c);
        return NULL;
    }
    memset(c->states, COVER_UNKNOWN, n);
    memset(c->keys, 0, n * sizeof(uint32_t));
    void* there = Dmod_OpenDir(dir);
    if (there != NULL)
        Dmod_CloseDir(there);
    else
        (void)Dmod_MakeDir(dir, 0755);
    load_decoders();
    for (uint32_t i = 0; i < WORKERS; i++)
    {
        worker_t* w = &c->workers[i];
        w->covers = c;
        w->rest = i > 0;
        w->thread = dmosi_thread_create(worker_thread, w, w->rest ? REST_PRIORITY : SHOWN_PRIORITY, THREAD_STACK,
                                        w->rest ? "carhmi_covers_rest" : "carhmi_covers", dmosi_process_current());
        if (w->thread == NULL)
        {
            covers_free(c);
            return NULL;
        }
    }
    return c;
}

void covers_free(covers_t c)
{
    if (c == NULL)
        return;
    if (c->lock != NULL)
    {
        dmosi_mutex_lock(c->lock);
        c->quit = true;
        dmosi_mutex_unlock(c->lock);
    }
    if (c->wake != NULL)
        (void)dmosi_semaphore_post(c->wake, 1);
    for (uint32_t i = 0; i < WORKERS; i++)
    {
        if (c->workers[i].thread != NULL)
        {
            (void)dmosi_thread_join(c->workers[i].thread);
            dmosi_thread_destroy(c->workers[i].thread);
        }
    }
    if (c->wake != NULL)
        dmosi_semaphore_destroy(c->wake);
    if (c->lock != NULL)
        dmosi_mutex_destroy(c->lock);
    for (uint32_t i = 0; c->sizes != NULL && i < c->size_count; i++)
        Dmod_Free(c->sizes[i].tail);
    if (c->sizes != NULL)
        Dmod_Free(c->sizes);
    if (c->states != NULL)
        Dmod_Free(c->states);
    if (c->keys != NULL)
        Dmod_Free(c->keys);
    if (c->dir != NULL)
        Dmod_Free(c->dir);
    Dmod_Free(c);
}

void covers_want(covers_t c, uint32_t index, bool first)
{
    if (c == NULL || index >= c->count)
        return;
    bool wake = false;
    dmosi_mutex_lock(c->lock);
    if (c->states[index] == COVER_UNKNOWN)
    {
        c->states[index] = COVER_WANTED;
        wake = true;
    }
    if (first)
        c->first = (int32_t)index;
    dmosi_mutex_unlock(c->lock);
    if (wake)
        (void)dmosi_semaphore_post(c->wake, 1);
}

size_t covers_name_size(covers_t c)
{
    return (c != NULL) ? strlen(c->dir) + 1U + KEY_DIGITS + 1U : 1u;
}

uint32_t covers_key(covers_t c, uint32_t index)
{
    if (c == NULL || index >= c->count)
        return 0;
    dmosi_mutex_lock(c->lock);
    uint32_t key = (c->states[index] == COVER_MADE) ? c->keys[index] : 0u;
    dmosi_mutex_unlock(c->lock);
    return key;
}

void covers_name(covers_t c, uint32_t key, char* name, size_t size)
{
    if (c != NULL)
        Dmod_SnPrintf(name, size, "%s/%08x", c->dir, (unsigned)key);
}
