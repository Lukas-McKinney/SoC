#ifndef PLAYER_H
#define PLAYER_H
#include "development_card.h"
#include "resource.h"

enum PlayerType{
  PLAYER_NONE = -1,
  PLAYER_RED,
  PLAYER_BLUE,
  PLAYER_GREEN,
  PLAYER_BLACK
};

enum PlayerControlMode {
  PLAYER_CONTROL_HUMAN,
  PLAYER_CONTROL_AI
};

enum AiDifficulty {
  AI_DIFFICULTY_EASY,
  AI_DIFFICULTY_MEDIUM,
  AI_DIFFICULTY_HARD
};

#define MAX_PLAYERS 4

struct PlayerState {
  enum PlayerType type;
  enum PlayerControlMode controlMode;
  enum AiDifficulty aiDifficulty;
  int resources[5];
  int developmentCards[DEVELOPMENT_CARD_COUNT];
  int newlyPurchasedDevelopmentCards[DEVELOPMENT_CARD_COUNT];
  int playedKnightCount;
};

#endif
