#ifndef CARHMI_LESSON_AUDIO_H
#define CARHMI_LESSON_AUDIO_H
#include <stdbool.h>
#include <stdint.h>
typedef struct lesson_audio* lesson_audio_t;
lesson_audio_t lesson_audio_new(const char* directory);
void lesson_audio_play(lesson_audio_t audio, const char* filename, bool reward);
void lesson_audio_cancel(lesson_audio_t audio);
bool lesson_audio_busy(lesson_audio_t audio);
/* PCM frames accepted for the current request; useful for audio diagnostics. */
uint32_t lesson_audio_frames(lesson_audio_t audio);
void lesson_audio_free(lesson_audio_t audio);
#endif
