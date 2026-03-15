#ifndef BOARD_RULES_H
#define BOARD_RULES_H

#include "map.h"
#include <raylib.h>

bool boardEdgeTouchesCorner(int edgeTileId, int sideIndex, int cornerTileId, int cornerIndex, Vector2 origin, float radius);
bool boardIsValidRoadPlacement(const struct Map *map, int tileId, int sideIndex, enum PlayerType player, Vector2 origin, float radius);
bool boardIsValidSettlementPlacement(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player, Vector2 origin, float radius);
bool boardIsValidCityPlacement(const struct Map *map, int tileId, int cornerIndex, enum PlayerType player);

#endif
