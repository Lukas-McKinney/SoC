#include "main_menu.h"

#include "ai_controller.h"
#include "localization.h"
#include "renderer_ui.h"
#include "settings_store.h"
#include "ui_state.h"

#include <ctype.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum MainMenuMultiplayerMode
{
    MAIN_MENU_MULTIPLAYER_HOST,
    MAIN_MENU_MULTIPLAYER_JOIN
};

enum MainMenuMultiplayerField
{
    MAIN_MENU_MULTIPLAYER_FIELD_NONE,
    MAIN_MENU_MULTIPLAYER_FIELD_HOST_ADDRESS,
    MAIN_MENU_MULTIPLAYER_FIELD_PORT
};

struct MultiplayerPopupLayout
{
    Rectangle panel;
    Rectangle hostModeButton;
    Rectangle joinModeButton;
    Rectangle localColorButton;
    Rectangle remoteColorButton;
    Rectangle aiDifficultyButton;
    Rectangle hostAddressField;
    Rectangle portField;
    Rectangle confirmButton;
    Rectangle cancelButton;
    Rectangle closeButton;
};

static Rectangle GetMainMenuPanelBounds(void);
static Rectangle GetStartButtonBounds(void);
static Rectangle GetAiStartButtonBounds(void);
static Rectangle GetMultiplayerButtonBounds(void);
static Rectangle GetStatisticsButtonBounds(void);
static Rectangle GetMainMenuSettingsButtonBounds(void);
static Rectangle GetQuitButtonBounds(void);
static Rectangle GetStartPopupBounds(void);
static Rectangle GetStartPopupColorButtonBounds(void);
static Rectangle GetStartPopupDifficultyButtonBounds(void);
static Rectangle GetStartPopupConfirmButtonBounds(void);
static Rectangle GetStartPopupCancelButtonBounds(void);
static Rectangle GetStatisticsPopupBounds(void);
static Rectangle GetStatisticsPopupCloseButtonBounds(void);
static Rectangle GetSettingsPopupBounds(void);
static Rectangle GetSettingsPopupThemeButtonBounds(void);
static Rectangle GetSettingsPopupDisplayButtonBounds(void);
static Rectangle GetSettingsPopupLanguageButtonBounds(void);
static Rectangle GetSettingsPopupAiSpeedTrackBounds(void);
static Rectangle GetSettingsPopupCloseButtonBounds(void);
static struct MultiplayerPopupLayout BuildMultiplayerPopupLayout(void);
static void DrawMenuButton(Rectangle bounds, const char *label, Color fill, Color border, Color text, bool emphasized);
static void DrawStatisticsRow(Rectangle bounds, float y, const char *label, const char *value, Color labelColor, Color valueColor);
static void DrawTextField(Rectangle bounds, const char *label, const char *value, const char *placeholder,
                          bool active, Color fill, Color border, Color textColor, Color labelColor);
static void DrawStartPopup(void);
static void DrawStatisticsPopup(void);
static void DrawSettingsPopup(void);
static void DrawMultiplayerPopup(void);
static void FormatElapsedDuration(unsigned long long totalSeconds, char *buffer, size_t bufferSize);
static const char *PlayerName(enum PlayerType player);
static void NormalizeMultiplayerSelection(void);
static enum PlayerType NextPlayerColor(enum PlayerType player, enum PlayerType excluded);
static void CycleMultiplayerLocalColor(void);
static void CycleMultiplayerRemoteColor(void);
static void CycleMultiplayerAiDifficulty(void);
static void HandleMultiplayerKeyboardInput(void);
static void AppendMultiplayerFieldChar(char *buffer, size_t bufferSize, int codepoint);
static void RemoveMultiplayerFieldChar(char *buffer);
static bool ValidateMultiplayerConfig(char *message, size_t messageSize);

static enum AiDifficulty gMainMenuAiDifficulty = AI_DIFFICULTY_MEDIUM;
static enum PlayerType gMainMenuHumanColor = PLAYER_RED;
static enum MainMenuAction gMainMenuPopupStartAction = MAIN_MENU_ACTION_NONE;
static bool gMainMenuStatisticsOpen = false;
static bool gMainMenuSettingsOpen = false;
static bool gMainMenuMultiplayerOpen = false;
static enum MainMenuMultiplayerMode gMainMenuMultiplayerMode = MAIN_MENU_MULTIPLAYER_HOST;
static enum MainMenuMultiplayerField gMainMenuMultiplayerField = MAIN_MENU_MULTIPLAYER_FIELD_NONE;
static enum PlayerType gMainMenuMultiplayerLocalColor = PLAYER_RED;
static enum PlayerType gMainMenuMultiplayerRemoteColor = PLAYER_BLUE;
static enum AiDifficulty gMainMenuMultiplayerAiDifficulty = AI_DIFFICULTY_MEDIUM;
static char gMainMenuMultiplayerHostAddress[64] = "127.0.0.1";
static char gMainMenuMultiplayerPortText[8] = "24680";
static char gMainMenuMultiplayerError[96] = {0};

void DrawMainMenu(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    const Rectangle startButton = GetStartButtonBounds();
    const Rectangle aiStartButton = GetAiStartButtonBounds();
    const Rectangle multiplayerButton = GetMultiplayerButtonBounds();
    const Rectangle statisticsButton = GetStatisticsButtonBounds();
    const Rectangle settingsButton = GetMainMenuSettingsButtonBounds();
    const Rectangle quitButton = GetQuitButtonBounds();
    const bool darkTheme = uiGetTheme() == UI_THEME_DARK;
    const Color panelColor = darkTheme ? (Color){35, 43, 55, 244} : (Color){245, 237, 217, 246};
    const Color borderColor = darkTheme ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
    const Color titleColor = darkTheme ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
    const Color bodyColor = darkTheme ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
    const Color accentColor = darkTheme ? (Color){188, 135, 83, 255} : (Color){171, 82, 54, 255};
    const Color goldColor = darkTheme ? (Color){219, 184, 106, 255} : (Color){191, 145, 61, 255};
    const char *titleLabel = loc("Settlers");
    const char *captionLabel = loc("Build, trade, and test a full match flow.");
    const int titleWidth = MeasureUiText(titleLabel, 54);
    const int captionWidth = MeasureUiText(captionLabel, 17);

    DrawRectangleRounded((Rectangle){panel.x + 10.0f, panel.y + 12.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, darkTheme ? 0.22f : 0.12f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawRectangleRounded((Rectangle){panel.x + 26.0f, panel.y + 24.0f, panel.width - 52.0f, 76.0f}, 0.18f, 8, Fade(accentColor, darkTheme ? 0.18f : 0.10f));
    DrawRectangleLinesEx((Rectangle){panel.x + 26.0f, panel.y + 24.0f, panel.width - 52.0f, 76.0f}, 1.5f, Fade(accentColor, 0.70f));
    DrawUiText(titleLabel, panel.x + panel.width * 0.5f - titleWidth * 0.5f, panel.y + 34.0f, 54, titleColor);

    DrawUiText(loc("Quick Start"), panel.x + 30.0f, panel.y + 132.0f, 18, goldColor);
    DrawUiText(captionLabel, panel.x + panel.width * 0.5f - captionWidth * 0.5f, panel.y + 160.0f, 17, bodyColor);

    DrawMenuButton(startButton, loc("Start Game"), accentColor, borderColor, RAYWHITE, true);
    DrawMenuButton(aiStartButton, loc("Start vs AI"), goldColor, borderColor, darkTheme ? (Color){38, 32, 24, 255} : RAYWHITE, true);
    DrawMenuButton(multiplayerButton, loc("Multiplayer"), darkTheme ? (Color){88, 103, 129, 255} : (Color){222, 216, 204, 255}, borderColor, titleColor, false);
    DrawMenuButton(statisticsButton, loc("Statistics"), darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, titleColor, false);
    DrawMenuButton(settingsButton, loc("Settings"), darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, titleColor, false);
    DrawMenuButton(quitButton, loc("Quit"), darkTheme ? (Color){82, 54, 54, 255} : (Color){231, 215, 204, 255}, borderColor, titleColor, false);
    DrawStartPopup();
    DrawStatisticsPopup();
    DrawSettingsPopup();
    DrawMultiplayerPopup();
}

enum MainMenuAction HandleMainMenuInput(void)
{
    const Vector2 mouse = GetMousePosition();
    const bool leftPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if (gMainMenuMultiplayerOpen)
    {
        struct MultiplayerPopupLayout layout = BuildMultiplayerPopupLayout();
        char validationError[96];

        HandleMultiplayerKeyboardInput();

        if (IsKeyPressed(KEY_ESCAPE))
        {
            MainMenuSetMultiplayerOpen(false);
            return MAIN_MENU_ACTION_NONE;
        }

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        {
            if (ValidateMultiplayerConfig(validationError, sizeof(validationError)))
            {
                gMainMenuMultiplayerError[0] = '\0';
                return gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_HOST
                           ? MAIN_MENU_ACTION_START_PRIVATE_HOST
                           : MAIN_MENU_ACTION_START_PRIVATE_JOIN;
            }

            MainMenuSetMultiplayerError(validationError);
            return MAIN_MENU_ACTION_NONE;
        }

        if (!leftPressed)
        {
            return MAIN_MENU_ACTION_NONE;
        }

        if (!CheckCollisionPointRec(mouse, layout.panel) || CheckCollisionPointRec(mouse, layout.cancelButton) || CheckCollisionPointRec(mouse, layout.closeButton))
        {
            MainMenuSetMultiplayerOpen(false);
            return MAIN_MENU_ACTION_NONE;
        }

        if (CheckCollisionPointRec(mouse, layout.hostModeButton))
        {
            gMainMenuMultiplayerMode = MAIN_MENU_MULTIPLAYER_HOST;
            gMainMenuMultiplayerField = MAIN_MENU_MULTIPLAYER_FIELD_PORT;
            gMainMenuMultiplayerError[0] = '\0';
            NormalizeMultiplayerSelection();
            return MAIN_MENU_ACTION_NONE;
        }
        if (CheckCollisionPointRec(mouse, layout.joinModeButton))
        {
            gMainMenuMultiplayerMode = MAIN_MENU_MULTIPLAYER_JOIN;
            gMainMenuMultiplayerField = MAIN_MENU_MULTIPLAYER_FIELD_HOST_ADDRESS;
            gMainMenuMultiplayerError[0] = '\0';
            NormalizeMultiplayerSelection();
            return MAIN_MENU_ACTION_NONE;
        }
        if (gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_HOST && CheckCollisionPointRec(mouse, layout.localColorButton))
        {
            CycleMultiplayerLocalColor();
            gMainMenuMultiplayerError[0] = '\0';
            return MAIN_MENU_ACTION_NONE;
        }
        if (gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_HOST && CheckCollisionPointRec(mouse, layout.remoteColorButton))
        {
            CycleMultiplayerRemoteColor();
            gMainMenuMultiplayerError[0] = '\0';
            return MAIN_MENU_ACTION_NONE;
        }
        if (gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_HOST && CheckCollisionPointRec(mouse, layout.aiDifficultyButton))
        {
            CycleMultiplayerAiDifficulty();
            gMainMenuMultiplayerError[0] = '\0';
            return MAIN_MENU_ACTION_NONE;
        }
        if (gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_JOIN && CheckCollisionPointRec(mouse, layout.hostAddressField))
        {
            gMainMenuMultiplayerField = MAIN_MENU_MULTIPLAYER_FIELD_HOST_ADDRESS;
            return MAIN_MENU_ACTION_NONE;
        }
        if (CheckCollisionPointRec(mouse, layout.portField))
        {
            gMainMenuMultiplayerField = MAIN_MENU_MULTIPLAYER_FIELD_PORT;
            return MAIN_MENU_ACTION_NONE;
        }
        if (CheckCollisionPointRec(mouse, layout.confirmButton))
        {
            if (ValidateMultiplayerConfig(validationError, sizeof(validationError)))
            {
                gMainMenuMultiplayerError[0] = '\0';
                return gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_HOST
                           ? MAIN_MENU_ACTION_START_PRIVATE_HOST
                           : MAIN_MENU_ACTION_START_PRIVATE_JOIN;
            }

            MainMenuSetMultiplayerError(validationError);
        }
        return MAIN_MENU_ACTION_NONE;
    }

    if (gMainMenuSettingsOpen)
    {
        const Rectangle settingsPanel = GetSettingsPopupBounds();
        const Rectangle themeButton = GetSettingsPopupThemeButtonBounds();
        const Rectangle displayButton = GetSettingsPopupDisplayButtonBounds();
        const Rectangle languageButton = GetSettingsPopupLanguageButtonBounds();
        const Rectangle aiSpeedTrack = GetSettingsPopupAiSpeedTrackBounds();
        const Rectangle closeButton = GetSettingsPopupCloseButtonBounds();

        if (IsKeyPressed(KEY_ESCAPE))
        {
            gMainMenuSettingsOpen = false;
            return MAIN_MENU_ACTION_NONE;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, aiSpeedTrack))
        {
            const float normalized = (mouse.x - aiSpeedTrack.x) / aiSpeedTrack.width;
            const int speed = (int)(normalized * 10.0f + 0.5f);
            if (speed != uiGetAiSpeedSetting())
            {
                uiSetAiSpeedSetting(speed);
                settingsStoreSaveCurrent();
            }
            return MAIN_MENU_ACTION_NONE;
        }

        if (!leftPressed)
        {
            return MAIN_MENU_ACTION_NONE;
        }

        if (!CheckCollisionPointRec(mouse, settingsPanel) || CheckCollisionPointRec(mouse, closeButton))
        {
            gMainMenuSettingsOpen = false;
            return MAIN_MENU_ACTION_NONE;
        }

        if (CheckCollisionPointRec(mouse, themeButton))
        {
            uiSetTheme(uiGetTheme() == UI_THEME_DARK ? UI_THEME_LIGHT : UI_THEME_DARK);
            settingsStoreSaveCurrent();
            return MAIN_MENU_ACTION_NONE;
        }
        if (CheckCollisionPointRec(mouse, displayButton))
        {
            uiSetWindowMode(uiGetWindowMode() == UI_WINDOW_MODE_FULLSCREEN ? UI_WINDOW_MODE_WINDOWED : UI_WINDOW_MODE_FULLSCREEN);
            settingsStoreSaveCurrent();
            return MAIN_MENU_ACTION_NONE;
        }
        if (CheckCollisionPointRec(mouse, languageButton))
        {
            locSetLanguage((enum UiLanguage)((locGetLanguage() + 1) % UI_LANGUAGE_COUNT));
            settingsStoreSaveCurrent();
            return MAIN_MENU_ACTION_NONE;
        }

        return MAIN_MENU_ACTION_NONE;
    }

    if (!leftPressed)
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

        if (gMainMenuPopupStartAction == MAIN_MENU_ACTION_START_AI_GAME &&
            CheckCollisionPointRec(mouse, GetStartPopupColorButtonBounds()))
        {
            MainMenuSetHumanColor((enum PlayerType)(((int)gMainMenuHumanColor + 1) % MAX_PLAYERS));
            settingsStoreSaveCurrent();
            return MAIN_MENU_ACTION_CYCLE_HUMAN_COLOR;
        }

        if (gMainMenuPopupStartAction == MAIN_MENU_ACTION_START_AI_GAME &&
            CheckCollisionPointRec(mouse, GetStartPopupDifficultyButtonBounds()))
        {
            MainMenuSetAiDifficulty((enum AiDifficulty)(((int)gMainMenuAiDifficulty + 1) % 3));
            settingsStoreSaveCurrent();
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

    if (gMainMenuStatisticsOpen)
    {
        if (!CheckCollisionPointRec(mouse, GetStatisticsPopupBounds()) ||
            CheckCollisionPointRec(mouse, GetStatisticsPopupCloseButtonBounds()))
        {
            gMainMenuStatisticsOpen = false;
        }
        return MAIN_MENU_ACTION_NONE;
    }

    if (CheckCollisionPointRec(mouse, GetStartButtonBounds()))
    {
        gMainMenuPopupStartAction = MAIN_MENU_ACTION_START_GAME;
        gMainMenuStatisticsOpen = false;
        gMainMenuSettingsOpen = false;
        gMainMenuMultiplayerOpen = false;
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetAiStartButtonBounds()))
    {
        gMainMenuPopupStartAction = MAIN_MENU_ACTION_START_AI_GAME;
        gMainMenuStatisticsOpen = false;
        gMainMenuSettingsOpen = false;
        gMainMenuMultiplayerOpen = false;
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetMultiplayerButtonBounds()))
    {
        MainMenuSetMultiplayerOpen(true);
        gMainMenuPopupStartAction = MAIN_MENU_ACTION_NONE;
        gMainMenuStatisticsOpen = false;
        gMainMenuSettingsOpen = false;
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetStatisticsButtonBounds()))
    {
        gMainMenuStatisticsOpen = true;
        gMainMenuPopupStartAction = MAIN_MENU_ACTION_NONE;
        gMainMenuSettingsOpen = false;
        gMainMenuMultiplayerOpen = false;
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetMainMenuSettingsButtonBounds()))
    {
        gMainMenuSettingsOpen = true;
        gMainMenuPopupStartAction = MAIN_MENU_ACTION_NONE;
        gMainMenuStatisticsOpen = false;
        gMainMenuMultiplayerOpen = false;
        return MAIN_MENU_ACTION_NONE;
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
    const float panelHeight = 588.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f - 18.0f,
        panelWidth,
        panelHeight};
}

static Rectangle GetStartButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 214.0f, panel.width - 68.0f, 52.0f};
}

static Rectangle GetAiStartButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 280.0f, panel.width - 68.0f, 52.0f};
}

static Rectangle GetMultiplayerButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 346.0f, panel.width - 68.0f, 46.0f};
}

static Rectangle GetStatisticsButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 404.0f, panel.width - 68.0f, 46.0f};
}

static Rectangle GetMainMenuSettingsButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 462.0f, panel.width - 68.0f, 46.0f};
}

static Rectangle GetQuitButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 520.0f, panel.width - 68.0f, 40.0f};
}

static Rectangle GetStartPopupBounds(void)
{
    const float panelWidth = 408.0f;
    const float panelHeight = gMainMenuPopupStartAction == MAIN_MENU_ACTION_START_AI_GAME ? 286.0f : 176.0f;
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

static Rectangle GetStatisticsPopupBounds(void)
{
    const float panelWidth = 408.0f;
    const float panelHeight = 294.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

static Rectangle GetStatisticsPopupCloseButtonBounds(void)
{
    const Rectangle panel = GetStatisticsPopupBounds();
    return (Rectangle){panel.x + 28.0f, panel.y + panel.height - 56.0f, panel.width - 56.0f, 38.0f};
}

static Rectangle GetSettingsPopupBounds(void)
{
    const float panelWidth = 408.0f;
    const float panelHeight = 404.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

static Rectangle GetSettingsPopupThemeButtonBounds(void)
{
    const Rectangle panel = GetSettingsPopupBounds();
    return (Rectangle){panel.x + 28.0f, panel.y + 88.0f, panel.width - 56.0f, 42.0f};
}

static Rectangle GetSettingsPopupDisplayButtonBounds(void)
{
    const Rectangle panel = GetSettingsPopupBounds();
    return (Rectangle){panel.x + 28.0f, panel.y + 142.0f, panel.width - 56.0f, 42.0f};
}

static Rectangle GetSettingsPopupLanguageButtonBounds(void)
{
    const Rectangle panel = GetSettingsPopupBounds();
    return (Rectangle){panel.x + 28.0f, panel.y + 196.0f, panel.width - 56.0f, 42.0f};
}

static Rectangle GetSettingsPopupAiSpeedTrackBounds(void)
{
    const Rectangle panel = GetSettingsPopupBounds();
    return (Rectangle){panel.x + 36.0f, panel.y + 288.0f, panel.width - 72.0f, 10.0f};
}

static Rectangle GetSettingsPopupCloseButtonBounds(void)
{
    const Rectangle panel = GetSettingsPopupBounds();
    return (Rectangle){panel.x + 28.0f, panel.y + panel.height - 56.0f, panel.width - 56.0f, 38.0f};
}

static struct MultiplayerPopupLayout BuildMultiplayerPopupLayout(void)
{
    const bool joinMode = gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_JOIN;
    const float panelWidth = 468.0f;
    const float panelHeight = joinMode ? 498.0f : 442.0f;
    const float fieldWidth = panelWidth - 52.0f;
    const float modeButtonWidth = (fieldWidth - 10.0f) * 0.5f;
    const Rectangle panel = {
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
    struct MultiplayerPopupLayout layout;

    layout.panel = panel;
    layout.hostModeButton = (Rectangle){panel.x + 26.0f, panel.y + 102.0f, modeButtonWidth, 42.0f};
    layout.joinModeButton = (Rectangle){layout.hostModeButton.x + modeButtonWidth + 10.0f, panel.y + 102.0f, modeButtonWidth, 42.0f};
    layout.localColorButton = (Rectangle){panel.x + 26.0f, panel.y + 164.0f, fieldWidth, 42.0f};
    layout.remoteColorButton = (Rectangle){panel.x + 26.0f, panel.y + 218.0f, fieldWidth, 42.0f};
    layout.aiDifficultyButton = (Rectangle){panel.x + 26.0f, panel.y + 272.0f, fieldWidth, 42.0f};
    layout.hostAddressField = (Rectangle){panel.x + 26.0f, panel.y + 272.0f, fieldWidth, 50.0f};
    layout.portField = (Rectangle){panel.x + 26.0f, panel.y + (joinMode ? 346.0f : 326.0f), fieldWidth, 50.0f};
    layout.confirmButton = (Rectangle){panel.x + 26.0f, panel.y + panel.height - 58.0f, panel.width - 168.0f, 38.0f};
    layout.cancelButton = (Rectangle){panel.x + panel.width - 122.0f, panel.y + panel.height - 58.0f, 94.0f, 38.0f};
    layout.closeButton = (Rectangle){panel.x + panel.width - 42.0f, panel.y + 14.0f, 24.0f, 24.0f};
    return layout;
}

enum AiDifficulty MainMenuGetAiDifficulty(void)
{
    return gMainMenuAiDifficulty;
}

void MainMenuSetAiDifficulty(enum AiDifficulty difficulty)
{
    if (difficulty < AI_DIFFICULTY_EASY || difficulty > AI_DIFFICULTY_HARD)
    {
        difficulty = AI_DIFFICULTY_MEDIUM;
    }

    gMainMenuAiDifficulty = difficulty;
    if (gMainMenuMultiplayerAiDifficulty < AI_DIFFICULTY_EASY || gMainMenuMultiplayerAiDifficulty > AI_DIFFICULTY_HARD)
    {
        gMainMenuMultiplayerAiDifficulty = difficulty;
    }
}

enum PlayerType MainMenuGetHumanColor(void)
{
    return gMainMenuHumanColor;
}

void MainMenuSetHumanColor(enum PlayerType player)
{
    if (player < PLAYER_RED || player > PLAYER_BLACK)
    {
        player = PLAYER_RED;
    }

    gMainMenuHumanColor = player;
}

enum PlayerType MainMenuGetMultiplayerLocalColor(void)
{
    NormalizeMultiplayerSelection();
    return gMainMenuMultiplayerLocalColor;
}

enum PlayerType MainMenuGetMultiplayerRemoteColor(void)
{
    NormalizeMultiplayerSelection();
    return gMainMenuMultiplayerRemoteColor;
}

enum AiDifficulty MainMenuGetMultiplayerAiDifficulty(void)
{
    return gMainMenuMultiplayerAiDifficulty;
}

unsigned short MainMenuGetMultiplayerPort(void)
{
    const long parsed = strtol(gMainMenuMultiplayerPortText, NULL, 10);
    return parsed > 0L && parsed <= 65535L ? (unsigned short)parsed : 24680u;
}

const char *MainMenuGetMultiplayerHostAddress(void)
{
    return gMainMenuMultiplayerHostAddress;
}

void MainMenuSetMultiplayerPort(unsigned short port)
{
    if (port == 0u)
    {
        port = 24680u;
    }

    snprintf(gMainMenuMultiplayerPortText, sizeof(gMainMenuMultiplayerPortText), "%u", (unsigned int)port);
}

void MainMenuSetMultiplayerHostAddress(const char *address)
{
    snprintf(gMainMenuMultiplayerHostAddress,
             sizeof(gMainMenuMultiplayerHostAddress),
             "%s",
             (address != NULL && address[0] != '\0') ? address : "127.0.0.1");
}

void MainMenuSetMultiplayerOpen(bool open)
{
    gMainMenuMultiplayerOpen = open;
    if (open)
    {
        gMainMenuSettingsOpen = false;
    }
    gMainMenuMultiplayerField = open
                                    ? (gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_JOIN
                                           ? MAIN_MENU_MULTIPLAYER_FIELD_HOST_ADDRESS
                                           : MAIN_MENU_MULTIPLAYER_FIELD_PORT)
                                    : MAIN_MENU_MULTIPLAYER_FIELD_NONE;
    if (!open)
    {
        gMainMenuMultiplayerError[0] = '\0';
    }
    NormalizeMultiplayerSelection();
}

void MainMenuSetMultiplayerError(const char *message)
{
    snprintf(gMainMenuMultiplayerError, sizeof(gMainMenuMultiplayerError), "%s", message == NULL ? "" : message);
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

    snprintf(colorLabel, sizeof(colorLabel), loc("Your Color: %s"), PlayerName(gMainMenuHumanColor));
    snprintf(difficultyLabel, sizeof(difficultyLabel), loc("AI Difficulty: %s"), aiDifficultyLabel(gMainMenuAiDifficulty));

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, darkTheme ? 0.46f : 0.28f));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText(aiPopup ? loc("Start vs AI") : loc("Start Game"), panel.x + 28.0f, panel.y + 24.0f, 30, textColor);
    DrawUiText(loc("Setup order is random every match."), panel.x + 28.0f, panel.y + 62.0f, 18, bodyColor);
    if (aiPopup)
    {
        DrawMenuButton(colorButton, colorLabel, darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, textColor, false);
        DrawMenuButton(difficultyButton, difficultyLabel, darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, textColor, false);
    }
    DrawMenuButton(confirmButton, aiPopup ? loc("Start Match") : loc("Start Hotseat"), accentColor, borderColor, aiPopup ? (darkTheme ? (Color){38, 32, 24, 255} : RAYWHITE) : RAYWHITE, true);
    DrawMenuButton(cancelButton, loc("Cancel"), darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, textColor, false);
}

static void DrawStatisticsPopup(void)
{
    const bool darkTheme = uiGetTheme() == UI_THEME_DARK;
    const Rectangle panel = GetStatisticsPopupBounds();
    const Rectangle closeButton = GetStatisticsPopupCloseButtonBounds();
    const Rectangle statsCard = {panel.x + 28.0f, panel.y + 74.0f, panel.width - 56.0f, 112.0f};
    const Color panelColor = darkTheme ? (Color){35, 43, 55, 252} : (Color){245, 237, 217, 252};
    const Color borderColor = darkTheme ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
    const Color textColor = darkTheme ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
    const Color bodyColor = darkTheme ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
    const unsigned long long totalWins = uiGetTotalWins();
    const unsigned long long totalLosses = uiGetTotalLosses();
    const unsigned long long totalMatches = totalWins + totalLosses;
    char playtimeLabel[32];
    char winsLabel[24];
    char lossesLabel[24];
    char winrateLabel[24];

    if (!gMainMenuStatisticsOpen)
    {
        return;
    }

    FormatElapsedDuration(uiGetTotalPlaytimeSeconds(), playtimeLabel, sizeof(playtimeLabel));
    snprintf(winsLabel, sizeof(winsLabel), "%llu", totalWins);
    snprintf(lossesLabel, sizeof(lossesLabel), "%llu", totalLosses);
    if (totalMatches > 0ULL)
    {
        snprintf(winrateLabel, sizeof(winrateLabel), "%.1f%%", (double)totalWins * 100.0 / (double)totalMatches);
    }
    else
    {
        snprintf(winrateLabel, sizeof(winrateLabel), "--");
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, darkTheme ? 0.46f : 0.28f));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText(loc("Statistics"), panel.x + 28.0f, panel.y + 24.0f, 30, textColor);
    DrawUiText(loc("Tracked across sessions."), panel.x + 28.0f, panel.y + 58.0f, 18, bodyColor);
    DrawRectangleRounded((Rectangle){statsCard.x + 4.0f, statsCard.y + 6.0f, statsCard.width, statsCard.height}, 0.16f, 8, Fade(BLACK, darkTheme ? 0.16f : 0.08f));
    DrawRectangleRounded(statsCard, 0.16f, 8, darkTheme ? (Color){44, 55, 69, 248} : (Color){238, 229, 208, 248});
    DrawRectangleLinesEx(statsCard, 1.8f, Fade(borderColor, 0.90f));
    DrawStatisticsRow(statsCard, statsCard.y + 14.0f, loc("Total Playtime"), playtimeLabel, bodyColor, textColor);
    DrawStatisticsRow(statsCard, statsCard.y + 38.0f, loc("Wins"), winsLabel, bodyColor, textColor);
    DrawStatisticsRow(statsCard, statsCard.y + 62.0f, loc("Losses"), lossesLabel, bodyColor, textColor);
    DrawStatisticsRow(statsCard, statsCard.y + 86.0f, loc("Win Rate"), winrateLabel, bodyColor, textColor);
    DrawUiText(loc("Wins and losses count single-player matches."), panel.x + 28.0f, closeButton.y - 30.0f, 16, bodyColor);
    DrawMenuButton(closeButton, loc("Close"), darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, textColor, false);
}

static void DrawSettingsPopup(void)
{
    const bool darkTheme = uiGetTheme() == UI_THEME_DARK;
    const enum UiWindowMode windowMode = uiGetWindowMode();
    const Rectangle panel = GetSettingsPopupBounds();
    const Rectangle themeButton = GetSettingsPopupThemeButtonBounds();
    const Rectangle displayButton = GetSettingsPopupDisplayButtonBounds();
    const Rectangle languageButton = GetSettingsPopupLanguageButtonBounds();
    const Rectangle aiSpeedTrack = GetSettingsPopupAiSpeedTrackBounds();
    const Rectangle closeButton = GetSettingsPopupCloseButtonBounds();
    const Color panelColor = darkTheme ? (Color){35, 43, 55, 252} : (Color){245, 237, 217, 252};
    const Color borderColor = darkTheme ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
    const Color textColor = darkTheme ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
    const Color bodyColor = darkTheme ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
    const Color sectionFill = darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255};
    const Color accentColor = darkTheme ? (Color){188, 135, 83, 255} : (Color){171, 82, 54, 255};
    const int aiSpeed = uiGetAiSpeedSetting();
    const float aiSpeedNormalized = (float)aiSpeed / 10.0f;
    const float aiSpeedKnobX = aiSpeedTrack.x + aiSpeedTrack.width * aiSpeedNormalized;
    char themeLabel[48];
    char displayLabel[48];
    char languageLabel[64];

    if (!gMainMenuSettingsOpen)
    {
        return;
    }

    snprintf(themeLabel,
             sizeof(themeLabel),
             loc("Theme: %s"),
             uiGetTheme() == UI_THEME_DARK ? loc("Dark") : loc("Light"));
    snprintf(displayLabel,
             sizeof(displayLabel),
             "%s: %s",
             loc("Display Mode"),
             loc(windowMode == UI_WINDOW_MODE_FULLSCREEN ? "Fullscreen" : "Windowed"));
    snprintf(languageLabel,
             sizeof(languageLabel),
             loc("Language: %s"),
             locLanguageDisplayName(locGetLanguage()));

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, darkTheme ? 0.46f : 0.28f));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText(loc("Settings"), panel.x + 28.0f, panel.y + 24.0f, 30, textColor);
    DrawUiText(loc("Tracked across sessions."), panel.x + 28.0f, panel.y + 58.0f, 18, bodyColor);
    DrawMenuButton(themeButton, themeLabel, sectionFill, borderColor, textColor, false);
    DrawMenuButton(displayButton, displayLabel, sectionFill, borderColor, textColor, false);
    DrawMenuButton(languageButton, languageLabel, sectionFill, borderColor, textColor, false);

    DrawUiText(loc("AI Speed"), panel.x + 28.0f, panel.y + 246.0f, 18, bodyColor);
    DrawUiText(loc("0 slow"), aiSpeedTrack.x, aiSpeedTrack.y + 16.0f, 14, bodyColor);
    DrawUiText(loc("10 instant"), aiSpeedTrack.x + aiSpeedTrack.width - MeasureUiText(loc("10 instant"), 14), aiSpeedTrack.y + 16.0f, 14, bodyColor);
    DrawRectangleRounded(aiSpeedTrack, 0.45f, 8, darkTheme ? (Color){51, 63, 79, 255} : (Color){224, 216, 198, 255});
    DrawRectangleRounded((Rectangle){aiSpeedTrack.x, aiSpeedTrack.y, aiSpeedTrack.width * aiSpeedNormalized, aiSpeedTrack.height}, 0.45f, 8, accentColor);
    DrawCircleV((Vector2){aiSpeedKnobX, aiSpeedTrack.y + aiSpeedTrack.height * 0.5f}, 9.0f, darkTheme ? (Color){236, 241, 246, 255} : (Color){247, 240, 226, 255});
    DrawCircleLines((int)aiSpeedKnobX, (int)(aiSpeedTrack.y + aiSpeedTrack.height * 0.5f), 9.0f, borderColor);
    DrawUiText(TextFormat("%d", aiSpeed), panel.x + panel.width * 0.5f - 5.0f, aiSpeedTrack.y - 24.0f, 18, textColor);

    DrawMenuButton(closeButton, loc("Close"), sectionFill, borderColor, textColor, false);
}

static void DrawMultiplayerPopup(void)
{
    struct MultiplayerPopupLayout layout;
    char localColorLabel[48];
    char remoteColorLabel[48];
    char difficultyLabel[48];
    const bool darkTheme = uiGetTheme() == UI_THEME_DARK;
    const bool joinMode = gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_JOIN;
    const Color panelColor = darkTheme ? (Color){35, 43, 55, 252} : (Color){245, 237, 217, 252};
    const Color borderColor = darkTheme ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
    const Color textColor = darkTheme ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
    const Color bodyColor = darkTheme ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
    const Color accentColor = darkTheme ? (Color){188, 135, 83, 255} : (Color){171, 82, 54, 255};
    const Color sectionFill = darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255};

    if (!gMainMenuMultiplayerOpen)
    {
        return;
    }

    NormalizeMultiplayerSelection();
    layout = BuildMultiplayerPopupLayout();
    snprintf(localColorLabel, sizeof(localColorLabel), loc("Your Color: %s"), PlayerName(gMainMenuMultiplayerLocalColor));
    snprintf(remoteColorLabel, sizeof(remoteColorLabel),
             joinMode ? loc("Host Color: %s") : loc("Remote Color: %s"),
             PlayerName(gMainMenuMultiplayerRemoteColor));
    snprintf(difficultyLabel, sizeof(difficultyLabel), loc("AI Difficulty: %s"), aiDifficultyLabel(gMainMenuMultiplayerAiDifficulty));

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, darkTheme ? 0.46f : 0.28f));
    DrawRectangleRounded((Rectangle){layout.panel.x + 8.0f, layout.panel.y + 10.0f, layout.panel.width, layout.panel.height}, 0.08f, 8, Fade(BLACK, 0.16f));
    DrawRectangleRounded(layout.panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(layout.panel, 2.0f, borderColor);

    DrawUiText(loc("Multiplayer"), layout.panel.x + 26.0f, layout.panel.y + 24.0f, 30, textColor);
    DrawUiText(loc("Private multiplayer currently supports 2 humans and AI."), layout.panel.x + 26.0f, layout.panel.y + 60.0f, 17, bodyColor);
    DrawUiText(loc("Mode"), layout.panel.x + 26.0f, layout.panel.y + 82.0f, 16, bodyColor);
    DrawMenuButton(layout.hostModeButton, loc("Host"),
                   gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_HOST ? accentColor : sectionFill,
                   borderColor,
                   gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_HOST ? RAYWHITE : textColor,
                   false);
    DrawMenuButton(layout.joinModeButton, loc("Join"),
                   joinMode ? accentColor : sectionFill,
                   borderColor,
                   joinMode ? RAYWHITE : textColor,
                   false);
    if (!joinMode)
    {
        DrawMenuButton(layout.localColorButton, localColorLabel, sectionFill, borderColor, textColor, false);
        DrawMenuButton(layout.remoteColorButton, remoteColorLabel, sectionFill, borderColor, textColor, false);
    }
    else
    {
        DrawUiText(loc("Colors are assigned by the host."), layout.panel.x + 26.0f, layout.localColorButton.y + 12.0f, 17, bodyColor);
        DrawUiText(loc("You will receive your seat after connecting."), layout.panel.x + 26.0f, layout.localColorButton.y + 34.0f, 17, bodyColor);
    }

    if (joinMode)
    {
        DrawUiText(loc("For another machine, use host LAN IP (not 127.0.0.1)."), layout.panel.x + 26.0f, layout.hostAddressField.y - 44.0f, 15, bodyColor);
        DrawTextField(layout.hostAddressField, loc("Host Address"),
                      gMainMenuMultiplayerHostAddress,
                      "127.0.0.1",
                      gMainMenuMultiplayerField == MAIN_MENU_MULTIPLAYER_FIELD_HOST_ADDRESS,
                      darkTheme ? (Color){51, 63, 79, 255} : (Color){236, 229, 210, 255},
                      borderColor,
                      textColor,
                      bodyColor);
    }
    else
    {
        DrawMenuButton(layout.aiDifficultyButton, difficultyLabel, sectionFill, borderColor, textColor, false);
    }

    DrawTextField(layout.portField, loc("Port"),
                  gMainMenuMultiplayerPortText,
                  "24680",
                  gMainMenuMultiplayerField == MAIN_MENU_MULTIPLAYER_FIELD_PORT,
                  darkTheme ? (Color){51, 63, 79, 255} : (Color){236, 229, 210, 255},
                  borderColor,
                  textColor,
                  bodyColor);

    DrawUiText(loc("Click a field and type."), layout.panel.x + 26.0f, layout.confirmButton.y - 58.0f, 16, bodyColor);
    if (gMainMenuMultiplayerError[0] != '\0')
    {
        DrawUiText(gMainMenuMultiplayerError, layout.panel.x + 26.0f, layout.confirmButton.y - 34.0f, 16, (Color){171, 82, 54, 255});
    }

    DrawMenuButton(layout.confirmButton,
                   joinMode ? loc("Join Match") : loc("Open Lobby"),
                   accentColor,
                   borderColor,
                   RAYWHITE,
                   true);
    DrawMenuButton(layout.cancelButton, loc("Cancel"), sectionFill, borderColor, textColor, false);
    DrawUiText("x", layout.closeButton.x + 6.0f, layout.closeButton.y + 1.0f, 22, bodyColor);
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

static void DrawStatisticsRow(Rectangle bounds, float y, const char *label, const char *value, Color labelColor, Color valueColor)
{
    const int valueWidth = MeasureUiText(value, 18);

    DrawUiText(label, bounds.x + 18.0f, y, 18, labelColor);
    DrawUiText(value, bounds.x + bounds.width - 18.0f - valueWidth, y, 18, valueColor);
}

static void DrawTextField(Rectangle bounds, const char *label, const char *value, const char *placeholder,
                          bool active, Color fill, Color border, Color textColor, Color labelColor)
{
    char display[96];
    const bool showCaret = active && ((int)(GetTime() * 2.0) % 2 == 0);
    const bool hasValue = value != NULL && value[0] != '\0';
    const char *shownText = hasValue ? value : placeholder;
    const Color shownColor = hasValue ? textColor : Fade(labelColor, 0.80f);

    display[0] = '\0';
    if (showCaret)
    {
        snprintf(display, sizeof(display), "%s|", hasValue ? value : "");
        shownText = display;
    }

    DrawUiText(label, bounds.x, bounds.y - 22.0f, 16, labelColor);
    DrawRectangleRounded((Rectangle){bounds.x + 4.0f, bounds.y + 6.0f, bounds.width, bounds.height}, 0.16f, 8, Fade(BLACK, 0.12f));
    DrawRectangleRounded(bounds, 0.16f, 8, active ? ColorBrightness(fill, 0.05f) : fill);
    DrawRectangleLinesEx(bounds, 2.0f, active ? ColorBrightness(border, 0.12f) : border);
    DrawUiText(shownText, bounds.x + 12.0f, bounds.y + bounds.height * 0.5f - 10.0f, 20, showCaret ? textColor : shownColor);
}

static void FormatElapsedDuration(unsigned long long totalSeconds, char *buffer, size_t bufferSize)
{
    const unsigned long long hours = totalSeconds / 3600ULL;
    const unsigned long long minutes = (totalSeconds % 3600ULL) / 60ULL;
    const unsigned long long seconds = totalSeconds % 60ULL;

    if (buffer == NULL || bufferSize == 0)
    {
        return;
    }

    if (hours > 0ULL)
    {
        snprintf(buffer, bufferSize, "%llu:%02llu:%02llu", hours, minutes, seconds);
        return;
    }

    snprintf(buffer, bufferSize, "%02llu:%02llu", minutes, seconds);
}

static const char *PlayerName(enum PlayerType player)
{
    return locPlayerName(player);
}

static void NormalizeMultiplayerSelection(void)
{
    if (gMainMenuMultiplayerLocalColor < PLAYER_RED || gMainMenuMultiplayerLocalColor > PLAYER_BLACK)
    {
        gMainMenuMultiplayerLocalColor = PLAYER_RED;
    }
    if (gMainMenuMultiplayerRemoteColor < PLAYER_RED || gMainMenuMultiplayerRemoteColor > PLAYER_BLACK)
    {
        gMainMenuMultiplayerRemoteColor = PLAYER_BLUE;
    }
    if (gMainMenuMultiplayerLocalColor == gMainMenuMultiplayerRemoteColor)
    {
        gMainMenuMultiplayerRemoteColor = NextPlayerColor(gMainMenuMultiplayerRemoteColor, gMainMenuMultiplayerLocalColor);
    }
    if (gMainMenuMultiplayerAiDifficulty < AI_DIFFICULTY_EASY || gMainMenuMultiplayerAiDifficulty > AI_DIFFICULTY_HARD)
    {
        gMainMenuMultiplayerAiDifficulty = AI_DIFFICULTY_MEDIUM;
    }
}

static enum PlayerType NextPlayerColor(enum PlayerType player, enum PlayerType excluded)
{
    for (int offset = 1; offset <= MAX_PLAYERS; offset++)
    {
        const enum PlayerType candidate = (enum PlayerType)(((int)player + offset) % MAX_PLAYERS);
        if (candidate != excluded)
        {
            return candidate;
        }
    }

    return PLAYER_RED;
}

static void CycleMultiplayerLocalColor(void)
{
    gMainMenuMultiplayerLocalColor = NextPlayerColor(gMainMenuMultiplayerLocalColor, PLAYER_NONE);
    NormalizeMultiplayerSelection();
}

static void CycleMultiplayerRemoteColor(void)
{
    gMainMenuMultiplayerRemoteColor = NextPlayerColor(gMainMenuMultiplayerRemoteColor, gMainMenuMultiplayerLocalColor);
    NormalizeMultiplayerSelection();
}

static void CycleMultiplayerAiDifficulty(void)
{
    gMainMenuMultiplayerAiDifficulty = (enum AiDifficulty)(((int)gMainMenuMultiplayerAiDifficulty + 1) % 3);
}

static void HandleMultiplayerKeyboardInput(void)
{
    char *activeBuffer = NULL;
    size_t activeBufferSize = 0u;
    int codepoint = 0;

    if (!gMainMenuMultiplayerOpen)
    {
        return;
    }

    if (IsKeyPressed(KEY_TAB))
    {
        if (gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_JOIN)
        {
            gMainMenuMultiplayerField = gMainMenuMultiplayerField == MAIN_MENU_MULTIPLAYER_FIELD_HOST_ADDRESS
                                            ? MAIN_MENU_MULTIPLAYER_FIELD_PORT
                                            : MAIN_MENU_MULTIPLAYER_FIELD_HOST_ADDRESS;
        }
        else
        {
            gMainMenuMultiplayerField = MAIN_MENU_MULTIPLAYER_FIELD_PORT;
        }
    }

    if (gMainMenuMultiplayerField == MAIN_MENU_MULTIPLAYER_FIELD_HOST_ADDRESS)
    {
        activeBuffer = gMainMenuMultiplayerHostAddress;
        activeBufferSize = sizeof(gMainMenuMultiplayerHostAddress);
    }
    else if (gMainMenuMultiplayerField == MAIN_MENU_MULTIPLAYER_FIELD_PORT)
    {
        activeBuffer = gMainMenuMultiplayerPortText;
        activeBufferSize = sizeof(gMainMenuMultiplayerPortText);
    }

    if (activeBuffer == NULL)
    {
        return;
    }

    if (IsKeyPressed(KEY_BACKSPACE))
    {
        RemoveMultiplayerFieldChar(activeBuffer);
        gMainMenuMultiplayerError[0] = '\0';
    }

    codepoint = GetCharPressed();
    while (codepoint > 0)
    {
        AppendMultiplayerFieldChar(activeBuffer, activeBufferSize, codepoint);
        codepoint = GetCharPressed();
    }
}

static void AppendMultiplayerFieldChar(char *buffer, size_t bufferSize, int codepoint)
{
    const size_t length = buffer != NULL ? strlen(buffer) : 0u;

    if (buffer == NULL || length + 1u >= bufferSize)
    {
        return;
    }

    if (gMainMenuMultiplayerField == MAIN_MENU_MULTIPLAYER_FIELD_PORT)
    {
        if (codepoint >= '0' && codepoint <= '9')
        {
            buffer[length] = (char)codepoint;
            buffer[length + 1u] = '\0';
            gMainMenuMultiplayerError[0] = '\0';
        }
        return;
    }

    if ((codepoint >= '0' && codepoint <= '9') ||
        (codepoint >= 'A' && codepoint <= 'Z') ||
        (codepoint >= 'a' && codepoint <= 'z') ||
        codepoint == '.' || codepoint == '-' || codepoint == ':')
    {
        buffer[length] = (char)codepoint;
        buffer[length + 1u] = '\0';
        gMainMenuMultiplayerError[0] = '\0';
    }
}

static void RemoveMultiplayerFieldChar(char *buffer)
{
    size_t length = 0u;

    if (buffer == NULL)
    {
        return;
    }

    length = strlen(buffer);
    if (length > 0u)
    {
        buffer[length - 1u] = '\0';
    }
}

static bool ValidateMultiplayerConfig(char *message, size_t messageSize)
{
    char *end = NULL;
    long parsedPort = 0L;

    NormalizeMultiplayerSelection();

    if (gMainMenuMultiplayerLocalColor == gMainMenuMultiplayerRemoteColor)
    {
        snprintf(message, messageSize, "%s", loc("Colors must differ."));
        return false;
    }

    parsedPort = strtol(gMainMenuMultiplayerPortText, &end, 10);
    if (gMainMenuMultiplayerPortText[0] == '\0' ||
        end == NULL ||
        *end != '\0' ||
        parsedPort <= 0L ||
        parsedPort > 65535L)
    {
        snprintf(message, messageSize, "%s", loc("Port must be between 1 and 65535."));
        return false;
    }

    if (gMainMenuMultiplayerMode == MAIN_MENU_MULTIPLAYER_JOIN && gMainMenuMultiplayerHostAddress[0] == '\0')
    {
        snprintf(message, messageSize, "%s", loc("Host address is required."));
        return false;
    }

    if (message != NULL && messageSize > 0u)
    {
        message[0] = '\0';
    }
    return true;
}
