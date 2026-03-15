#include "main_menu.h"

#include "ai_controller.h"
#include "renderer_ui.h"
#include "ui_state.h"

#include <raylib.h>
#include <stdio.h>

static Rectangle GetMainMenuPanelBounds(void);
static Rectangle GetStartButtonBounds(void);
static Rectangle GetAiStartButtonBounds(void);
static Rectangle GetThemeButtonBounds(void);
static Rectangle GetQuitButtonBounds(void);
static Rectangle GetStartPopupBounds(void);
static Rectangle GetStartPopupColorButtonBounds(void);
static Rectangle GetStartPopupDifficultyButtonBounds(void);
static Rectangle GetStartPopupConfirmButtonBounds(void);
static Rectangle GetStartPopupCancelButtonBounds(void);
static void DrawMenuButton(Rectangle bounds, const char *label, Color fill, Color border, Color text, bool emphasized);
static void DrawStartPopup(void);
static const char *PlayerName(enum PlayerType player);

static enum AiDifficulty gMainMenuAiDifficulty = AI_DIFFICULTY_MEDIUM;
static enum PlayerType gMainMenuHumanColor = PLAYER_RED;
static enum MainMenuAction gMainMenuPopupStartAction = MAIN_MENU_ACTION_NONE;

void DrawMainMenu(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    const Rectangle startButton = GetStartButtonBounds();
    const Rectangle aiStartButton = GetAiStartButtonBounds();
    const Rectangle themeButton = GetThemeButtonBounds();
    const Rectangle quitButton = GetQuitButtonBounds();
    const bool darkTheme = uiGetTheme() == UI_THEME_DARK;
    const Color panelColor = darkTheme ? (Color){35, 43, 55, 244} : (Color){245, 237, 217, 246};
    const Color borderColor = darkTheme ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
    const Color titleColor = darkTheme ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
    const Color bodyColor = darkTheme ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
    const Color accentColor = darkTheme ? (Color){188, 135, 83, 255} : (Color){171, 82, 54, 255};
    const Color goldColor = darkTheme ? (Color){219, 184, 106, 255} : (Color){191, 145, 61, 255};
    const int titleWidth = MeasureUiText("Settlers", 54);
    const int captionWidth = MeasureUiText("Build, trade, and test a full match flow.", 17);
    const char *themeLabel = darkTheme ? "Theme: Dark" : "Theme: Light";

    DrawRectangleRounded((Rectangle){panel.x + 10.0f, panel.y + 12.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, darkTheme ? 0.22f : 0.12f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawRectangleRounded((Rectangle){panel.x + 26.0f, panel.y + 24.0f, panel.width - 52.0f, 76.0f}, 0.18f, 8, Fade(accentColor, darkTheme ? 0.18f : 0.10f));
    DrawRectangleLinesEx((Rectangle){panel.x + 26.0f, panel.y + 24.0f, panel.width - 52.0f, 76.0f}, 1.5f, Fade(accentColor, 0.70f));
    DrawUiText("Settlers", panel.x + panel.width * 0.5f - titleWidth * 0.5f, panel.y + 34.0f, 54, titleColor);

    DrawUiText("Quick Start", panel.x + 30.0f, panel.y + 132.0f, 18, goldColor);
    DrawUiText("Build, trade, and test a full match flow.", panel.x + panel.width * 0.5f - captionWidth * 0.5f, panel.y + 160.0f, 17, bodyColor);

    DrawMenuButton(startButton, "Start Game", accentColor, borderColor, RAYWHITE, true);
    DrawMenuButton(aiStartButton, "Start vs AI", goldColor, borderColor, darkTheme ? (Color){38, 32, 24, 255} : RAYWHITE, true);
    DrawMenuButton(themeButton, themeLabel, darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, titleColor, false);
    DrawMenuButton(quitButton, "Quit", darkTheme ? (Color){82, 54, 54, 255} : (Color){231, 215, 204, 255}, borderColor, titleColor, false);

    DrawStartPopup();
}

enum MainMenuAction HandleMainMenuInput(void)
{
    const Vector2 mouse = GetMousePosition();

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        return MAIN_MENU_ACTION_NONE;
    }

    if (gMainMenuPopupStartAction != MAIN_MENU_ACTION_NONE)
    {
        if (!CheckCollisionPointRec(mouse, GetStartPopupBounds()) ||
            CheckCollisionPointRec(mouse, GetStartPopupCancelButtonBounds()))
        {
            gMainMenuPopupStartAction = MAIN_MENU_ACTION_NONE;
            return MAIN_MENU_ACTION_NONE;
        }

        if (CheckCollisionPointRec(mouse, GetStartPopupColorButtonBounds()))
        {
            gMainMenuHumanColor = (enum PlayerType)(((int)gMainMenuHumanColor + 1) % MAX_PLAYERS);
            return MAIN_MENU_ACTION_CYCLE_HUMAN_COLOR;
        }

        if (gMainMenuPopupStartAction == MAIN_MENU_ACTION_START_AI_GAME &&
            CheckCollisionPointRec(mouse, GetStartPopupDifficultyButtonBounds()))
        {
            gMainMenuAiDifficulty = (enum AiDifficulty)(((int)gMainMenuAiDifficulty + 1) % 3);
            return MAIN_MENU_ACTION_CYCLE_AI_DIFFICULTY;
        }

        if (CheckCollisionPointRec(mouse, GetStartPopupConfirmButtonBounds()))
        {
            const enum MainMenuAction action = gMainMenuPopupStartAction;
            gMainMenuPopupStartAction = MAIN_MENU_ACTION_NONE;
            return action;
        }

        return MAIN_MENU_ACTION_NONE;
    }

    if (CheckCollisionPointRec(mouse, GetStartButtonBounds()))
    {
        gMainMenuPopupStartAction = MAIN_MENU_ACTION_START_GAME;
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetAiStartButtonBounds()))
    {
        gMainMenuPopupStartAction = MAIN_MENU_ACTION_START_AI_GAME;
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetThemeButtonBounds()))
    {
        return MAIN_MENU_ACTION_TOGGLE_THEME;
    }
    if (CheckCollisionPointRec(mouse, GetQuitButtonBounds()))
    {
        return MAIN_MENU_ACTION_QUIT;
    }

    return MAIN_MENU_ACTION_NONE;
}

static Rectangle GetMainMenuPanelBounds(void)
{
    const float panelWidth = 452.0f;
    const float panelHeight = 454.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f - 18.0f,
        panelWidth,
        panelHeight};
}

static Rectangle GetStartButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 206.0f, panel.width - 68.0f, 52.0f};
}

static Rectangle GetAiStartButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 272.0f, panel.width - 68.0f, 52.0f};
}

static Rectangle GetThemeButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 338.0f, panel.width - 68.0f, 46.0f};
}

static Rectangle GetQuitButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 396.0f, panel.width - 68.0f, 40.0f};
}

static Rectangle GetStartPopupBounds(void)
{
    const float panelWidth = 408.0f;
    const float panelHeight = gMainMenuPopupStartAction == MAIN_MENU_ACTION_START_AI_GAME ? 286.0f : 232.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

static Rectangle GetStartPopupColorButtonBounds(void)
{
    const Rectangle panel = GetStartPopupBounds();
    return (Rectangle){panel.x + 28.0f, panel.y + 102.0f, panel.width - 56.0f, 44.0f};
}

static Rectangle GetStartPopupDifficultyButtonBounds(void)
{
    const Rectangle panel = GetStartPopupBounds();
    return (Rectangle){panel.x + 28.0f, panel.y + 156.0f, panel.width - 56.0f, 44.0f};
}

static Rectangle GetStartPopupConfirmButtonBounds(void)
{
    const Rectangle panel = GetStartPopupBounds();
    return (Rectangle){panel.x + 28.0f, panel.y + panel.height - 56.0f, panel.width - 168.0f, 38.0f};
}

static Rectangle GetStartPopupCancelButtonBounds(void)
{
    const Rectangle panel = GetStartPopupBounds();
    return (Rectangle){panel.x + panel.width - 122.0f, panel.y + panel.height - 56.0f, 94.0f, 38.0f};
}

enum AiDifficulty MainMenuGetAiDifficulty(void)
{
    return gMainMenuAiDifficulty;
}

enum PlayerType MainMenuGetHumanColor(void)
{
    return gMainMenuHumanColor;
}

static void DrawStartPopup(void)
{
    char colorLabel[32];
    char difficultyLabel[40];
    const bool darkTheme = uiGetTheme() == UI_THEME_DARK;
    const Rectangle panel = GetStartPopupBounds();
    const Rectangle colorButton = GetStartPopupColorButtonBounds();
    const Rectangle difficultyButton = GetStartPopupDifficultyButtonBounds();
    const Rectangle confirmButton = GetStartPopupConfirmButtonBounds();
    const Rectangle cancelButton = GetStartPopupCancelButtonBounds();
    const bool aiPopup = gMainMenuPopupStartAction == MAIN_MENU_ACTION_START_AI_GAME;
    const Color panelColor = darkTheme ? (Color){35, 43, 55, 252} : (Color){245, 237, 217, 252};
    const Color borderColor = darkTheme ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
    const Color textColor = darkTheme ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
    const Color bodyColor = darkTheme ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
    const Color accentColor = aiPopup ? (darkTheme ? (Color){219, 184, 106, 255} : (Color){191, 145, 61, 255}) : (darkTheme ? (Color){188, 135, 83, 255} : (Color){171, 82, 54, 255});

    if (gMainMenuPopupStartAction == MAIN_MENU_ACTION_NONE)
    {
        return;
    }

    snprintf(colorLabel, sizeof(colorLabel), "Your Color: %s", PlayerName(gMainMenuHumanColor));
    snprintf(difficultyLabel, sizeof(difficultyLabel), "AI Difficulty: %s", aiDifficultyLabel(gMainMenuAiDifficulty));

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, darkTheme ? 0.46f : 0.28f));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText(aiPopup ? "Start vs AI" : "Start Game", panel.x + 28.0f, panel.y + 24.0f, 30, textColor);
    DrawUiText("Setup order is random every match.", panel.x + 28.0f, panel.y + 62.0f, 18, bodyColor);
    DrawMenuButton(colorButton, colorLabel, darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, textColor, false);
    if (aiPopup)
    {
        DrawMenuButton(difficultyButton, difficultyLabel, darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, textColor, false);
    }
    DrawMenuButton(confirmButton, aiPopup ? "Start Match" : "Start Hotseat", accentColor, borderColor, aiPopup ? (darkTheme ? (Color){38, 32, 24, 255} : RAYWHITE) : RAYWHITE, true);
    DrawMenuButton(cancelButton, "Cancel", darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, textColor, false);
}

static void DrawMenuButton(Rectangle bounds, const char *label, Color fill, Color border, Color text, bool emphasized)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const float yOffset = pressed ? 2.0f : (hovered ? -2.0f : 0.0f);
    const Rectangle button = {bounds.x, bounds.y + yOffset, bounds.width, bounds.height};
    const int fontSize = emphasized ? 24 : 21;
    const int labelWidth = MeasureUiText(label, fontSize);
    const Color shadow = emphasized ? Fade(BLACK, 0.18f) : Fade(BLACK, 0.12f);

    DrawRectangleRounded((Rectangle){button.x + 4.0f, button.y + 6.0f, button.width, button.height}, 0.18f, 8, shadow);
    DrawRectangleRounded(button, 0.18f, 8, hovered ? ColorBrightness(fill, 0.08f) : fill);
    DrawRectangleLinesEx(button, emphasized ? 2.2f : 1.8f, hovered ? ColorBrightness(border, 0.08f) : border);
    DrawUiText(label, button.x + button.width * 0.5f - labelWidth * 0.5f, button.y + button.height * 0.5f - (float)fontSize * 0.5f - 1.0f, fontSize, text);
}

static const char *PlayerName(enum PlayerType player)
{
    switch (player)
    {
    case PLAYER_RED:
        return "Red";
    case PLAYER_BLUE:
        return "Blue";
    case PLAYER_GREEN:
        return "Green";
    case PLAYER_BLACK:
        return "Black";
    default:
        return "Red";
    }
}
