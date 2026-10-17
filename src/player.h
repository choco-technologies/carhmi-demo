#ifndef CARHMI_PLAYER_H
#define CARHMI_PLAYER_H

#include "dmod.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * The MP3 player of carhmi: the files of a directory, played one after the
 * other by a thread of its own - decoded by dmmp3, played through libdmaudio
 * (the device's mixer paces it).
 */

/** What a track shows. Its texts stay as they are until player_free() */
typedef struct
{
    const char* title;          /**< The tag, else the file's name (without .mp3) */
    const char* artist;         /**< "" without the tag */
    uint32_t    duration_ms;    /**< 0: not known */
} player_info_t;

typedef struct player* player_t;

/** @brief What the player is at: a snapshot */
typedef struct
{
    int32_t     track;          /**< -1: none */
    bool        playing;
    uint32_t    position_ms;
    uint32_t    duration_ms;
    uint32_t    changes;        /**< Grows when the track, or playing, changed */
} player_state_t;

/**
 * @brief The MP3 files of `dir` (sorted by name), a thread to play them. NULL: out of memory.
 *
 * The files are a list (dmlist) of what each takes - its path and names on
 * the heap -, as many as there are; a track's tags are read when it is
 * shown (player_info()) or played, not all at the start.
 */
player_t player_new(const char* dir);
void player_free(player_t p);

uint32_t player_count(player_t p);
/** @brief What track `index` shows - its tags read the first time it is asked. false: no such track */
bool player_info(player_t p, uint32_t index, player_info_t* info);

void player_play(player_t p, uint32_t index);      /* From its start */
void player_toggle(player_t p);
void player_next(player_t p, int32_t step);         /* +1 / -1, around */
void player_state(player_t p, player_state_t* state);

#endif /* CARHMI_PLAYER_H */
