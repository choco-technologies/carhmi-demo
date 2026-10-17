#ifndef CARHMI_VOLUME_H
#define CARHMI_VOLUME_H

#include "dmod.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * The volume of carhmi: the master volume of the audio device (libdmaudio),
 * and what is kept of it between starts - a file `.volume` next to the music.
 *
 * The device may not be there yet when carhmi starts (its service starts
 * together with this one): volume_poll() sets it as soon as it can. The
 * file is written a while after the last change, not at every tap.
 */

/** Volume at the first start */
#define VOLUME_DEFAULT      60u

/** How long after the last change the file is written */
#define VOLUME_SAVE_MS      2000u

typedef struct volume* volume_t;

/**
 * @brief The volume kept in `dir` (VOLUME_DEFAULT when there is none), not muted. NULL: out of memory.
 *
 * @param dir The directory of the file (NULL: nothing is kept)
 */
volume_t volume_new(const char* dir);

/** @brief Write what was not written and free it */
void volume_free(volume_t v);

/** @brief The level, 0 ... 100 */
uint8_t volume_level(volume_t v);

/** @brief Whether the sound is muted */
bool volume_muted(volume_t v);

/** @brief Set the level (above 100: 100); a new level turns the sound on again */
void volume_set(volume_t v, int32_t level);

/** @brief Mute, or turn the sound on */
void volume_toggle_mute(volume_t v);

/**
 * @brief Do what is due: set the device, write the file
 *
 * @param now Dmod_GetUptime(): when it is called
 *
 * @return true when the device has what the volume is
 */
bool volume_poll(volume_t v, Dmod_Timestamp_t now);

#endif /* CARHMI_VOLUME_H */
