#include "map.h"
#include <stdlib.h>

static int random_int(int min, int max);
static void createRandomizedDevelopmentDeck(enum DevelopmentCardType deck[DEVELOPMENT_DECK_SIZE]);
const int centerIndex = 9;

void createRandomizedTileTypes(enum TileType types[MAX_TILES])
{
  enum TileType temp[MAX_TILES - 1] = {
      TILE_FOREST, TILE_FOREST, TILE_FOREST, TILE_FOREST,
      TILE_SHEEPMEADOW, TILE_SHEEPMEADOW, TILE_SHEEPMEADOW, TILE_SHEEPMEADOW,
      TILE_FARMLAND, TILE_FARMLAND, TILE_FARMLAND, TILE_FARMLAND,
      TILE_MOUNTAIN, TILE_MOUNTAIN, TILE_MOUNTAIN, TILE_MINE,
      TILE_MINE, TILE_MINE};

  for (int i = MAX_TILES - 2; i > 0; i--)
  {
    int swap_index = random_int(0, i + 1);

    enum TileType tmp = temp[i];
    temp[i] = temp[swap_index];
    temp[swap_index] = tmp;
  }

  int j = 0;
  for (int i = 0; i < MAX_TILES; i++)
  {
    if (i == centerIndex)
    {
      types[i] = TILE_DESERT;
    }
    else
    {
      types[i] = temp[j];
      j++;
    }
  }
}

void createRandomizedDiceNumbers(int diceNumbers[MAX_TILES])
{
  int temp[MAX_TILES - 1] = {
      2,
      3, 3,
      4, 4,
      5, 5,
      6, 6,
      8, 8,
      9, 9,
      10, 10,
      11, 11,
      12};

  for (int i = MAX_TILES - 2; i > 0; i--)
  {
    int swap_index = random_int(0, i + 1);

    int tmp = temp[i];
    temp[i] = temp[swap_index];
    temp[swap_index] = tmp;
  }

  int j = 0;
  for (int i = 0; i < MAX_TILES; i++)
  {
    if (i == centerIndex)
    {
      diceNumbers[i] = 0;
    }
    else
    {
      diceNumbers[i] = temp[j];
      j++;
    }
  }
}

bool setupMap(struct Map *map)
{
  // Create Array to be filled
  enum TileType shuffledTypes[MAX_TILES];
  // Shuffle Array
  createRandomizedTileTypes(shuffledTypes);

  for (int i = 0; i < MAX_SIDES; i++)
  {
    map->sides[i].id = i;
    map->sides[i].isset = false;
    map->sides[i].player = PLAYER_NONE;
  }

  for (int i = 0; i < MAX_CORNERS; i++)
  {
    map->corners[i].id = i;
    map->corners[i].structure = STRUCTURE_NONE;
    map->corners[i].owner = PLAYER_NONE;
  }

  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    map->players[i].type = (enum PlayerType)i;
    map->players[i].controlMode = PLAYER_CONTROL_HUMAN;
    map->players[i].aiDifficulty = AI_DIFFICULTY_EASY;
    for (int resource = 0; resource < 5; resource++)
    {
      map->players[i].resources[resource] = 10;
    }
    for (int card = 0; card < DEVELOPMENT_CARD_COUNT; card++)
    {
      map->players[i].developmentCards[card] = 0;
      map->players[i].newlyPurchasedDevelopmentCards[card] = 0;
    }
    map->players[i].playedKnightCount = 0;
  }

  map->setupStartPlayer = (enum PlayerType)random_int(0, MAX_PLAYERS);
  map->currentPlayer = map->setupStartPlayer;
  map->winner = PLAYER_NONE;
  map->phase = GAME_PHASE_SETUP;
  map->setupStep = 0;
  map->setupNeedsRoad = false;
  map->setupSettlementTileId = -1;
  map->setupSettlementCornerIndex = -1;
  map->lastDiceRoll = 0;
  map->rolledThisTurn = false;
  map->awaitingThiefPlacement = false;
  map->awaitingThiefVictimSelection = false;
  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    map->discardRemaining[i] = 0;
  }
  createRandomizedDevelopmentDeck(map->developmentDeck);
  map->developmentDeckCount = DEVELOPMENT_DECK_SIZE;
  map->playedDevelopmentCardThisTurn = false;
  map->freeRoadPlacementsRemaining = 0;
  map->largestArmyOwner = PLAYER_NONE;
  map->longestRoadOwner = PLAYER_NONE;
  map->longestRoadLength = 0;

  int diceNumbers[MAX_TILES];
  createRandomizedDiceNumbers(diceNumbers);
  // Create Tiles with the shuhffeled Tile types
  for (int i = 0; i < MAX_TILES; i++)
  {
    map->tiles[i].id = i;
    map->tiles[i].diceNumber = diceNumbers[i];
    map->tiles[i].type = shuffledTypes[i];

    for (int j = 0; j < 6; j++)
    {
      map->tiles[i].sides[j].id = j;
      map->tiles[i].sides[j].isset = false;
      map->tiles[i].sides[j].player = PLAYER_NONE;

      map->tiles[i].corners[j].id = j;
      map->tiles[i].corners[j].structure = STRUCTURE_NONE;
      map->tiles[i].corners[j].owner = PLAYER_NONE;
    }
  }

  map->thiefTileId = centerIndex;

  return true;
}

static int random_int(int min, int max) { return min + rand() % (max - min); }

static void createRandomizedDevelopmentDeck(enum DevelopmentCardType deck[DEVELOPMENT_DECK_SIZE])
{
  enum DevelopmentCardType ordered[DEVELOPMENT_DECK_SIZE] = {
      DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT,
      DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT,
      DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT,
      DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT,
      DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_KNIGHT, DEVELOPMENT_CARD_VICTORY_POINT,
      DEVELOPMENT_CARD_VICTORY_POINT, DEVELOPMENT_CARD_VICTORY_POINT,
      DEVELOPMENT_CARD_VICTORY_POINT, DEVELOPMENT_CARD_VICTORY_POINT,
      DEVELOPMENT_CARD_ROAD_BUILDING, DEVELOPMENT_CARD_ROAD_BUILDING,
      DEVELOPMENT_CARD_YEAR_OF_PLENTY, DEVELOPMENT_CARD_YEAR_OF_PLENTY,
      DEVELOPMENT_CARD_MONOPOLY, DEVELOPMENT_CARD_MONOPOLY};

  for (int i = DEVELOPMENT_DECK_SIZE - 1; i > 0; i--)
  {
    const int swapIndex = random_int(0, i + 1);
    const enum DevelopmentCardType card = ordered[i];
    ordered[i] = ordered[swapIndex];
    ordered[swapIndex] = card;
  }

  for (int i = 0; i < DEVELOPMENT_DECK_SIZE; i++)
  {
    deck[i] = ordered[i];
  }
}
