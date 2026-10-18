#ifndef CARHMI_COVERS_H
#define CARHMI_COVERS_H

#include "dmod.h"
#include "player.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * The album covers of the tracks: the pictures of their ID3 tags (dmmp3),
 * made into the images the view shows - at each size the view says - by a
 * thread of their own, and kept in a directory next to the music: the files
 * <dir>/<key>-<size>.dmvi, where the key is a hash of the picture (the tracks
 * of an album share their files). A cover made once is not made again.
 *
 * The view names an image by a property (data-dmod-src): "<dir>/<key>" -
 * covers_name() - shows the cover, the view's own name its default image.
 * The view's property <name>.sizes says what to make: the ends of the files'
 * names ("-88x88-k0-0-88x88-r11.dmvi -430x430-b40.dmvi").
 */

/** Of the picture of a tag, at most: a larger one is not read */
#define COVERS_MAX_PICTURE      (3u * 1024u * 1024u)

typedef struct covers* covers_t;

/**
 * @brief The covers of the tracks of `p`, made in `dir` (made when it is not
 *        there) at `sizes` - the ends of the files' names, separated by
 *        spaces. NULL: out of memory, no sizes
 */
covers_t covers_new(player_t p, const char* dir, const char* sizes);

/** @brief Stop the thread and free it (safe on NULL) */
void covers_free(covers_t c);

/**
 * @brief Ask for the cover of track `index`: it is made when the thread gets
 *        to it - the one asked `first` before all the others
 */
void covers_want(covers_t c, uint32_t index, bool first);

/** @brief The key of the cover of track `index`; 0: none - not made (yet), no picture, it cannot be made */
uint32_t covers_key(covers_t c, uint32_t index);

/** @brief How many of the tracks are done: their covers made, or found to be none */
uint32_t covers_done(covers_t c);

/** @brief Bytes covers_name() writes, with its zero */
size_t covers_name_size(covers_t c);

/** @brief The name of the cover `key` as the view takes it: "<dir>/<key>" */
void covers_name(covers_t c, uint32_t key, char* name, size_t size);

#endif /* CARHMI_COVERS_H */
