#include <raylib.h>
#include <stdlib.h>
#include <time.h>
#include "ai_controller.h"
#include "game_logic.h"
#include "main_menu.h"
#include "map.h"
#include "renderer.h"
#include "settings_store.h"
#include "ui_state.h"

enum AppScreen
{
    APP_SCREEN_MAIN_MENU,
    APP_SCREEN_GAME
};

static void DrawSceneBackground(void);
static void ConfigureMatch(struct Map *map, bool aiOpponentsEnabled, enum AiDifficulty difficulty, enum PlayerType humanColor);
static void LoadPersistedSettings(void);

int main(void)
{
    InitWindow(1440, 980, "Catan Map");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    srand((unsigned)time(NULL));

    LoadRendererAssets();
    initUiState();
    LoadPersistedSettings();

    struct Map map;
    setupMap(&map);
    aiConfigureHotseatMatch(&map);
    aiResetController();
    enum AppScreen appScreen = APP_SCREEN_MAIN_MENU;
    bool shouldQuit = false;
    bool aiOpponentsEnabled = false;
    enum AiDifficulty aiDifficulty = MainMenuGetAiDifficulty();
    enum PlayerType humanColor = MainMenuGetHumanColor();

    while (!WindowShouldClose() && !shouldQuit)
    {
        if (appScreen == APP_SCREEN_MAIN_MENU)
        {
            switch (HandleMainMenuInput())
            {
            case MAIN_MENU_ACTION_START_GAME:
                setupMap(&map);
                aiOpponentsEnabled = false;
                aiDifficulty = MainMenuGetAiDifficulty();
                humanColor = MainMenuGetHumanColor();
                ConfigureMatch(&map, aiOpponentsEnabled, aiDifficulty, humanColor);
                aiResetController();
                uiResetForNewGame();
                appScreen = APP_SCREEN_GAME;
                break;
            case MAIN_MENU_ACTION_START_AI_GAME:
                setupMap(&map);
                aiOpponentsEnabled = true;
                aiDifficulty = MainMenuGetAiDifficulty();
                humanColor = MainMenuGetHumanColor();
                ConfigureMatch(&map, aiOpponentsEnabled, aiDifficulty, humanColor);
                aiResetController();
                uiResetForNewGame();
                appScreen = APP_SCREEN_GAME;
                break;
            case MAIN_MENU_ACTION_CYCLE_AI_DIFFICULTY:
                aiDifficulty = MainMenuGetAiDifficulty();
                break;
            case MAIN_MENU_ACTION_CYCLE_HUMAN_COLOR:
                humanColor = MainMenuGetHumanColor();
                break;
            case MAIN_MENU_ACTION_TOGGLE_THEME:
                uiSetTheme(uiGetTheme() == UI_THEME_DARK ? UI_THEME_LIGHT : UI_THEME_DARK);
                settingsStoreSaveCurrent();
                break;
            case MAIN_MENU_ACTION_QUIT:
                shouldQuit = true;
                continue;
            case MAIN_MENU_ACTION_NONE:
            default:
                break;
            }
        }
        else
        {
            updateUiState(&map);
            const bool aiControlledDecision = aiControlsActiveDecision(&map);
            if (aiControlledDecision)
            {
                handleUiGlobalInput();
                HandleMapInput(&map);
                aiUpdateTurn(&map);
            }
            else
            {
                handleUiGlobalInput();

                if ((IsKeyPressed(KEY_T) || IsKeyPressed(KEY_TWO)) &&
                    map.phase == GAME_PHASE_PLAY &&
                    !gameHasFreeRoadPlacements(&map) &&
                    !uiIsDevelopmentPurchaseConfirmOpen() &&
                    !uiIsDevelopmentPlayConfirmOpen())
                {
                    uiTogglePlayerTradeMenu();
                    uiSetTradeMenuOpen(false);
                }

                if ((IsKeyPressed(KEY_W) || IsKeyPressed(KEY_THREE)) &&
                    map.phase == GAME_PHASE_PLAY &&
                    !gameHasFreeRoadPlacements(&map) &&
                    !uiIsDevelopmentPurchaseConfirmOpen() &&
                    !uiIsDevelopmentPlayConfirmOpen())
                {
                    uiToggleTradeMenu();
                    uiSetPlayerTradeMenuOpen(false);
                }

                if ((IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ENTER)) &&
                    gameCanRollDice(&map) &&
                    !uiIsDevelopmentPurchaseConfirmOpen() &&
                    !uiIsDevelopmentPlayConfirmOpen() &&
                    !uiIsDiceRolling())
                {
                    uiStartDiceRollAnimation();
                }

                HandleMapInput(&map);
            }

            if (uiConsumeRestartGameRequest())
            {
                setupMap(&map);
                ConfigureMatch(&map, aiOpponentsEnabled, aiDifficulty, humanColor);
                aiResetController();
                uiResetForNewGame();
                continue;
            }
            if (uiConsumeReturnToMainMenuRequest())
            {
                aiResetController();
                uiResetForNewGame();
                appScreen = APP_SCREEN_MAIN_MENU;
            }
            if (uiConsumeQuitGameRequest())
            {
                break;
            }
        }

        BeginDrawing();
        DrawSceneBackground();
        if (appScreen == APP_SCREEN_MAIN_MENU)
        {
            DrawMainMenu();
        }
        else
        {
            DrawMap(&map);
        }

        EndDrawing();
    }

    UnloadRendererAssets();
    CloseWindow();
    return 0;
}

static void ConfigureMatch(struct Map *map, bool aiOpponentsEnabled, enum AiDifficulty difficulty, enum PlayerType humanColor)
{
    if (aiOpponentsEnabled)
    {
        aiConfigureAIMatch(map, humanColor, difficulty);
        return;
    }

    aiConfigureHotseatMatch(map);
}

static void LoadPersistedSettings(void)
{
    struct PersistedSettings settings;

    settingsStoreLoadDefaults(&settings);
    settingsStoreLoad(&settings);

    uiSetTheme(settings.theme);
    uiSetAiSpeedSetting(settings.aiSpeed);
    MainMenuSetAiDifficulty(settings.aiDifficulty);
    MainMenuSetHumanColor(settings.humanColor);
}

static void DrawSceneBackground(void)
{
    if (uiGetTheme() == UI_THEME_DARK)
    {
        ClearBackground((Color){28, 36, 48, 255});
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                               (Color){44, 61, 83, 255},
                               (Color){34, 43, 38, 255});
        DrawCircleV((Vector2){160.0f, 120.0f}, 170.0f, Fade((Color){132, 163, 209, 255}, 0.20f));
        DrawCircleV((Vector2){GetScreenWidth() - 220.0f, 160.0f}, 240.0f, Fade((Color){93, 128, 117, 255}, 0.18f));
        DrawCircleV((Vector2){GetScreenWidth() * 0.5f, GetScreenHeight() + 120.0f}, 380.0f, Fade((Color){74, 98, 70, 255}, 0.22f));
        for (int i = 0; i < GetScreenWidth(); i += 96)
        {
            DrawLineEx(
                (Vector2){(float)i, (float)GetScreenHeight() - 110.0f},
                (Vector2){(float)i + 42.0f, (float)GetScreenHeight() - 38.0f},
                2.0f,
                Fade((Color){210, 227, 255, 255}, 0.04f));
        }
        return;
    }

    ClearBackground((Color){232, 242, 225, 255});
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                           (Color){196, 225, 255, 255},
                           (Color){241, 232, 205, 255});
    DrawCircleV((Vector2){140.0f, 120.0f}, 180.0f, Fade((Color){255, 248, 217, 255}, 0.65f));
    DrawCircleV((Vector2){GetScreenWidth() - 180.0f, 140.0f}, 220.0f, Fade((Color){214, 240, 214, 255}, 0.45f));
    DrawCircleV((Vector2){GetScreenWidth() * 0.5f, GetScreenHeight() + 120.0f}, 360.0f, Fade((Color){189, 214, 160, 255}, 0.30f));
    for (int i = 0; i < GetScreenWidth(); i += 96)
    {
        DrawLineEx(
            (Vector2){(float)i, (float)GetScreenHeight() - 110.0f},
            (Vector2){(float)i + 42.0f, (float)GetScreenHeight() - 38.0f},
            2.0f,
            Fade((Color){255, 255, 255, 255}, 0.08f));
    }
}
