#include "lesson_audio.h"
#include "dmod.h"
#include "dmosi.h"
#include "dmmp3.h"
#include "libdmaudio.h"
#include "sfx_synth.h"
#include <errno.h>
#include <string.h>

#define FRAMES 441u
#define RATE 22050u
struct lesson_audio {
    dmosi_mutex_t lock;
    dmosi_semaphore_t wake;
    dmosi_thread_t thread;
    char directory[192], file[64];
    uint32_t serial, frames;
    bool reward, quit, busy;
};
static bool current(lesson_audio_t a, uint32_t serial)
{
    dmosi_mutex_lock(a->lock);
    bool ok = !a->quit && a->serial == serial;
    dmosi_mutex_unlock(a->lock);
    return ok;
}
static bool write_pcm(lesson_audio_t a, uint32_t serial, libdmaudio_stream_t stream,
                      const int16_t* pcm, uint32_t frames, unsigned channels)
{
    while (frames && current(a, serial)) {
        int32_t written = libdmaudio_write(stream, pcm, frames, 100);
        if (written == -ETIMEDOUT) continue;
        if (written <= 0) return false;
        dmosi_mutex_lock(a->lock);
        if (a->serial == serial) a->frames += (uint32_t)written;
        dmosi_mutex_unlock(a->lock);
        frames -= (uint32_t)written; pcm += written * channels;
    }
    return current(a, serial);
}
static void drain(lesson_audio_t a, uint32_t serial, libdmaudio_stream_t stream)
{
    while (current(a, serial)) {
        int result = libdmaudio_drain(stream, 100);
        if (result != -ETIMEDOUT) break;
    }
}
/* A bright four-note reward followed by several overlapping handclaps.
 * It works without any voice recordings and uses no MP3 samples or libm. */
static void applause(lesson_audio_t a, uint32_t serial, int16_t* pcm)
{
    libdmaudio_format_t format = { RATE, 1, DMDRVI_AUDIO_FORMAT_S16_LE };
    libdmaudio_stream_options_t options = { 180, 80 };
    int status = 0;
    libdmaudio_stream_t stream = libdmaudio_open(NULL, &format, &options, &status);
    if (stream == NULL) return;
    uint32_t noise = 0xabc123u;
    const float notes[] = { 523.25f, 659.25f, 783.99f, 1046.5f };
    float previous_noise = 0;
    for (unsigned chunk = 0; chunk < 90 && current(a, serial); chunk++) {
        for (unsigned i = 0; i < FRAMES; i++) {
            unsigned sample = chunk * FRAMES + i;
            float t = (float)sample / RATE, value = 0;
            if (t < 0.64f) {
                unsigned note = sample / 3528u;
                float local = (float)(sample % 3528u) / 3528.0f;
                value += sfx_synth_sin(t * notes[note < 4 ? note : 3]) * (1.0f - local) * 0.28f;
            }
            if (t > 0.38f) {
                noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
                float white = (float)(int16_t)(noise & 0xffff) / 32768.0f;
                float envelope = 0;
                for (unsigned hand = 0; hand < 4; hand++) {
                    unsigned period = 2447u + hand * 431u;
                    unsigned phase = (sample + hand * 809u) % period;
                    if (phase < 700u) envelope += (1.0f - (float)phase / 700.0f) * 0.12f;
                }
                float fade = t < 1.45f ? 1.0f : (1.8f - t) / 0.35f;
                if (fade < 0) fade = 0;
                value += (white - previous_noise * 0.65f) * envelope * fade;
                previous_noise = white;
            }
            if (value > 0.8f) value = 0.8f;
            if (value < -0.8f) value = -0.8f;
            pcm[i] = (int16_t)(value * 24000.0f);
        }
        if (!write_pcm(a, serial, stream, pcm, FRAMES, 1)) break;
    }
    drain(a, serial, stream);
    libdmaudio_close(stream);
}
static void voice(lesson_audio_t a, uint32_t serial, const char* filename, int16_t* pcm)
{
    if (!filename[0] || !current(a, serial)) return;
    char path[272];
    Dmod_SnPrintf(path, sizeof(path), "%s/%s", a->directory, filename);
    /* Voice files are supplied separately. Missing speech never blocks play. */
    if (!Dmod_FileAvailable(path)) return;
    int status = 0;
    dmmp3_t decoder = dmmp3_open(path, &status);
    if (decoder == NULL) return;
    dmmp3_info_t info;
    memset(&info, 0, sizeof(info));
    libdmaudio_stream_t stream = NULL;
    if (dmmp3_get_info(decoder, &info) == 0) {
        libdmaudio_format_t format = { info.sample_rate_hz, info.channels, DMDRVI_AUDIO_FORMAT_S16_LE };
        libdmaudio_stream_options_t options = { 350, 100 };
        stream = libdmaudio_open(NULL, &format, &options, &status);
    }
    while (stream != NULL && current(a, serial)) {
        int32_t frames = dmmp3_read(decoder, pcm, 1152);
        if (frames <= 0 || !write_pcm(a, serial, stream, pcm, (uint32_t)frames, info.channels)) break;
    }
    if (stream != NULL) drain(a, serial, stream);
    libdmaudio_close(stream);
    dmmp3_close(decoder);
}
static void audio_thread(void* data)
{
    lesson_audio_t a = data;
    int16_t* pcm = Dmod_Malloc(1152u * 2u * sizeof(*pcm));
    uint32_t handled = 0;
    if (pcm == NULL) {
        dmosi_mutex_lock(a->lock); a->quit = true; dmosi_mutex_unlock(a->lock);
    }
    while (pcm != NULL) {
        dmosi_mutex_lock(a->lock);
        bool quit = a->quit, reward = a->reward;
        uint32_t serial = a->serial;
        char filename[64];
        strcpy(filename, a->file);
        dmosi_mutex_unlock(a->lock);
        if (quit) break;
        if (serial == handled) { (void)dmosi_semaphore_wait(a->wake, 1, 100); continue; }
        handled = serial;
        if (reward) applause(a, serial, pcm);
        voice(a, serial, filename, pcm);
        dmosi_mutex_lock(a->lock);
        if (a->serial == serial) a->busy = false;
        dmosi_mutex_unlock(a->lock);
    }
    Dmod_Free(pcm);
    dmosi_mutex_lock(a->lock); a->busy = false; dmosi_mutex_unlock(a->lock);
}
lesson_audio_t lesson_audio_new(const char* directory)
{
    lesson_audio_t a = Dmod_Malloc(sizeof(*a));
    if (a == NULL) return NULL;
    memset(a, 0, sizeof(*a));
    Dmod_SnPrintf(a->directory, sizeof(a->directory), "%s", directory);
    a->lock = dmosi_mutex_create(false); a->wake = dmosi_semaphore_create(0, 1);
    if (a->lock != NULL && a->wake != NULL)
        a->thread = dmosi_thread_create(audio_thread, a, 2, 4096, "carhmi_lesson", dmosi_process_current());
    if (a->thread == NULL) { lesson_audio_free(a); return NULL; }
    return a;
}
void lesson_audio_play(lesson_audio_t a, const char* file, bool reward)
{
    if (a == NULL) return;
    dmosi_mutex_lock(a->lock);
    if (a->quit) { dmosi_mutex_unlock(a->lock); return; }
    Dmod_SnPrintf(a->file, sizeof(a->file), "%s", file ? file : "");
    a->reward = reward; a->busy = true; a->frames = 0; a->serial++;
    dmosi_mutex_unlock(a->lock);
    (void)dmosi_semaphore_post(a->wake, 1);
}
void lesson_audio_cancel(lesson_audio_t a) { lesson_audio_play(a, "", false); }
bool lesson_audio_busy(lesson_audio_t a)
{
    if (a == NULL) return false;
    dmosi_mutex_lock(a->lock); bool busy = a->busy; dmosi_mutex_unlock(a->lock);
    return busy;
}
uint32_t lesson_audio_frames(lesson_audio_t a)
{
    if (a == NULL) return 0;
    dmosi_mutex_lock(a->lock); uint32_t frames = a->frames; dmosi_mutex_unlock(a->lock);
    return frames;
}
void lesson_audio_free(lesson_audio_t a)
{
    if (a == NULL) return;
    if (a->thread != NULL) {
        dmosi_mutex_lock(a->lock); a->quit = true; a->serial++; dmosi_mutex_unlock(a->lock);
        (void)dmosi_semaphore_post(a->wake, 1);
        (void)dmosi_thread_join(a->thread); dmosi_thread_destroy(a->thread);
    }
    if (a->wake != NULL) dmosi_semaphore_destroy(a->wake);
    if (a->lock != NULL) dmosi_mutex_destroy(a->lock);
    Dmod_Free(a);
}
