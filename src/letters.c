#include "letters.h"
#include <string.h>
#include "letters_data.inc"

static uint32_t random_value(letters_t* g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g->rng = x;
}
static void shuffle(letters_t* g, uint8_t* values, unsigned count)
{
    for (unsigned i = count; i > 1; i--) {
        unsigned j = random_value(g) % i;
        uint8_t t = values[i - 1]; values[i - 1] = values[j]; values[j] = t;
    }
}
static unsigned decode(const char* text, uint16_t* out)
{
    const unsigned char* p = (const unsigned char*)text;
    unsigned n = 0;
    while (*p && n < LETTERS_TILES) {
        uint16_t ch = *p++;
        if ((ch & 0xe0) == 0xc0 && *p) { ch = (uint16_t)((ch & 31) << 6); ch |= *p++ & 63; }
        else if ((ch & 0xf0) == 0xe0 && p[0] && p[1]) {
            ch = (uint16_t)((ch & 15) << 12); ch |= (uint16_t)(*p++ & 63) << 6; ch |= *p++ & 63;
        }
        out[n++] = ch;
    }
    return n;
}
void letters_glyph(uint16_t ch, char out[4])
{
    if (ch < 128) { out[0] = (char)ch; out[1] = 0; }
    else if (ch < 2048) { out[0] = (char)(0xc0 | (ch >> 6)); out[1] = (char)(0x80 | (ch & 63)); out[2] = 0; }
    else { out[0] = (char)(0xe0 | (ch >> 12)); out[1] = (char)(0x80 | ((ch >> 6) & 63)); out[2] = (char)(0x80 | (ch & 63)); out[3] = 0; }
}
void letters_assembled(const letters_t* g, char out[64])
{
    out[0] = 0;
    for (unsigned i = 0; i < g->length; i++) {
        char ch[4];
        if (i < g->filled) letters_glyph(g->glyphs[i], ch);
        else { ch[0] = '_'; ch[1] = 0; }
        strcat(out, ch);
        if (i + 1 < g->length) strcat(out, " ");
    }
}
static void next_round(letters_t* g)
{
    if (g->deck_at >= letters_word_count) {
        for (unsigned i = 0; i < letters_word_count; i++) g->deck[i] = (uint8_t)i;
        shuffle(g, g->deck, letters_word_count);
        if (g->deck[0] == (uint8_t)g->last_word) {
            uint8_t t = g->deck[0]; g->deck[0] = g->deck[1]; g->deck[1] = t;
        }
        g->deck_at = 0;
    }
    if (g->mode_at >= 4) {
        for (unsigned i = 0; i < 4; i++) g->modes[i] = (uint8_t)i;
        shuffle(g, g->modes, 4);
        if (g->modes[0] == (uint8_t)g->last_mode) {
            uint8_t t = g->modes[0]; g->modes[0] = g->modes[1]; g->modes[1] = t;
        }
        g->mode_at = 0;
    }
    g->word = g->deck[g->deck_at++]; g->last_word = (int8_t)g->word;
    g->mode = g->modes[g->mode_at++]; g->last_mode = (int8_t)g->mode;
    /* Packed into an int event with a 4-bit choice. Wrap well below INT_MAX. */
    g->round = (g->round % 0x07fffffeu) + 1u;
    g->solved = g->wrong = false;
    g->filled = 0; g->used = 0;
    g->length = (uint8_t)decode(letters_words[g->word].upper, g->glyphs);
    for (unsigned i = 0; i < g->length; i++) g->order[i] = (uint8_t)i;
    shuffle(g, g->order, g->length);
    /* Never present the spelling tiles already in the requested order. */
    if (g->mode == LETTERS_SPELL) {
        bool ordered = true;
        for (unsigned i = 0; i < g->length; i++)
            ordered = ordered && g->glyphs[g->order[i]] == g->glyphs[i];
        if (ordered) {
            for (unsigned i = 1; i < g->length; i++) {
                if (g->glyphs[g->order[i]] != g->glyphs[g->order[0]]) {
                    uint8_t t = g->order[0]; g->order[0] = g->order[i]; g->order[i] = t; break;
                }
            }
        }
    }
    g->correct = (uint8_t)(random_value(g) % LETTERS_CHOICES);
    if (g->mode == LETTERS_INITIAL) {
        unsigned correct_letter = 0;
        while (strcmp(letters_alphabet[correct_letter], letters_words[g->word].letter) != 0) correct_letter++;
        for (unsigned i = 0; i < LETTERS_CHOICES; i++) {
            unsigned letter;
            if (i == g->correct) letter = correct_letter;
            else {
                bool duplicate;
                do {
                    letter = random_value(g) % letters_alphabet_count;
                    duplicate = letter == correct_letter;
                    for (unsigned j = 0; j < i; j++) duplicate = duplicate || g->options[j] == letter;
                } while (duplicate);
            }
            g->options[i] = (uint8_t)letter;
        }
    } else {
        for (unsigned i = 0; i < LETTERS_CHOICES; i++) {
            unsigned word;
            if (i == g->correct) word = g->word;
            else {
                bool duplicate;
                do {
                    word = random_value(g) % letters_word_count;
                    duplicate = strcmp(letters_words[word].letter, letters_words[g->word].letter) == 0;
                    for (unsigned j = 0; j < i; j++) duplicate = duplicate || g->options[j] == word;
                } while (duplicate);
            }
            g->options[i] = (uint8_t)word;
        }
    }
}
void letters_init(letters_t* g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x4c495445u;
    g->deck_at = (uint8_t)letters_word_count; g->mode_at = 4;
    g->last_word = g->last_mode = -1;
}
void letters_start(letters_t* g, uint32_t now)
{
    (void)now;
    g->active = true; g->score = 0;
    next_round(g);
}
void letters_leave(letters_t* g) { g->active = g->solved = g->wrong = false; }
bool letters_tick(letters_t* g, uint32_t now)
{
    if (!g->active || (!g->solved && !g->wrong) || (int32_t)(now - g->deadline) < 0) return false;
    if (g->solved) next_round(g);
    else g->wrong = false;
    return true;
}
letters_result_t letters_answer(letters_t* g, uint32_t round, unsigned choice, uint32_t now)
{
    if (!g->active || g->solved || round != g->round) return LETTERS_IGNORED;
    if (g->mode == LETTERS_SPELL) {
        if (choice >= g->length || (g->used & (1u << choice))) return LETTERS_IGNORED;
        if (g->glyphs[g->order[choice]] != g->glyphs[g->filled]) goto wrong;
        g->used |= (uint16_t)(1u << choice);
        g->placed[g->filled++] = (uint8_t)choice;
        g->wrong = false;
        if (g->filled < g->length) return LETTERS_PLACED;
    } else {
        if (choice >= LETTERS_CHOICES) return LETTERS_IGNORED;
        if (choice != g->correct) goto wrong;
    }
    g->solved = true; g->wrong = false; g->score++;
    g->deadline = now + LETTERS_REWARD_MS;
    return LETTERS_WON;
wrong:
    g->wrong = true; g->deadline = now + 1100u;
    return LETTERS_WRONG;
}
bool letters_undo(letters_t* g)
{
    if (!g->active || g->solved || g->mode != LETTERS_SPELL || !g->filled) return false;
    g->used &= (uint16_t)~(1u << g->placed[--g->filled]);
    g->wrong = false;
    return true;
}
