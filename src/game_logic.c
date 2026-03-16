#include "game_logic.h"

#include "debug_log.h"
#include <raylib.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#define LAND_TILE_COUNT 19
#define HEX_CORNERS 6

struct AxialCoord
{
  int q;
  int r;
};

struct PortRule
{
  struct AxialCoord oceanCoord;
  struct AxialCoord landCoord;
  bool generic;
  enum ResourceType resource;
};

static const struct AxialCoord kLandCoords[LAND_TILE_COUNT] = {
    {0, -2}, {1, -2}, {2, -2}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}, {-2, 2}, {-1, 2}, {0, 2}};

static const struct PortRule kPorts[] = {
    {{0, -3}, {0, -2}, true, RESOURCE_WOOD},
    {{2, -3}, {2, -2}, false, RESOURCE_WHEAT},
    {{3, -2}, {2, -1}, true, RESOURCE_WOOD},
    {{3, 0}, {2, 0}, false, RESOURCE_STONE},
    {{1, 2}, {1, 1}, true, RESOURCE_WOOD},
    {{-1, 3}, {-1, 2}, false, RESOURCE_CLAY},
    {{-3, 2}, {-2, 1}, true, RESOURCE_WOOD},
    {{-3, 0}, {-2, 0}, false, RESOURCE_SHEEP},
    {{-1, -2}, {-1, -1}, false, RESOURCE_WOOD}};

static int setup_player_for_step(enum PlayerType startPlayer, int step);
static enum ResourceType resource_for_tile_type(enum TileType type);
static int resource_yield_for_structure(enum StructureType structure);
static void distribute_resources_for_roll(struct Map *map, int roll);
static bool can_afford_cost(const struct Map *map, int wood, int wheat, int clay, int sheep, int stone);
static bool try_pay_cost(struct Map *map, int wood, int wheat, int clay, int sheep, int stone);
static int find_land_tile_id(struct AxialCoord coord);
static int find_port_side_index(const struct PortRule *port);
static void get_side_corner_indices(int sideIndex, int *cornerA, int *cornerB);
static bool player_has_port(const struct Map *map, enum PlayerType player, enum ResourceType resource, bool *hasGeneric);
static Vector2 axial_to_world(struct AxialCoord coord, Vector2 origin, float radius);
static Vector2 point_on_hex(Vector2 center, float radius, int cornerIndex);
static void get_corner_key(Vector2 center, float radius, int cornerIndex, int *x, int *y);
static bool corner_keys_match(int x1, int y1, int x2, int y2);
static bool is_canonical_shared_corner(int tileId, int cornerIndex);
static int total_resources_for_player(const struct Map *map, enum PlayerType player);
static bool any_pending_discards(const struct Map *map);
static void begin_discard_phase(struct Map *map);
static bool player_touches_tile(const struct Map *map, enum PlayerType player, int tileId);
static bool player_has_any_resources(const struct Map *map, enum PlayerType player);
static void grant_setup_settlement_resources(struct Map *map, int tileId, int cornerIndex);
static int playable_development_card_count(const struct Map *map, enum PlayerType player, enum DevelopmentCardType type);
static bool can_play_development_card_type(const struct Map *map, enum DevelopmentCardType type);
static void consume_development_card(struct Map *map, enum DevelopmentCardType type);
static void get_road_edge_key(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by);
static bool road_edge_keys_match(int ax1, int ay1, int bx1, int by1, int ax2, int ay2, int bx2, int by2);
static enum PlayerType get_shared_corner_owner_by_key(const struct Map *map, int x, int y);
static int player_longest_road_length(const struct Map *map, enum PlayerType player);
static int dfs_longest_road(const int nodeX[], const int nodeY[], const bool nodeBlocked[], int nodeCount,
                            const int edgeA[], const int edgeB[], int edgeCount, int currentNode,
                            bool startingNode, bool usedEdges[MAX_SIDES]);
static void update_largest_army_owner(struct Map *map);
static void update_longest_road_owner(struct Map *map);

bool gameIsSetupSettlementTurn(const struct Map *map)
{
  return map != NULL && map->phase == GAME_PHASE_SETUP && !map->setupNeedsRoad;
}

bool gameIsSetupRoadTurn(const struct Map *map)
{
  return map != NULL && map->phase == GAME_PHASE_SETUP && map->setupNeedsRoad;
}

bool gameCanRollDice(const struct Map *map)
{
  return map != NULL &&
         map->phase == GAME_PHASE_PLAY &&
         !map->rolledThisTurn &&
         !any_pending_discards(map) &&
         !map->awaitingThiefPlacement &&
         !map->awaitingThiefVictimSelection;
}

bool gameCanEndTurn(const struct Map *map)
{
  return map != NULL &&
         map->phase == GAME_PHASE_PLAY &&
         map->rolledThisTurn &&
         !any_pending_discards(map) &&
         !map->awaitingThiefPlacement &&
         !map->awaitingThiefVictimSelection;
}

bool gameHasWinner(const struct Map *map)
{
  return map != NULL &&
         map->phase == GAME_PHASE_GAME_OVER &&
         map->winner >= PLAYER_RED &&
         map->winner <= PLAYER_BLACK;
}

enum PlayerType gameGetWinner(const struct Map *map)
{
  return gameHasWinner(map) ? map->winner : PLAYER_NONE;
}

bool gameCheckVictory(struct Map *map, enum PlayerType player)
{
  if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
  {
    return false;
  }

  if (gameHasWinner(map))
  {
    return map->winner == player;
  }

  if (gameComputeVictoryPoints(map, player) < 10)
  {
    return false;
  }

  map->winner = player;
  map->phase = GAME_PHASE_GAME_OVER;
  map->rolledThisTurn = false;
  map->awaitingThiefPlacement = false;
  map->awaitingThiefVictimSelection = false;
  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    map->discardRemaining[i] = 0;
  }
  return true;
}

bool gameHasPendingDiscards(const struct Map *map)
{
  return any_pending_discards(map);
}

enum PlayerType gameGetCurrentDiscardPlayer(const struct Map *map)
{
  if (map == NULL)
  {
    return PLAYER_NONE;
  }

  for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
  {
    if (map->discardRemaining[player] > 0)
    {
      return (enum PlayerType)player;
    }
  }

  return PLAYER_NONE;
}

int gameGetDiscardAmountForPlayer(const struct Map *map, enum PlayerType player)
{
  if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
  {
    return 0;
  }

  return map->discardRemaining[player];
}

bool gameTrySubmitDiscard(struct Map *map, enum PlayerType player, const int resources[5])
{
  if (map == NULL || resources == NULL || !any_pending_discards(map) ||
      player != gameGetCurrentDiscardPlayer(map) ||
      player < PLAYER_RED || player > PLAYER_BLACK)
  {
    return false;
  }

  int discardCount = 0;
  for (int resource = 0; resource < 5; resource++)
  {
    if (resources[resource] < 0 ||
        resources[resource] > map->players[player].resources[resource])
    {
      return false;
    }
    discardCount += resources[resource];
  }

  if (discardCount != map->discardRemaining[player])
  {
    return false;
  }

  for (int resource = 0; resource < 5; resource++)
  {
    map->players[player].resources[resource] -= resources[resource];
  }
  map->discardRemaining[player] = 0;

  if (!any_pending_discards(map))
  {
    map->awaitingThiefPlacement = true;
  }

  debugLog("GAME", "discard submitted player=%d plan=[%d,%d,%d,%d,%d] thiefPlacement=%d",
           player,
           resources[RESOURCE_WOOD],
           resources[RESOURCE_WHEAT],
           resources[RESOURCE_CLAY],
           resources[RESOURCE_SHEEP],
           resources[RESOURCE_STONE],
           map->awaitingThiefPlacement ? 1 : 0);

  return true;
}

bool gameNeedsThiefPlacement(const struct Map *map)
{
  return map != NULL &&
         map->phase == GAME_PHASE_PLAY &&
         map->awaitingThiefPlacement;
}

bool gameCanMoveThiefToTile(const struct Map *map, int tileId)
{
  return gameNeedsThiefPlacement(map) &&
         tileId >= 0 &&
         tileId < LAND_TILE_COUNT &&
         tileId != map->thiefTileId;
}

bool gameNeedsThiefVictimSelection(const struct Map *map)
{
  return map != NULL &&
         map->phase == GAME_PHASE_PLAY &&
         map->awaitingThiefVictimSelection;
}

bool gameCanStealFromPlayer(const struct Map *map, enum PlayerType victim)
{
  return gameNeedsThiefVictimSelection(map) &&
         victim >= PLAYER_RED &&
         victim <= PLAYER_BLACK &&
         victim != map->currentPlayer &&
         player_touches_tile(map, victim, map->thiefTileId) &&
         player_has_any_resources(map, victim);
}

void gameRollDice(struct Map *map, int diceRoll)
{
  const double started = GetTime();

  if (!gameCanRollDice(map))
  {
    debugLog("GAME", "gameRollDice ignored total=%d", diceRoll);
    return;
  }

  debugLog("GAME", "gameRollDice start player=%d total=%d", map->currentPlayer, diceRoll);
  map->lastDiceRoll = diceRoll;
  if (diceRoll == 7)
  {
    begin_discard_phase(map);
  }
  else
  {
    distribute_resources_for_roll(map, diceRoll);
  }
  map->rolledThisTurn = true;
  debugLog("GAME", "gameRollDice end player=%d total=%d elapsed=%.3f pendingDiscards=%d thiefPlacement=%d thiefVictim=%d",
           map->currentPlayer,
           diceRoll,
           GetTime() - started,
           any_pending_discards(map) ? 1 : 0,
           map->awaitingThiefPlacement ? 1 : 0,
           map->awaitingThiefVictimSelection ? 1 : 0);
}

void gameMoveThief(struct Map *map, int tileId)
{
  const double started = GetTime();

  if (!gameCanMoveThiefToTile(map, tileId))
  {
    debugLog("GAME", "move thief ignored player=%d tile=%d", map != NULL ? map->currentPlayer : PLAYER_NONE, tileId);
    return;
  }

  debugLog("GAME", "move thief start player=%d tile=%d", map->currentPlayer, tileId);
  map->thiefTileId = tileId;
  map->awaitingThiefPlacement = false;
  map->awaitingThiefVictimSelection = false;
  for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
  {
    if (player != map->currentPlayer &&
        player_touches_tile(map, (enum PlayerType)player, map->thiefTileId) &&
        player_has_any_resources(map, (enum PlayerType)player))
    {
      map->awaitingThiefVictimSelection = true;
      break;
    }
  }

  debugLog("GAME", "move thief end player=%d tile=%d elapsed=%.3f thiefVictim=%d",
           map->currentPlayer,
           tileId,
           GetTime() - started,
           map->awaitingThiefVictimSelection ? 1 : 0);
}

bool gameStealRandomResource(struct Map *map, enum PlayerType victim)
{
  if (!gameCanStealFromPlayer(map, victim))
  {
    return false;
  }

  int available[5];
  int availableCount = 0;
  for (int resource = 0; resource < 5; resource++)
  {
    if (map->players[victim].resources[resource] > 0)
    {
      available[availableCount++] = resource;
    }
  }

  if (availableCount <= 0)
  {
    return false;
  }

  const int stolenResource = available[rand() % availableCount];
  map->players[victim].resources[stolenResource]--;
  map->players[map->currentPlayer].resources[stolenResource]++;
  map->awaitingThiefVictimSelection = false;
  debugLog("GAME", "steal resource player=%d victim=%d resource=%d",
           map->currentPlayer,
           victim,
           stolenResource);
  return true;
}

void gameEndTurn(struct Map *map)
{
  if (!gameCanEndTurn(map))
  {
    return;
  }

  if (map->currentPlayer >= PLAYER_RED && map->currentPlayer <= PLAYER_BLACK)
  {
    for (int type = 0; type < DEVELOPMENT_CARD_COUNT; type++)
    {
      map->players[map->currentPlayer].newlyPurchasedDevelopmentCards[type] = 0;
    }
  }

  map->currentPlayer = (enum PlayerType)(((int)map->currentPlayer + 1) % MAX_PLAYERS);
  map->lastDiceRoll = 0;
  map->rolledThisTurn = false;
  map->awaitingThiefPlacement = false;
  map->awaitingThiefVictimSelection = false;
  map->playedDevelopmentCardThisTurn = false;
  map->freeRoadPlacementsRemaining = 0;
  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    map->discardRemaining[i] = 0;
  }
}

void gameHandlePlacedSettlement(struct Map *map, int tileId, int cornerIndex)
{
  if (!gameIsSetupSettlementTurn(map))
  {
    return;
  }

  if (map->setupStep >= MAX_PLAYERS)
  {
    grant_setup_settlement_resources(map, tileId, cornerIndex);
  }

  map->setupNeedsRoad = true;
  map->setupSettlementTileId = tileId;
  map->setupSettlementCornerIndex = cornerIndex;
}

void gameHandlePlacedRoad(struct Map *map)
{
  if (!gameIsSetupRoadTurn(map))
  {
    return;
  }

  map->setupNeedsRoad = false;
  map->setupSettlementTileId = -1;
  map->setupSettlementCornerIndex = -1;
  map->setupStep++;

  if (map->setupStep >= 8)
  {
    map->phase = GAME_PHASE_PLAY;
    map->currentPlayer = map->setupStartPlayer;
    return;
  }

  map->currentPlayer = (enum PlayerType)setup_player_for_step(map->setupStartPlayer, map->setupStep);
}

bool gameCanAffordRoad(const struct Map *map)
{
  return can_afford_cost(map, 1, 0, 1, 0, 0);
}

bool gameCanAffordSettlement(const struct Map *map)
{
  return can_afford_cost(map, 1, 1, 1, 1, 0);
}

bool gameCanAffordCity(const struct Map *map)
{
  return can_afford_cost(map, 0, 2, 0, 0, 3);
}

bool gameCanAffordDevelopment(const struct Map *map)
{
  return can_afford_cost(map, 0, 1, 0, 1, 1);
}

bool gameCanBuyDevelopment(const struct Map *map)
{
  return map != NULL &&
         map->phase == GAME_PHASE_PLAY &&
         !gameHasWinner(map) &&
         map->developmentDeckCount > 0 &&
         gameCanAffordDevelopment(map);
}

bool gameCanPlayDevelopmentCard(const struct Map *map, enum DevelopmentCardType type)
{
  return can_play_development_card_type(map, type);
}

bool gameTryBuyRoad(struct Map *map)
{
  return try_pay_cost(map, 1, 0, 1, 0, 0);
}

bool gameTryBuySettlement(struct Map *map)
{
  return try_pay_cost(map, 1, 1, 1, 1, 0);
}

bool gameTryBuyCity(struct Map *map)
{
  return try_pay_cost(map, 0, 2, 0, 0, 3);
}

bool gameTryBuyDevelopment(struct Map *map, enum DevelopmentCardType *drawnCard)
{
  if (!gameCanBuyDevelopment(map) || !try_pay_cost(map, 0, 1, 0, 1, 1))
  {
    return false;
  }

  const enum DevelopmentCardType card = map->developmentDeck[map->developmentDeckCount - 1];
  map->developmentDeckCount--;
  map->players[map->currentPlayer].developmentCards[card]++;
  map->players[map->currentPlayer].newlyPurchasedDevelopmentCards[card]++;

  if (drawnCard != NULL)
  {
    *drawnCard = card;
  }

  return true;
}

bool gameTryPlayKnight(struct Map *map)
{
  if (!can_play_development_card_type(map, DEVELOPMENT_CARD_KNIGHT))
  {
    return false;
  }

  consume_development_card(map, DEVELOPMENT_CARD_KNIGHT);
  map->players[map->currentPlayer].playedKnightCount++;
  update_largest_army_owner(map);
  map->awaitingThiefPlacement = true;
  map->awaitingThiefVictimSelection = false;
  return true;
}

bool gameTryPlayRoadBuilding(struct Map *map)
{
  if (!can_play_development_card_type(map, DEVELOPMENT_CARD_ROAD_BUILDING))
  {
    return false;
  }

  consume_development_card(map, DEVELOPMENT_CARD_ROAD_BUILDING);
  map->freeRoadPlacementsRemaining = 2;
  return true;
}

bool gameTryPlayYearOfPlenty(struct Map *map, enum ResourceType first, enum ResourceType second)
{
  if (!can_play_development_card_type(map, DEVELOPMENT_CARD_YEAR_OF_PLENTY) ||
      first < RESOURCE_WOOD || first > RESOURCE_STONE ||
      second < RESOURCE_WOOD || second > RESOURCE_STONE)
  {
    return false;
  }

  consume_development_card(map, DEVELOPMENT_CARD_YEAR_OF_PLENTY);
  map->players[map->currentPlayer].resources[first]++;
  map->players[map->currentPlayer].resources[second]++;
  return true;
}

bool gameTryPlayMonopoly(struct Map *map, enum ResourceType resource)
{
  if (!can_play_development_card_type(map, DEVELOPMENT_CARD_MONOPOLY) ||
      resource < RESOURCE_WOOD || resource > RESOURCE_STONE)
  {
    return false;
  }

  consume_development_card(map, DEVELOPMENT_CARD_MONOPOLY);
  for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
  {
    if (player == map->currentPlayer)
    {
      continue;
    }

    const int taken = map->players[player].resources[resource];
    map->players[player].resources[resource] -= taken;
    map->players[map->currentPlayer].resources[resource] += taken;
  }
  return true;
}

bool gameHasFreeRoadPlacements(const struct Map *map)
{
  return map != NULL &&
         map->phase == GAME_PHASE_PLAY &&
         map->freeRoadPlacementsRemaining > 0;
}

int gameGetFreeRoadPlacementsRemaining(const struct Map *map)
{
  return map != NULL ? map->freeRoadPlacementsRemaining : 0;
}

void gameConsumeFreeRoadPlacement(struct Map *map)
{
  if (map == NULL || map->freeRoadPlacementsRemaining <= 0)
  {
    return;
  }

  map->freeRoadPlacementsRemaining--;
}

void gameRefreshAwards(struct Map *map)
{
  update_longest_road_owner(map);
}

enum PlayerType gameGetLargestArmyOwner(const struct Map *map)
{
  return map != NULL ? map->largestArmyOwner : PLAYER_NONE;
}

enum PlayerType gameGetLongestRoadOwner(const struct Map *map)
{
  return map != NULL ? map->longestRoadOwner : PLAYER_NONE;
}

int gameGetLongestRoadLength(const struct Map *map)
{
  return map != NULL ? map->longestRoadLength : 0;
}

int gameComputeVisibleVictoryPoints(const struct Map *map, enum PlayerType player)
{
  if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
  {
    return 0;
  }

  int points = 0;
  for (int tileId = 0; tileId < MAX_TILES; tileId++)
  {
    for (int cornerIndex = 0; cornerIndex < 6; cornerIndex++)
    {
      const struct Corner *corner = &map->tiles[tileId].corners[cornerIndex];
      if (corner->owner != player || !is_canonical_shared_corner(tileId, cornerIndex))
      {
        continue;
      }

      if (corner->structure == STRUCTURE_TOWN)
      {
        points += 1;
      }
      else if (corner->structure == STRUCTURE_CITY)
      {
        points += 2;
      }
    }
  }

  if (map->largestArmyOwner == player)
  {
    points += 2;
  }

  if (map->longestRoadOwner == player)
  {
    points += 2;
  }

  return points;
}

int gameComputeVictoryPoints(const struct Map *map, enum PlayerType player)
{
  if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
  {
    return 0;
  }

  return gameComputeVisibleVictoryPoints(map, player) +
         map->players[player].developmentCards[DEVELOPMENT_CARD_VICTORY_POINT];
}

int gameGetDevelopmentDeckCount(const struct Map *map)
{
  return map != NULL ? map->developmentDeckCount : 0;
}

int gameGetDevelopmentCardCount(const struct Map *map, enum PlayerType player, enum DevelopmentCardType type)
{
  if (map == NULL ||
      player < PLAYER_RED || player > PLAYER_BLACK ||
      type < 0 || type >= DEVELOPMENT_CARD_COUNT)
  {
    return 0;
  }

  return map->players[player].developmentCards[type];
}

int gameGetMaritimeTradeRate(const struct Map *map, enum ResourceType give)
{
  if (map == NULL || map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
  {
    return 4;
  }

  bool hasGeneric = false;
  if (player_has_port(map, map->currentPlayer, give, &hasGeneric))
  {
    return 2;
  }

  if (hasGeneric)
  {
    return 3;
  }

  return 4;
}

bool gameCanTradeMaritime(const struct Map *map, enum ResourceType give, int tradeCount, enum ResourceType receive)
{
  if (map == NULL || give == receive || tradeCount <= 0 ||
      map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
  {
    return false;
  }

  const int rate = gameGetMaritimeTradeRate(map, give);
  return map->players[map->currentPlayer].resources[give] >= rate * tradeCount;
}

bool gameTryTradeMaritime(struct Map *map, enum ResourceType give, int tradeCount, enum ResourceType receive)
{
  if (!gameCanTradeMaritime(map, give, tradeCount, receive))
  {
    return false;
  }

  const int rate = gameGetMaritimeTradeRate(map, give);
  struct PlayerState *player = &map->players[map->currentPlayer];
  player->resources[give] -= rate * tradeCount;
  player->resources[receive] += tradeCount;
  return true;
}

bool gameCanTradeWithPlayer(const struct Map *map, enum PlayerType otherPlayer, enum ResourceType give, int giveAmount, enum ResourceType receive, int receiveAmount)
{
  if (map == NULL || give == receive || giveAmount <= 0 || receiveAmount <= 0 ||
      map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK ||
      otherPlayer < PLAYER_RED || otherPlayer > PLAYER_BLACK ||
      otherPlayer == map->currentPlayer)
  {
    return false;
  }

  return map->players[map->currentPlayer].resources[give] >= giveAmount &&
         map->players[otherPlayer].resources[receive] >= receiveAmount;
}

bool gameTryTradeWithPlayer(struct Map *map, enum PlayerType otherPlayer, enum ResourceType give, int giveAmount, enum ResourceType receive, int receiveAmount)
{
  if (!gameCanTradeWithPlayer(map, otherPlayer, give, giveAmount, receive, receiveAmount))
  {
    return false;
  }

  struct PlayerState *current = &map->players[map->currentPlayer];
  struct PlayerState *other = &map->players[otherPlayer];
  current->resources[give] -= giveAmount;
  current->resources[receive] += receiveAmount;
  other->resources[receive] -= receiveAmount;
  other->resources[give] += giveAmount;
  return true;
}

static int playable_development_card_count(const struct Map *map, enum PlayerType player, enum DevelopmentCardType type)
{
  if (map == NULL ||
      player < PLAYER_RED || player > PLAYER_BLACK ||
      type < 0 || type >= DEVELOPMENT_CARD_COUNT)
  {
    return 0;
  }

  const int total = map->players[player].developmentCards[type];
  const int locked = map->players[player].newlyPurchasedDevelopmentCards[type];
  return total > locked ? total - locked : 0;
}

static bool can_play_development_card_type(const struct Map *map, enum DevelopmentCardType type)
{
  if (map == NULL ||
      map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK ||
      type < 0 || type >= DEVELOPMENT_CARD_COUNT ||
      type == DEVELOPMENT_CARD_VICTORY_POINT)
  {
    return false;
  }

  return map->phase == GAME_PHASE_PLAY &&
         !gameHasWinner(map) &&
         !map->playedDevelopmentCardThisTurn &&
         !any_pending_discards(map) &&
         !map->awaitingThiefPlacement &&
         !map->awaitingThiefVictimSelection &&
         playable_development_card_count(map, map->currentPlayer, type) > 0;
}

static void consume_development_card(struct Map *map, enum DevelopmentCardType type)
{
  if (map == NULL ||
      map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK ||
      type < 0 || type >= DEVELOPMENT_CARD_COUNT)
  {
    return;
  }

  map->players[map->currentPlayer].developmentCards[type]--;
  map->playedDevelopmentCardThisTurn = true;
}

static void update_largest_army_owner(struct Map *map)
{
  if (map == NULL || map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
  {
    return;
  }

  const int currentKnightCount = map->players[map->currentPlayer].playedKnightCount;
  if (currentKnightCount < 3)
  {
    return;
  }

  if (map->largestArmyOwner == map->currentPlayer)
  {
    return;
  }

  if (map->largestArmyOwner < PLAYER_RED || map->largestArmyOwner > PLAYER_BLACK)
  {
    int bestOther = 0;
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
      if (player != map->currentPlayer &&
          map->players[player].playedKnightCount > bestOther)
      {
        bestOther = map->players[player].playedKnightCount;
      }
    }

    if (currentKnightCount > bestOther)
    {
      map->largestArmyOwner = map->currentPlayer;
    }
    return;
  }

  if (currentKnightCount > map->players[map->largestArmyOwner].playedKnightCount)
  {
    map->largestArmyOwner = map->currentPlayer;
  }
}

static void get_road_edge_key(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by)
{
  int cornerA = 0;
  int cornerB = 1;
  get_side_corner_indices(sideIndex, &cornerA, &cornerB);
  get_corner_key(center, radius, cornerA, ax, ay);
  get_corner_key(center, radius, cornerB, bx, by);

  if (*ax > *bx || (*ax == *bx && *ay > *by))
  {
    const int tx = *ax;
    const int ty = *ay;
    *ax = *bx;
    *ay = *by;
    *bx = tx;
    *by = ty;
  }
}

static bool road_edge_keys_match(int ax1, int ay1, int bx1, int by1, int ax2, int ay2, int bx2, int by2)
{
  return ax1 == ax2 && ay1 == ay2 && bx1 == bx2 && by1 == by2;
}

static enum PlayerType get_shared_corner_owner_by_key(const struct Map *map, int x, int y)
{
  if (map == NULL)
  {
    return PLAYER_NONE;
  }

  const float radius = 68.0f;
  const Vector2 origin = {1600.0f * 0.42f, 900.0f * 0.46f};
  for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
  {
    const Vector2 center = axial_to_world(kLandCoords[tileId], origin, radius);
    for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
    {
      int cornerX = 0;
      int cornerY = 0;
      const struct Corner *corner = &map->tiles[tileId].corners[cornerIndex];
      if (corner->structure == STRUCTURE_NONE)
      {
        continue;
      }

      get_corner_key(center, radius, cornerIndex, &cornerX, &cornerY);
      if (corner_keys_match(x, y, cornerX, cornerY))
      {
        return corner->owner;
      }
    }
  }

  return PLAYER_NONE;
}

static int dfs_longest_road(const int nodeX[], const int nodeY[], const bool nodeBlocked[], int nodeCount,
                            const int edgeA[], const int edgeB[], int edgeCount, int currentNode,
                            bool startingNode, bool usedEdges[MAX_SIDES])
{
  int best = 0;

  (void)nodeX;
  (void)nodeY;
  (void)nodeCount;

  if (!startingNode && nodeBlocked[currentNode])
  {
    return 0;
  }

  for (int edgeIndex = 0; edgeIndex < edgeCount; edgeIndex++)
  {
    int nextNode = -1;
    if (usedEdges[edgeIndex])
    {
      continue;
    }
    if (edgeA[edgeIndex] == currentNode)
    {
      nextNode = edgeB[edgeIndex];
    }
    else if (edgeB[edgeIndex] == currentNode)
    {
      nextNode = edgeA[edgeIndex];
    }
    else
    {
      continue;
    }

    usedEdges[edgeIndex] = true;
    {
      const int candidate = 1 + dfs_longest_road(nodeX, nodeY, nodeBlocked, nodeCount,
                                                 edgeA, edgeB, edgeCount, nextNode, false, usedEdges);
      if (candidate > best)
      {
        best = candidate;
      }
    }
    usedEdges[edgeIndex] = false;
  }

  return best;
}

static int player_longest_road_length(const struct Map *map, enum PlayerType player)
{
  int nodeX[MAX_CORNERS];
  int nodeY[MAX_CORNERS];
  bool nodeBlocked[MAX_CORNERS] = {0};
  int edgeA[MAX_SIDES];
  int edgeB[MAX_SIDES];
  int nodeCount = 0;
  int edgeCount = 0;
  const float radius = 68.0f;
  const Vector2 origin = {1600.0f * 0.42f, 900.0f * 0.46f};

  if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
  {
    return 0;
  }

  for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
  {
    const Vector2 center = axial_to_world(kLandCoords[tileId], origin, radius);
    for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
    {
      int ax = 0;
      int ay = 0;
      int bx = 0;
      int by = 0;
      int aNode = -1;
      int bNode = -1;
      bool duplicate = false;

      if (!map->tiles[tileId].sides[sideIndex].isset ||
          map->tiles[tileId].sides[sideIndex].player != player)
      {
        continue;
      }

      get_road_edge_key(center, radius, sideIndex, &ax, &ay, &bx, &by);
      for (int edgeIndex = 0; edgeIndex < edgeCount; edgeIndex++)
      {
        if (road_edge_keys_match(ax, ay, bx, by,
                                 nodeX[edgeA[edgeIndex]], nodeY[edgeA[edgeIndex]],
                                 nodeX[edgeB[edgeIndex]], nodeY[edgeB[edgeIndex]]))
        {
          duplicate = true;
          break;
        }
      }
      if (duplicate)
      {
        continue;
      }

      for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
      {
        if (nodeX[nodeIndex] == ax && nodeY[nodeIndex] == ay)
        {
          aNode = nodeIndex;
        }
        if (nodeX[nodeIndex] == bx && nodeY[nodeIndex] == by)
        {
          bNode = nodeIndex;
        }
      }

      if (aNode < 0 && nodeCount < MAX_CORNERS)
      {
        aNode = nodeCount;
        nodeX[nodeCount] = ax;
        nodeY[nodeCount] = ay;
        nodeBlocked[nodeCount] = false;
        nodeCount++;
      }
      if (bNode < 0 && nodeCount < MAX_CORNERS)
      {
        bNode = nodeCount;
        nodeX[nodeCount] = bx;
        nodeY[nodeCount] = by;
        nodeBlocked[nodeCount] = false;
        nodeCount++;
      }
      if (aNode < 0 || bNode < 0 || edgeCount >= MAX_SIDES)
      {
        continue;
      }

      edgeA[edgeCount] = aNode;
      edgeB[edgeCount] = bNode;
      edgeCount++;
    }
  }

  if (edgeCount <= 0)
  {
    return 0;
  }

  for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
  {
    const enum PlayerType owner = get_shared_corner_owner_by_key(map, nodeX[nodeIndex], nodeY[nodeIndex]);
    nodeBlocked[nodeIndex] = owner != PLAYER_NONE && owner != player;
  }

  {
    int best = 0;
    bool usedEdges[MAX_SIDES] = {0};
    for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
    {
      for (int edgeIndex = 0; edgeIndex < edgeCount; edgeIndex++)
      {
        usedEdges[edgeIndex] = false;
      }
      {
        const int candidate = dfs_longest_road(nodeX, nodeY, nodeBlocked, nodeCount,
                                               edgeA, edgeB, edgeCount, nodeIndex, true, usedEdges);
        if (candidate > best)
        {
          best = candidate;
        }
      }
    }
    return best;
  }
}

static void update_longest_road_owner(struct Map *map)
{
  int lengths[MAX_PLAYERS] = {0};
  int bestLength = 0;
  int bestCount = 0;
  enum PlayerType uniqueBest = PLAYER_NONE;
  enum PlayerType previousOwner = PLAYER_NONE;

  if (map == NULL)
  {
    return;
  }

  previousOwner = map->longestRoadOwner;
  for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
  {
    lengths[player] = player_longest_road_length(map, (enum PlayerType)player);
    if (lengths[player] > bestLength)
    {
      bestLength = lengths[player];
      bestCount = 1;
      uniqueBest = (enum PlayerType)player;
    }
    else if (lengths[player] == bestLength && bestLength > 0)
    {
      bestCount++;
    }
  }

  map->longestRoadLength = bestLength;
  if (bestLength < 5)
  {
    map->longestRoadOwner = PLAYER_NONE;
    return;
  }

  if (previousOwner >= PLAYER_RED &&
      previousOwner <= PLAYER_BLACK &&
      lengths[previousOwner] == bestLength)
  {
    map->longestRoadOwner = previousOwner;
    return;
  }

  if (bestCount == 1)
  {
    map->longestRoadOwner = uniqueBest;
    return;
  }

  map->longestRoadOwner = PLAYER_NONE;
}

static int setup_player_for_step(enum PlayerType startPlayer, int step)
{
  const int start = startPlayer >= PLAYER_RED && startPlayer <= PLAYER_BLACK ? (int)startPlayer : PLAYER_RED;

  if (step < 0 || step >= MAX_PLAYERS * 2)
  {
    return PLAYER_RED;
  }

  if (step < MAX_PLAYERS)
  {
    return (start + step) % MAX_PLAYERS;
  }

  return (start + (MAX_PLAYERS * 2 - 1 - step)) % MAX_PLAYERS;
}

static Vector2 axial_to_world(struct AxialCoord coord, Vector2 origin, float radius)
{
  const float root3 = 1.7320508f;
  return (Vector2){
      origin.x + radius * root3 * ((float)coord.q + (float)coord.r * 0.5f),
      origin.y + radius * 1.5f * (float)coord.r};
}

static Vector2 point_on_hex(Vector2 center, float radius, int cornerIndex)
{
  const float angle = DEG2RAD * (60.0f * (float)cornerIndex - 30.0f);
  return (Vector2){
      center.x + cosf(angle) * radius,
      center.y + sinf(angle) * radius};
}

static void get_corner_key(Vector2 center, float radius, int cornerIndex, int *x, int *y)
{
  const Vector2 point = point_on_hex(center, radius, cornerIndex);
  *x = (int)roundf(point.x * 10.0f);
  *y = (int)roundf(point.y * 10.0f);
}

static bool corner_keys_match(int x1, int y1, int x2, int y2)
{
  return x1 == x2 && y1 == y2;
}

static bool is_canonical_shared_corner(int tileId, int cornerIndex)
{
  const float radius = 68.0f;
  const Vector2 origin = {1600.0f * 0.42f, 900.0f * 0.46f};
  const Vector2 center = axial_to_world(kLandCoords[tileId], origin, radius);
  int tx = 0;
  int ty = 0;
  get_corner_key(center, radius, cornerIndex, &tx, &ty);

  for (int otherTile = 0; otherTile < tileId; otherTile++)
  {
    const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
    for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
    {
      int ox = 0;
      int oy = 0;
      get_corner_key(otherCenter, radius, otherCorner, &ox, &oy);
      if (corner_keys_match(tx, ty, ox, oy))
      {
        return false;
      }
    }
  }

  return true;
}

static int total_resources_for_player(const struct Map *map, enum PlayerType player)
{
  if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
  {
    return 0;
  }

  int total = 0;
  for (int resource = 0; resource < 5; resource++)
  {
    total += map->players[player].resources[resource];
  }
  return total;
}

static bool any_pending_discards(const struct Map *map)
{
  if (map == NULL)
  {
    return false;
  }

  for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
  {
    if (map->discardRemaining[player] > 0)
    {
      return true;
    }
  }

  return false;
}

static void begin_discard_phase(struct Map *map)
{
  if (map == NULL)
  {
    return;
  }

  map->awaitingThiefPlacement = false;
  map->awaitingThiefVictimSelection = false;
  debugLog("GAME", "begin discard phase currentPlayer=%d", map->currentPlayer);
  for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
  {
    const int total = total_resources_for_player(map, (enum PlayerType)player);
    map->discardRemaining[player] = total > 7 ? total / 2 : 0;
    debugLog("GAME", "discard requirement player=%d total=%d discard=%d",
             player,
             total,
             map->discardRemaining[player]);
  }

  if (!any_pending_discards(map))
  {
    map->awaitingThiefPlacement = true;
    debugLog("GAME", "no discards needed, thief placement starts immediately");
  }
}

static bool player_touches_tile(const struct Map *map, enum PlayerType player, int tileId)
{
  if (map == NULL || player < PLAYER_RED || player > PLAYER_BLACK ||
      tileId < 0 || tileId >= LAND_TILE_COUNT)
  {
    return false;
  }

  for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
  {
    if (map->tiles[tileId].corners[cornerIndex].owner == player)
    {
      return true;
    }
  }

  return false;
}

static bool player_has_any_resources(const struct Map *map, enum PlayerType player)
{
  return total_resources_for_player(map, player) > 0;
}

static void grant_setup_settlement_resources(struct Map *map, int tileId, int cornerIndex)
{
  if (map == NULL ||
      map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK ||
      tileId < 0 || tileId >= LAND_TILE_COUNT ||
      cornerIndex < 0 || cornerIndex >= HEX_CORNERS)
  {
    return;
  }

  const float radius = 68.0f;
  const Vector2 origin = {1600.0f * 0.42f, 900.0f * 0.46f};
  const Vector2 center = axial_to_world(kLandCoords[tileId], origin, radius);
  int targetX = 0;
  int targetY = 0;
  get_corner_key(center, radius, cornerIndex, &targetX, &targetY);

  for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
  {
    if (map->tiles[otherTile].type == TILE_DESERT)
    {
      continue;
    }

    const Vector2 otherCenter = axial_to_world(kLandCoords[otherTile], origin, radius);
    for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
    {
      int otherX = 0;
      int otherY = 0;
      get_corner_key(otherCenter, radius, otherCorner, &otherX, &otherY);
      if (!corner_keys_match(targetX, targetY, otherX, otherY))
      {
        continue;
      }

      const enum ResourceType resource = resource_for_tile_type(map->tiles[otherTile].type);
      map->players[map->currentPlayer].resources[resource]++;
      break;
    }
  }
}

static enum ResourceType resource_for_tile_type(enum TileType type)
{
  switch (type)
  {
  case TILE_FOREST:
    return RESOURCE_WOOD;
  case TILE_FARMLAND:
    return RESOURCE_WHEAT;
  case TILE_MINE:
    return RESOURCE_CLAY;
  case TILE_SHEEPMEADOW:
    return RESOURCE_SHEEP;
  case TILE_MOUNTAIN:
    return RESOURCE_STONE;
  case TILE_DESERT:
  default:
    return RESOURCE_WOOD;
  }
}

static int resource_yield_for_structure(enum StructureType structure)
{
  if (structure == STRUCTURE_CITY)
  {
    return 2;
  }

  if (structure == STRUCTURE_TOWN)
  {
    return 1;
  }

  return 0;
}

static void distribute_resources_for_roll(struct Map *map, int roll)
{
  if (map == NULL || roll <= 0)
  {
    return;
  }

  for (int tileId = 0; tileId < MAX_TILES; tileId++)
  {
    const struct Tile *tile = &map->tiles[tileId];
    if (tile->diceNumber != roll || tile->type == TILE_DESERT ||
        map->thiefTileId == tile->id)
    {
      continue;
    }

    const enum ResourceType resource = resource_for_tile_type(tile->type);
    for (int cornerIndex = 0; cornerIndex < 6; cornerIndex++)
    {
      const struct Corner *corner = &tile->corners[cornerIndex];
      if (corner->owner < PLAYER_RED || corner->owner > PLAYER_BLACK)
      {
        continue;
      }

      const int yield = resource_yield_for_structure(corner->structure);
      if (yield > 0)
      {
        map->players[corner->owner].resources[resource] += yield;
      }
    }
  }
}

static bool can_afford_cost(const struct Map *map, int wood, int wheat, int clay, int sheep, int stone)
{
  if (map == NULL || map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
  {
    return false;
  }

  const struct PlayerState *player = &map->players[map->currentPlayer];
  return player->resources[RESOURCE_WOOD] >= wood &&
         player->resources[RESOURCE_WHEAT] >= wheat &&
         player->resources[RESOURCE_CLAY] >= clay &&
         player->resources[RESOURCE_SHEEP] >= sheep &&
         player->resources[RESOURCE_STONE] >= stone;
}

static bool try_pay_cost(struct Map *map, int wood, int wheat, int clay, int sheep, int stone)
{
  if (!can_afford_cost(map, wood, wheat, clay, sheep, stone))
  {
    return false;
  }

  struct PlayerState *player = &map->players[map->currentPlayer];
  player->resources[RESOURCE_WOOD] -= wood;
  player->resources[RESOURCE_WHEAT] -= wheat;
  player->resources[RESOURCE_CLAY] -= clay;
  player->resources[RESOURCE_SHEEP] -= sheep;
  player->resources[RESOURCE_STONE] -= stone;
  return true;
}

static int find_land_tile_id(struct AxialCoord coord)
{
  for (int i = 0; i < LAND_TILE_COUNT; i++)
  {
    if (kLandCoords[i].q == coord.q && kLandCoords[i].r == coord.r)
    {
      return i;
    }
  }

  return -1;
}

static int find_port_side_index(const struct PortRule *port)
{
  static const struct AxialCoord directions[HEX_CORNERS] = {
      {1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};

  const int dq = port->landCoord.q - port->oceanCoord.q;
  const int dr = port->landCoord.r - port->oceanCoord.r;
  for (int i = 0; i < HEX_CORNERS; i++)
  {
    if (directions[i].q == dq && directions[i].r == dr)
    {
      return (i + 3) % HEX_CORNERS;
    }
  }

  return -1;
}

static void get_side_corner_indices(int sideIndex, int *cornerA, int *cornerB)
{
  static const int sideCorners[HEX_CORNERS][2] = {
      {0, 1}, {5, 0}, {4, 5}, {3, 4}, {2, 3}, {1, 2}};
  const int index = ((sideIndex % HEX_CORNERS) + HEX_CORNERS) % HEX_CORNERS;
  *cornerA = sideCorners[index][0];
  *cornerB = sideCorners[index][1];
}

static bool player_has_port(const struct Map *map, enum PlayerType player, enum ResourceType resource, bool *hasGeneric)
{
  if (hasGeneric != NULL)
  {
    *hasGeneric = false;
  }

  for (int i = 0; i < (int)(sizeof(kPorts) / sizeof(kPorts[0])); i++)
  {
    const int tileId = find_land_tile_id(kPorts[i].landCoord);
    const int sideIndex = find_port_side_index(&kPorts[i]);
    if (tileId < 0 || sideIndex < 0)
    {
      continue;
    }

    int cornerA = 0;
    int cornerB = 1;
    get_side_corner_indices(sideIndex, &cornerA, &cornerB);
    const struct Corner *a = &map->tiles[tileId].corners[cornerA];
    const struct Corner *b = &map->tiles[tileId].corners[cornerB];
    const bool ownsPort = (a->owner == player && a->structure != STRUCTURE_NONE) ||
                          (b->owner == player && b->structure != STRUCTURE_NONE);
    if (!ownsPort)
    {
      continue;
    }

    if (kPorts[i].generic)
    {
      if (hasGeneric != NULL)
      {
        *hasGeneric = true;
      }
      continue;
    }

    if (kPorts[i].resource == resource)
    {
      return true;
    }
  }

  return false;
}
