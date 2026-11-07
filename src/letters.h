#ifndef CARHMI_LETTERS_H
#define CARHMI_LETTERS_H
#include <stdbool.h>
#include <stdint.h>

#define LETTERS_MAX_WORDS 96
#define LETTERS_TILES 16
#define LETTERS_CHOICES 3
#define LETTERS_REWARD_MS 2100u

/* Inline UTF-8 fields keep this ROM table position independent, without
 * absolute pointers requiring loader relocations. */
typedef struct { char id[16], word[32], upper[32], letter[4]; } letters_word_t;
extern const letters_word_t letters_words[];
extern const unsigned letters_word_count;
extern const char letters_alphabet[][4];
extern const unsigned letters_alphabet_count;

typedef enum { LETTERS_INITIAL, LETTERS_PICTURE, LETTERS_SPELL, LETTERS_READ } letters_mode_t;
typedef enum { LETTERS_IGNORED, LETTERS_WRONG, LETTERS_PLACED, LETTERS_WON } letters_result_t;

typedef struct {
    uint32_t rng, round, score, deadline;
    bool active, solved, wrong;
    uint8_t mode, word, correct, length, filled;
    uint8_t options[LETTERS_CHOICES], order[LETTERS_TILES], placed[LETTERS_TILES];
    uint16_t glyphs[LETTERS_TILES], used;
    uint8_t deck[LETTERS_MAX_WORDS], deck_at, modes[4], mode_at;
    int8_t last_word, last_mode;
} letters_t;

void letters_init(letters_t* game, uint32_t seed);
void letters_start(letters_t* game, uint32_t now);
void letters_leave(letters_t* game);
/* New round after the celebration, or clearing the gentle retry hint. */
bool letters_tick(letters_t* game, uint32_t now);
letters_result_t letters_answer(letters_t* game, uint32_t round, unsigned choice, uint32_t now);
bool letters_undo(letters_t* game);
void letters_glyph(uint16_t glyph, char text[4]);
void letters_assembled(const letters_t* game, char text[64]);
#endif
