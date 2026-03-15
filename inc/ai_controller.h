#ifndef AI_CONTROLLER_H
#define AI_CONTROLLER_H

#include "map.h"

/* Resets AI pacing and per-turn state, typically when starting or restarting a match. */
void aiResetController(void);

/* Configures the map for the current hotseat mode with all players controlled by humans. */
void aiConfigureHotseatMatch(struct Map *map);

/* Configures a single-human match with the selected human color and all other players controlled by AI opponents. */
void aiConfigureAIMatch(struct Map *map, enum PlayerType humanPlayer, enum AiDifficulty difficulty);

/* Returns true when the currently actionable game decision belongs to an AI-controlled player. */
bool aiControlsActiveDecision(const struct Map *map);

/* Advances one AI decision when the current game state is AI-controlled. */
void aiUpdateTurn(struct Map *map);

/* Returns true when an AI target accepts a player-trade offer using the same give/receive perspective as gameTryTradeWithPlayer(). */
bool aiShouldAcceptPlayerTradeOffer(const struct Map *map, enum PlayerType aiPlayer, enum ResourceType give, int giveAmount, enum ResourceType receive, int receiveAmount);

/* Returns a short label for the requested AI difficulty. */
const char *aiDifficultyLabel(enum AiDifficulty difficulty);

#endif
