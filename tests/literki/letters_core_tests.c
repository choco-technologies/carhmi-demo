#include "letters.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void solve(letters_t* g, uint32_t now)
{
    uint32_t score = g->score;
    if (g->mode == LETTERS_SPELL) {
        while (!g->solved) {
            unsigned tile = 0;
            while (tile < g->length && ((g->used & (1u << tile)) ||
                   g->glyphs[g->order[tile]] != g->glyphs[g->filled])) tile++;
            assert(tile < g->length);
            letters_result_t result = letters_answer(g, g->round, tile, now);
            assert(result == (g->filled == g->length ? LETTERS_WON : LETTERS_PLACED));
            if (!g->solved) {
                unsigned filled = g->filled;
                assert(letters_answer(g, g->round, tile, now) == LETTERS_IGNORED);
                assert(g->filled == filled);
            }
        }
    } else assert(letters_answer(g, g->round, g->correct, now) == LETTERS_WON);
    assert(g->score == score + 1);
    assert(letters_answer(g, g->round, g->correct, now) == LETTERS_IGNORED);
    assert(g->score == score + 1);
}
int main(void)
{
    assert(letters_word_count <= LETTERS_MAX_WORDS);
    assert(letters_word_count == letters_alphabet_count * 3);
    bool mama = false, tata = false, dom = false;
    for (unsigned i = 0; i < letters_word_count; i++) {
        mama |= !strcmp(letters_words[i].id, "mama");
        tata |= !strcmp(letters_words[i].id, "tata");
        dom |= !strcmp(letters_words[i].id, "dom");
    }
    assert(mama && tata && dom);
    unsigned completed = 0, spelling = 0, polish = 0, repeated = 0;
    for (unsigned seed = 0; seed < 40; seed++) {
        letters_t g;
        letters_init(&g, seed);
        assert(letters_answer(&g, 0, 0, 0) == LETTERS_IGNORED);
        uint32_t now = 0xffffe000u;
        letters_start(&g, now);
        unsigned seen[LETTERS_MAX_WORDS] = {0}, mode_mask = 0;
        int previous_word = -1, previous_mode = -1;
        for (unsigned n = 0; n < letters_word_count * 3; n++) {
            assert(g.word != previous_word && g.mode != previous_mode);
            previous_word = g.word; previous_mode = g.mode;
            mode_mask |= 1u << g.mode;
            if (n < letters_word_count) seen[g.word]++;
            if (n % 4 == 3) { assert(mode_mask == 15); mode_mask = 0; }
            uint32_t round = g.round;
            assert(letters_answer(&g, round - 1, 0, now) == LETTERS_IGNORED);
            assert(letters_answer(&g, round, 16, now) == LETTERS_IGNORED);
            if (g.mode == LETTERS_INITIAL) {
                assert(!strcmp(letters_alphabet[g.options[g.correct]], letters_words[g.word].letter));
                for (unsigned i = 0; i < 3; i++) for (unsigned j = i + 1; j < 3; j++) assert(g.options[i] != g.options[j]);
            } else if (g.mode != LETTERS_SPELL) {
                assert(g.options[g.correct] == g.word);
                for (unsigned i = 0; i < 3; i++) if (i != g.correct)
                    assert(strcmp(letters_words[g.options[i]].letter, letters_words[g.word].letter));
            }
            if (g.mode == LETTERS_SPELL) {
                spelling++;
                unsigned counts[LETTERS_TILES] = {0}; bool in_order = true;
                for (unsigned i = 0; i < g.length; i++) {
                    counts[g.order[i]]++;
                    in_order &= g.glyphs[g.order[i]] == g.glyphs[i];
                    polish += g.glyphs[i] > 127;
                    for (unsigned j = 0; j < i; j++) repeated += g.glyphs[i] == g.glyphs[j];
                }
                for (unsigned i = 0; i < g.length; i++) assert(counts[i] == 1);
                assert(!in_order);
                unsigned first = 0;
                while (g.glyphs[g.order[first]] != g.glyphs[0]) first++;
                assert(letters_answer(&g, round, first, now) == LETTERS_PLACED);
                assert(letters_undo(&g) && g.filled == 0 && g.used == 0);
                assert(!letters_undo(&g));
            } else {
                assert(letters_answer(&g, round, (g.correct + 1) % 3, now) == LETTERS_WRONG);
                assert(g.wrong && !g.solved);
                assert(!letters_tick(&g, now + 1099));
                assert(letters_tick(&g, now + 1100) && !g.wrong && g.round == round);
            }
            solve(&g, now);
            assert(!letters_tick(&g, now + LETTERS_REWARD_MS - 1));
            assert(letters_tick(&g, now + LETTERS_REWARD_MS));
            assert(g.round != round && !g.solved && g.active);
            now += 5000;
            completed++;
        }
        for (unsigned i = 0; i < letters_word_count; i++) assert(seen[i] == 1);
        letters_leave(&g);
        assert(!letters_tick(&g, now + 5000));
        assert(letters_answer(&g, g.round, 0, now) == LETTERS_IGNORED);
    }
    assert(spelling > 0 && polish > 0 && repeated > 0);
    printf("PASS: %u rounds, %u spelling tasks, %u Polish glyphs, %u repeated letters\n", completed, spelling, polish, repeated);
    return 0;
}
