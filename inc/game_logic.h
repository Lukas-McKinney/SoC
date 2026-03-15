#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include "map.h"

bool gameIsSetupSettlementTurn(const struct Map *map);
bool gameIsSetupRoadTurn(const struct Map *map);
bool gameCanRollDice(const struct Map *map);
bool gameCanEndTurn(const struct Map *map);
bool gameHasWinner(const struct Map *map);
enum PlayerType gameGetWinner(const struct Map *map);
bool gameCheckVictory(struct Map *map, enum PlayerType player);
bool gameHasPendingDiscards(const struct Map *map);
enum PlayerType gameGetCurrentDiscardPlayer(const struct Map *map);
int gameGetDiscardAmountForPlayer(const struct Map *map, enum PlayerType player);
bool gameTrySubmitDiscard(struct Map *map, enum PlayerType player, const int resources[5]);
bool gameNeedsThiefPlacement(const struct Map *map);
bool gameCanMoveThiefToTile(const struct Map *map, int tileId);
bool gameNeedsThiefVictimSelection(const struct Map *map);
bool gameCanStealFromPlayer(const struct Map *map, enum PlayerType victim);
void gameRollDice(struct Map *map, int diceRoll);
void gameMoveThief(struct Map *map, int tileId);
bool gameStealRandomResource(struct Map *map, enum PlayerType victim);
void gameEndTurn(struct Map *map);
void gameHandlePlacedSettlement(struct Map *map, int tileId, int cornerIndex);
void gameHandlePlacedRoad(struct Map *map);
bool gameCanAffordRoad(const struct Map *map);
bool gameCanAffordSettlement(const struct Map *map);
bool gameCanAffordCity(const struct Map *map);
bool gameCanAffordDevelopment(const struct Map *map);
bool gameCanBuyDevelopment(const struct Map *map);
bool gameCanPlayDevelopmentCard(const struct Map *map, enum DevelopmentCardType type);
bool gameTryBuyRoad(struct Map *map);
bool gameTryBuySettlement(struct Map *map);
bool gameTryBuyCity(struct Map *map);
bool gameTryBuyDevelopment(struct Map *map, enum DevelopmentCardType *drawnCard);
bool gameTryPlayKnight(struct Map *map);
bool gameTryPlayRoadBuilding(struct Map *map);
bool gameTryPlayYearOfPlenty(struct Map *map, enum ResourceType first, enum ResourceType second);
bool gameTryPlayMonopoly(struct Map *map, enum ResourceType resource);
bool gameHasFreeRoadPlacements(const struct Map *map);
int gameGetFreeRoadPlacementsRemaining(const struct Map *map);
void gameConsumeFreeRoadPlacement(struct Map *map);
void gameRefreshAwards(struct Map *map);
enum PlayerType gameGetLargestArmyOwner(const struct Map *map);
enum PlayerType gameGetLongestRoadOwner(const struct Map *map);
int gameGetLongestRoadLength(const struct Map *map);
int gameComputeVictoryPoints(const struct Map *map, enum PlayerType player);
int gameComputeVisibleVictoryPoints(const struct Map *map, enum PlayerType player);
int gameGetDevelopmentDeckCount(const struct Map *map);
int gameGetDevelopmentCardCount(const struct Map *map, enum PlayerType player, enum DevelopmentCardType type);
int gameGetMaritimeTradeRate(const struct Map *map, enum ResourceType give);
bool gameCanTradeMaritime(const struct Map *map, enum ResourceType give, int tradeCount, enum ResourceType receive);
bool gameTryTradeMaritime(struct Map *map, enum ResourceType give, int tradeCount, enum ResourceType receive);
bool gameCanTradeWithPlayer(const struct Map *map, enum PlayerType otherPlayer, enum ResourceType give, int giveAmount, enum ResourceType receive, int receiveAmount);
bool gameTryTradeWithPlayer(struct Map *map, enum PlayerType otherPlayer, enum ResourceType give, int giveAmount, enum ResourceType receive, int receiveAmount);

#endif
