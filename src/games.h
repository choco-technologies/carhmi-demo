#ifndef CARHMI_GAMES_H
#define CARHMI_GAMES_H
#include "player.h"
#include "sfx.h"
#include "libdmview.h"
typedef struct games* games_t;
games_t games_new(uint32_t claim, const char* view_path, player_t music, sfx_t sounds);
bool games_event(games_t games, const libdmview_event_t* event, uint32_t now);
void games_update(games_t games, uint32_t now);
bool games_is_open(games_t games);
void games_free(games_t games);
#endif
