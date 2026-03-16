#include "main_menu.h"

#include "ai_controller.h"
#include "localization.h"
#include "renderer_ui.h"
#include "settings_store.h"
#include "ui_state.h"

#include <raylib.h>
#include <stdio.h>

static Rectangle GetMainMenuPanelBounds(void);
static Rectangle GetStartButtonBounds(void);
static Rectangle GetAiStartButtonBounds(void);
static Rectangle GetStatisticsButtonBounds(void);
static Rectangle GetLanguageButtonBounds(void);
static Rectangle GetThemeButtonBounds(void);
static Rectangle GetQuitButtonBounds(void);
static Rectangle GetStartPopupBounds(void);
static Rectangle GetStartPopupColorButtonBounds(void);
static Rectangle GetStartPopupDifficultyButtonBounds(void);
static Rectangle GetStartPopupConfirmButtonBounds(void);
static Rectangle GetStartPopupCancelButtonBounds(void);
static Rectangle GetStatisticsPopupBounds(void);
static Rectangle GetStatisticsPopupCloseButtonBounds(void);
static void DrawMenuButton(Rectangle bounds, const char *label, Color fill, Color border, Color text, bool emphasized);
static void DrawStatisticsRow(Rectangle bounds, float y, const char *label, const char *value, Color labelColor, Color valueColor);
static void DrawStartPopup(void);
static void DrawStatisticsPopup(void);
static void FormatElapsedDuration(unsigned long long totalSeconds, char *buffer, size_t bufferSize);
static const char *PlayerName(enum PlayerType player);

static enum AiDifficulty gMainMenuAiDifficulty = AI_DIFFICULTY_MEDIUM;
static enum PlayerType gMainMenuHumanColor = PLAYER_RED;
static enum MainMenuAction gMainMenuPopupStartAction = MAIN_MENU_ACTION_NONE;
static bool gMainMenuStatisticsOpen = false;

void DrawMainMenu(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    const Rectangle startButton = GetStartButtonBounds();
    const Rectangle aiStartButton = GetAiStartButtonBounds();
    const Rectangle statisticsButton = GetStatisticsButtonBounds();
    const Rectangle languageButton = GetLanguageButtonBounds();
    const Rectangle themeButton = GetThemeButtonBounds();
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
    const char *themeValue = uiGetTheme() == UI_THEME_DARK ? loc("Dark") : loc("Light");
    const char *themeLabel = TextFormat(loc("Theme: %s"), themeValue);
    const char *languageLabel = TextFormat(loc("Language: %s"), locLanguageDisplayName(locGetLanguage()));
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
    DrawMenuButton(statisticsButton, loc("Statistics"), darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, titleColor, false);
    DrawMenuButton(languageButton, languageLabel, darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, titleColor, false);
    DrawMenuButton(themeButton, themeLabel, darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255}, borderColor, titleColor, false);
    DrawMenuButton(quitButton, loc("Quit"), darkTheme ? (Color){82, 54, 54, 255} : (Color){231, 215, 204, 255}, borderColor, titleColor, false);

    DrawStartPopup();
    DrawStatisticsPopup();
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
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetAiStartButtonBounds()))
    {
        gMainMenuPopupStartAction = MAIN_MENU_ACTION_START_AI_GAME;
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetStatisticsButtonBounds()))
    {
        gMainMenuStatisticsOpen = true;
        return MAIN_MENU_ACTION_NONE;
    }
    if (CheckCollisionPointRec(mouse, GetLanguageButtonBounds()))
    {
        locSetLanguage((enum UiLanguage)((locGetLanguage() + 1) % UI_LANGUAGE_COUNT));
        settingsStoreSaveCurrent();
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
    const float panelHeight = 578.0f;
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

static Rectangle GetStatisticsButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 346.0f, panel.width - 68.0f, 46.0f};
}

static Rectangle GetThemeButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 460.0f, panel.width - 68.0f, 46.0f};
}

static Rectangle GetQuitButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 518.0f, panel.width - 68.0f, 40.0f};
}

static Rectangle GetLanguageButtonBounds(void)
{
    const Rectangle panel = GetMainMenuPanelBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + 404.0f, panel.width - 68.0f, 46.0f};
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
