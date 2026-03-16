#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include "player.h"
#include <raylib.h>

enum MainMenuAction
{
    MAIN_MENU_ACTION_NONE,
    MAIN_MENU_ACTION_START_GAME,
    MAIN_MENU_ACTION_START_AI_GAME,
    MAIN_MENU_ACTION_CYCLE_AI_DIFFICULTY,
    MAIN_MENU_ACTION_CYCLE_HUMAN_COLOR,
    MAIN_MENU_ACTION_TOGGLE_THEME,
    MAIN_MENU_ACTION_QUIT
};

/* Draws the application's main menu overlay. */
void DrawMainMenu(void);

/* Returns the button currently activated by keyboard or mouse on the main menu. */
enum MainMenuAction HandleMainMenuInput(void);

/* Returns the currently selected AI difficulty for AI matches. */
enum AiDifficulty MainMenuGetAiDifficulty(void);
void MainMenuSetAiDifficulty(enum AiDifficulty difficulty);

/* Returns the currently selected human color for AI matches. */
enum PlayerType MainMenuGetHumanColor(void);
void MainMenuSetHumanColor(enum PlayerType player);

#endif
