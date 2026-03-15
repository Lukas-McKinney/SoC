#ifndef MAP_H
#define MAP_H
#include "tile.h"

#define MAX_TILES 19
#define MAX_SIDES 54
#define MAX_CORNERS 72

enum GamePhase {
    GAME_PHASE_SETUP,
    GAME_PHASE_PLAY,
    GAME_PHASE_GAME_OVER
};

struct Map {
    struct Tile tiles[MAX_TILES];
    struct Side sides[MAX_SIDES];
    struct Corner corners[MAX_CORNERS];
    struct PlayerState players[MAX_PLAYERS];
    enum PlayerType currentPlayer;
    enum PlayerType setupStartPlayer;
    enum PlayerType winner;
    enum GamePhase phase;
    int setupStep;
    bool setupNeedsRoad;
    int setupSettlementTileId;
    int setupSettlementCornerIndex;
    int lastDiceRoll;
    bool rolledThisTurn;
    int thiefTileId;
    bool awaitingThiefPlacement;
    bool awaitingThiefVictimSelection;
    int discardRemaining[MAX_PLAYERS];
    enum DevelopmentCardType developmentDeck[DEVELOPMENT_DECK_SIZE];
    int developmentDeckCount;
    bool playedDevelopmentCardThisTurn;
    int freeRoadPlacementsRemaining;
    enum PlayerType largestArmyOwner;
    enum PlayerType longestRoadOwner;
    int longestRoadLength;
};

bool setupMap(struct Map *map);
void createRandomizedTileTypes(enum TileType types[MAX_TILES]);
void createRandomizedDiceNumbers(int diceNumbers[MAX_TILES]);

#endif
