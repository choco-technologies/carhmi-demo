#include "games.h"
#include "letters.h"
#include "lesson_audio.h"
#include <string.h>

struct games {
    uint32_t claim, revision;
    letters_t letters;
    lesson_audio_t audio;
    player_t music;
    sfx_t sounds;
    bool open, resume_music, resume_siren;
};
static void set_int(games_t g, const char* name, int32_t value) { (void)libdmview_prop_set_int(g->claim, name, value); }
static void set_text(games_t g, const char* name, const char* value) { (void)libdmview_prop_set_string(g->claim, name, value); }
static void show(games_t g)
{
    const letters_t* l = &g->letters;
    const letters_word_t* w = &letters_words[l->word];
    static const char titles[][32] = { "Pierwsza literka", "Znajdź obrazek", "Ułóż słowo", "Czytamy!" };
    set_int(g, "game.active", l->active); set_int(g, "game.mode", l->mode);
    set_int(g, "game.solved", l->solved); set_int(g, "game.wrong", l->wrong);
    set_int(g, "game.round", (int32_t)l->round);
    set_int(g, "game.length", l->length);
    set_int(g, "game.used", l->used);
    set_text(g, "game.title", titles[l->mode]);
    set_text(g, "game.target", l->mode == LETTERS_READ ? w->upper : w->letter);
    set_text(g, "game.status", l->solved ? "Brawo! Świetnie!" : l->wrong ? "Spróbuj jeszcze raz" :
        l->mode == LETTERS_SPELL ? "Dotykaj literek po kolei" :
        l->mode == LETTERS_READ ? "Przeczytaj i wybierz obrazek" :
        l->mode == LETTERS_INITIAL ? "Jaką literką zaczyna się to słowo?" : "Co zaczyna się na tę literkę?");
    char text[80];
    Dmod_SnPrintf(text, sizeof(text), "Gwiazdki: %u", (unsigned)l->score);
    set_text(g, "game.score", text);
    Dmod_SnPrintf(text, sizeof(text), "lit_%s", w->id);
    set_text(g, "game.object", text); set_text(g, "game.spell_image", text);
    letters_assembled(l, text); set_text(g, "game.assembled", text);
    for (unsigned i = 0; i < LETTERS_CHOICES; i++) {
        char property[32];
        Dmod_SnPrintf(property, sizeof(property), "game.letter%u", i);
        set_text(g, property, l->mode == LETTERS_INITIAL ? letters_alphabet[l->options[i]] : "");
        Dmod_SnPrintf(property, sizeof(property), "game.picture%u", i);
        Dmod_SnPrintf(text, sizeof(text), "lit_%s", letters_words[l->options[i]].id);
        set_text(g, property, text);
    }
    for (unsigned i = 0; i < LETTERS_TILES; i++) {
        char property[32];
        Dmod_SnPrintf(property, sizeof(property), "game.tile%u", i);
        text[0] = 0;
        if (i < l->length && !(l->used & (1u << i))) letters_glyph(l->glyphs[l->order[i]], text);
        set_text(g, property, text);
    }
    /* One committed snapshot: UI handlers read all fields on this change. */
    set_int(g, "game.revision", (int32_t)++g->revision);
}
static void prompt(games_t g)
{
    const letters_t* l = &g->letters;
    char filename[64];
    if (l->mode == LETTERS_READ) strcpy(filename, "read.mp3");
    else if (l->mode == LETTERS_PICTURE) {
        unsigned letter = 0;
        while (strcmp(letters_alphabet[letter], letters_words[l->word].letter)) letter++;
        Dmod_SnPrintf(filename, sizeof(filename), "letter_%02u.mp3", letter);
    } else Dmod_SnPrintf(filename, sizeof(filename), "%s_%s.mp3",
                         l->mode == LETTERS_INITIAL ? "first" : "spell", letters_words[l->word].id);
    lesson_audio_play(g->audio, filename, false);
}
static void open_panel(games_t g)
{
    if (g->open) return;
    player_state_t state;
    memset(&state, 0, sizeof(state));
    if (g->music != NULL) player_state(g->music, &state);
    g->resume_music = state.playing;
    if (state.playing) player_toggle(g->music);
    g->resume_siren = sfx_siren_wanted(g->sounds);
    sfx_siren_set(g->sounds, false);
    sfx_engine_set(g->sounds, false);
    sfx_pump_set(g->sounds, false);
    g->open = true;
}
games_t games_new(uint32_t claim, const char* view_path, player_t music, sfx_t sounds)
{
    games_t g = Dmod_Malloc(sizeof(*g));
    if (g == NULL) return NULL;
    memset(g, 0, sizeof(*g));
    g->claim = claim; g->music = music; g->sounds = sounds;
    letters_init(&g->letters, (uint32_t)Dmod_GetUptime() ^ 0x4c495445u);
    const char* slash = strrchr(view_path, '/');
    char directory[192];
    Dmod_SnPrintf(directory, sizeof(directory), "%.*s/literki/audio", slash ? (int)(slash - view_path) : 1,
                  slash ? view_path : ".");
    g->audio = lesson_audio_new(directory);
    show(g);
    return g;
}
bool games_event(games_t g, const libdmview_event_t* event, uint32_t now)
{
    if (g == NULL || strncmp(event->name, "game.", 5)) return false;
    if (!strcmp(event->name, "game.enter")) {
        if (!g->open) { open_panel(g); lesson_audio_play(g->audio, "welcome.mp3", false); }
    } else if (!strcmp(event->name, "game.leave")) {
        if (g->open) {
            g->open = false;
            letters_leave(&g->letters); lesson_audio_cancel(g->audio);
            if (g->resume_music) player_toggle(g->music);
            sfx_siren_set(g->sounds, g->resume_siren);
            show(g);
        }
    } else if (!strcmp(event->name, "game.start")) {
        open_panel(g); letters_start(&g->letters, now); show(g); prompt(g);
    } else if (!strcmp(event->name, "game.back")) {
        letters_leave(&g->letters); lesson_audio_cancel(g->audio); show(g);
    } else if (!strcmp(event->name, "game.repeat")) {
        if (g->open && g->letters.active && !g->letters.solved) prompt(g);
    } else if (!strcmp(event->name, "game.undo")) {
        if (letters_undo(&g->letters)) show(g);
    } else if (!strcmp(event->name, "game.answer") && event->value >= 0) {
        letters_result_t result = letters_answer(&g->letters, (uint32_t)event->value / 16u,
                                                (unsigned)event->value % 16u, now);
        if (result != LETTERS_IGNORED) show(g);
        if (result == LETTERS_WON) lesson_audio_play(g->audio, "bravo.mp3", true);
        else if (result == LETTERS_WRONG) lesson_audio_play(g->audio, "again.mp3", false);
    }
    return true;
}
void games_update(games_t g, uint32_t now)
{
    if (g == NULL || (g->letters.solved && lesson_audio_busy(g->audio))) return;
    uint32_t previous = g->letters.round;
    if (letters_tick(&g->letters, now)) {
        show(g);
        if (g->letters.round != previous) prompt(g);
    }
}
bool games_is_open(games_t g) { return g != NULL && g->open; }
void games_free(games_t g)
{
    if (g == NULL) return;
    lesson_audio_free(g->audio);
    Dmod_Free(g);
}
