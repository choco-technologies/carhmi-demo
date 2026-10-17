#ifndef CARHMI_PLAYSTATE_H
#define CARHMI_PLAYSTATE_H

#include "dmod.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Where carhmi was in the music - the track (its file's name), where in it,
 * whether it played -, kept in a file `.playing` next to the music and read
 * when carhmi starts, so a reset goes on where it was. Written at once when
 * the track or the playing changed, and every PLAYSTATE_SAVE_MS while it
 * plays: a reset loses at most that much of a track.
 */

/** How often the place in a track is written while it plays */
#define PLAYSTATE_SAVE_MS   15000u

typedef struct playstate* playstate_t;

/**
 * @brief What was kept in `dir` (nothing: playstate_saved() false). NULL: out of memory.
 *
 * @param dir The directory of the file (NULL: nothing is kept)
 */
playstate_t playstate_new(const char* dir);

/** @brief Write what was not written and free it */
void playstate_free(playstate_t s);

/**
 * @brief What was kept when it was made
 *
 * @param file  Receives the track's file name (as long as the playstate is, until playstate_update())
 * @return false: nothing was kept
 */
bool playstate_saved(playstate_t s, const char** file, uint32_t* position_ms, bool* playing);

/**
 * @brief Where the player is now: written when the track or the playing
 *        changed, else every PLAYSTATE_SAVE_MS while it plays
 *
 * @param file  The track's file name, NULL: none
 * @param now   Dmod_GetUptime()
 */
void playstate_update(playstate_t s, const char* file, uint32_t position_ms, bool playing, Dmod_Timestamp_t now);

#endif /* CARHMI_PLAYSTATE_H */
