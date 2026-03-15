#ifndef TILE_H
#define TILE_H
#include "resource.h"
#include "player.h"
#include "structure.h"
#include <stdbool.h>
enum TileType{
    TILE_FARMLAND,
    TILE_SHEEPMEADOW,
    TILE_MINE,
    TILE_FOREST,
    TILE_MOUNTAIN,
    TILE_DESERT
};

struct Corner {
    int id;
    enum StructureType structure;
    enum PlayerType owner;
};

struct Side {
    int id;
    bool isset;
    enum PlayerType player;
};


struct Tile {
    int id;
    int diceNumber;
    enum TileType type;    
    struct Side sides[6];
    struct Corner corners[6];
    //bool connectToWater;
    //bool hasWaterTrade;
    //enum ResourceType tradeResource;
};

#endif
