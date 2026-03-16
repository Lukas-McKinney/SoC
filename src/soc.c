#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ai_controller.h"
#include "game_action.h"
#include "game_logic.h"
#include "netplay.h"
#include "localization.h"
#include "main_menu.h"
#include "map.h"
#include "match_session.h"
#include "renderer.h"
#include "settings_store.h"
#include "ui_state.h"

enum AppScreen
{
    APP_SCREEN_MAIN_MENU,
    APP_SCREEN_GAME
};

struct LaunchOptions
{
    bool hostMode;
    bool clientMode;
    unsigned short port;
    enum PlayerType localPlayer;
    enum PlayerType remotePlayer;
    enum AiDifficulty aiDifficulty;
    char hostAddress[64];
};

static void DrawSceneBackground(void);
static void ConfigureMatch(struct MatchSession *session, bool aiOpponentsEnabled, enum AiDifficulty difficulty, enum PlayerType humanColor);
static void ConfigurePrivateMatch(struct MatchSession *session,
                                  enum PlayerType hostPlayer,
                                  enum PlayerType remotePlayer,
                                  enum AiDifficulty difficulty,
                                  bool localIsHost);
static void LoadPersistedSettings(void);
static void InitLaunchOptions(struct LaunchOptions *options);
static void PrintUsage(const char *programName);
static bool ParsePlayerType(const char *value, enum PlayerType *player);
static bool ParseAiDifficulty(const char *value, enum AiDifficulty *difficulty);
static bool ParseLaunchOptions(int argc, char **argv, struct LaunchOptions *options);

int main(int argc, char **argv)
{
    struct LaunchOptions launchOptions;
    InitWindow(1440, 980, "Catan Map");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    srand((unsigned)time(NULL));

    LoadRendererAssets();
    initUiState();
    LoadPersistedSettings();
    InitLaunchOptions(&launchOptions);
    launchOptions.aiDifficulty = MainMenuGetAiDifficulty();
    if (!ParseLaunchOptions(argc, argv, &launchOptions))
    {
        PrintUsage(argc > 0 ? argv[0] : "settlers");
        settingsStoreSaveCurrent();
        UnloadRendererAssets();
        CloseWindow();
        return 1;
    }

    struct MatchSession session;
    matchSessionInit(&session);
    matchSessionSetActive(&session);
    setupMap(&session.map);
    aiConfigureHotseatMatch(&session.map);
    matchSessionConfigureHotseat(&session);
    matchSessionRefreshStateHash(&session);
    aiResetController();
    enum AppScreen appScreen = APP_SCREEN_MAIN_MENU;
    bool shouldQuit = false;
    bool aiOpponentsEnabled = false;
    enum AiDifficulty aiDifficulty = MainMenuGetAiDifficulty();
    enum PlayerType humanColor = MainMenuGetHumanColor();

    if (launchOptions.hostMode || launchOptions.clientMode)
    {
        const bool localIsHost = launchOptions.hostMode;
        const enum PlayerType hostPlayer = localIsHost ? launchOptions.localPlayer : launchOptions.remotePlayer;
        const enum PlayerType remotePlayer = localIsHost ? launchOptions.remotePlayer : launchOptions.localPlayer;

        setupMap(&session.map);
        ConfigurePrivateMatch(&session, hostPlayer, remotePlayer, launchOptions.aiDifficulty, localIsHost);
        aiResetController();
        uiResetForNewGame();
        uiBeginMatch();
        if (launchOptions.hostMode)
        {
            if (!matchSessionOpenPrivateHost(&session, launchOptions.port))
            {
                fprintf(stderr, "failed to host private match: %s\n", matchSessionGetConnectionError(&session));
                settingsStoreSaveCurrent();
                UnloadRendererAssets();
                CloseWindow();
                return 1;
            }
        }
        else if (!matchSessionOpenPrivateClient(&session, launchOptions.hostAddress, launchOptions.port))
        {
            fprintf(stderr, "failed to join private match: %s\n", matchSessionGetConnectionError(&session));
            settingsStoreSaveCurrent();
            UnloadRendererAssets();
            CloseWindow();
            return 1;
        }

        aiDifficulty = launchOptions.aiDifficulty;
        humanColor = launchOptions.localPlayer;
        appScreen = APP_SCREEN_GAME;
    }

    while (!WindowShouldClose() && !shouldQuit)
    {
        SetWindowTitle(loc("Catan Map"));
        if (appScreen == APP_SCREEN_MAIN_MENU)
        {
            switch (HandleMainMenuInput())
            {
            case MAIN_MENU_ACTION_START_GAME:
                setupMap(&session.map);
                aiOpponentsEnabled = false;
                aiDifficulty = MainMenuGetAiDifficulty();
                humanColor = MainMenuGetHumanColor();
                ConfigureMatch(&session, aiOpponentsEnabled, aiDifficulty, humanColor);
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
                appScreen = APP_SCREEN_GAME;
                break;
            case MAIN_MENU_ACTION_START_AI_GAME:
                setupMap(&session.map);
                aiOpponentsEnabled = true;
                aiDifficulty = MainMenuGetAiDifficulty();
                humanColor = MainMenuGetHumanColor();
                ConfigureMatch(&session, aiOpponentsEnabled, aiDifficulty, humanColor);
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
                appScreen = APP_SCREEN_GAME;
                break;
            case MAIN_MENU_ACTION_START_PRIVATE_HOST:
                setupMap(&session.map);
                aiOpponentsEnabled = false;
                aiDifficulty = MainMenuGetMultiplayerAiDifficulty();
                humanColor = MainMenuGetMultiplayerLocalColor();
                ConfigurePrivateMatch(&session,
                                      MainMenuGetMultiplayerLocalColor(),
                                      MainMenuGetMultiplayerRemoteColor(),
                                      aiDifficulty,
                                      true);
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
                if (!matchSessionOpenPrivateHost(&session, MainMenuGetMultiplayerPort()))
                {
                    MainMenuSetMultiplayerError(matchSessionGetConnectionError(&session));
                    MainMenuSetMultiplayerOpen(true);
                    appScreen = APP_SCREEN_MAIN_MENU;
                    break;
                }

                MainMenuSetMultiplayerOpen(false);
                appScreen = APP_SCREEN_GAME;
                break;
            case MAIN_MENU_ACTION_START_PRIVATE_JOIN:
                setupMap(&session.map);
                aiOpponentsEnabled = false;
                aiDifficulty = MainMenuGetMultiplayerAiDifficulty();
                humanColor = MainMenuGetMultiplayerLocalColor();
                ConfigurePrivateMatch(&session,
                                      MainMenuGetMultiplayerRemoteColor(),
                                      MainMenuGetMultiplayerLocalColor(),
                                      aiDifficulty,
                                      false);
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
                if (!matchSessionOpenPrivateClient(&session,
                                                   MainMenuGetMultiplayerHostAddress(),
                                                   MainMenuGetMultiplayerPort()))
                {
                    MainMenuSetMultiplayerError(matchSessionGetConnectionError(&session));
                    MainMenuSetMultiplayerOpen(true);
                    appScreen = APP_SCREEN_MAIN_MENU;
                    break;
                }

                MainMenuSetMultiplayerOpen(false);
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
            matchSessionUpdate(&session);
            updateUiState(&session.map);

            const bool aiControlledDecision = matchSessionShouldRunAi(&session) && aiControlsActiveDecision(&session.map);
            const bool localControlledDecision = matchSessionLocalCanActOnCurrentDecision(&session);
            if (aiControlledDecision)
            {
                handleUiGlobalInput();
                HandleMapInput(&session);
                aiUpdateTurn(&session.map);
            }
            else if (localControlledDecision)
            {
                handleUiGlobalInput();

                if ((IsKeyPressed(KEY_T) || IsKeyPressed(KEY_TWO)) &&
                    session.map.phase == GAME_PHASE_PLAY &&
                    !gameHasFreeRoadPlacements(&session.map) &&
                    !uiIsDevelopmentPurchaseConfirmOpen() &&
                    !uiIsDevelopmentPlayConfirmOpen())
                {
                    uiTogglePlayerTradeMenu();
                    uiSetTradeMenuOpen(false);
                }

                if ((IsKeyPressed(KEY_W) || IsKeyPressed(KEY_THREE)) &&
                    session.map.phase == GAME_PHASE_PLAY &&
                    !gameHasFreeRoadPlacements(&session.map) &&
                    !uiIsDevelopmentPurchaseConfirmOpen() &&
                    !uiIsDevelopmentPlayConfirmOpen())
                {
                    uiToggleTradeMenu();
                    uiSetPlayerTradeMenuOpen(false);
                }

                if ((IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ENTER)) &&
                    gameCanRollDice(&session.map) &&
                    !uiIsDevelopmentPurchaseConfirmOpen() &&
                    !uiIsDevelopmentPlayConfirmOpen() &&
                    !uiIsDiceRolling())
                {
                    if (matchSessionShouldAnimateLocalRoll(&session))
                    {
                        uiStartDiceRollAnimation();
                    }
                    else
                    {
                        matchSessionSubmitAction(&session,
                                                 &(struct GameAction){.type = GAME_ACTION_ROLL_DICE},
                                                 NULL,
                                                 NULL);
                    }
                }

                HandleMapInput(&session);
            }
            else
            {
                handleUiGlobalInput();
                HandleMapInput(&session);
            }

            if (uiConsumeRestartGameRequest())
            {
                if (matchSessionIsNetplay(&session))
                {
                    if (!matchSessionIsHost(&session))
                    {
                        uiShowCenteredWarning(loc("Only the host can restart private multiplayer."));
                        continue;
                    }

                    setupMap(&session.map);
                    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
                    {
                        const enum MatchSeatAuthority authority = matchSessionGetSeatAuthority(&session, (enum PlayerType)player);
                        if (authority == MATCH_SEAT_AI)
                        {
                            session.map.players[player].controlMode = PLAYER_CONTROL_AI;
                            session.map.players[player].aiDifficulty = aiDifficulty;
                        }
                        else
                        {
                            session.map.players[player].controlMode = PLAYER_CONTROL_HUMAN;
                            session.map.players[player].aiDifficulty = AI_DIFFICULTY_EASY;
                        }
                    }

                    aiResetController();
                    uiResetForNewGame();
                    uiBeginMatch();
                    matchSessionRefreshStateHash(&session);
                    matchSessionBroadcastState(&session);
                    settingsStoreSaveCurrent();
                    continue;
                }

                setupMap(&session.map);
                ConfigureMatch(&session, aiOpponentsEnabled, aiDifficulty, humanColor);
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
                settingsStoreSaveCurrent();
                continue;
            }
            if (uiConsumeReturnToMainMenuRequest())
            {
                matchSessionShutdown(&session);
                aiResetController();
                uiResetForNewGame();
                settingsStoreSaveCurrent();
                appScreen = APP_SCREEN_MAIN_MENU;
            }
            if (uiConsumeQuitGameRequest())
            {
                break;
            }

            matchSessionRefreshStateHash(&session);
        }

        BeginDrawing();
        DrawSceneBackground();
        if (appScreen == APP_SCREEN_MAIN_MENU)
        {
            DrawMainMenu();
        }
        else
        {
            DrawMap(&session.map);
        }

        EndDrawing();
    }

    settingsStoreSaveCurrent();
    matchSessionShutdown(&session);
    UnloadRendererAssets();
    CloseWindow();
    return 0;
}

static void ConfigureMatch(struct MatchSession *session, bool aiOpponentsEnabled, enum AiDifficulty difficulty, enum PlayerType humanColor)
{
    if (session == NULL)
    {
        return;
    }

    if (aiOpponentsEnabled)
    {
        aiConfigureAIMatch(&session->map, humanColor, difficulty);
        matchSessionConfigureSinglePlayer(session, humanColor);
        return;
    }

    aiConfigureHotseatMatch(&session->map);
    matchSessionConfigureHotseat(session);
}

static void ConfigurePrivateMatch(struct MatchSession *session,
                                  enum PlayerType hostPlayer,
                                  enum PlayerType remotePlayer,
                                  enum AiDifficulty difficulty,
                                  bool localIsHost)
{
    if (session == NULL)
    {
        return;
    }

    aiConfigureHotseatMatch(&session->map);
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (player == hostPlayer || player == remotePlayer)
        {
            session->map.players[player].controlMode = PLAYER_CONTROL_HUMAN;
            session->map.players[player].aiDifficulty = AI_DIFFICULTY_EASY;
        }
        else
        {
            session->map.players[player].controlMode = PLAYER_CONTROL_AI;
            session->map.players[player].aiDifficulty = difficulty;
        }
    }

    if (localIsHost)
    {
        matchSessionConfigurePrivateHost(session, hostPlayer);
        matchSessionSetSeatAuthority(session, remotePlayer, MATCH_SEAT_REMOTE);
    }
    else
    {
        /* Client seat is authoritative from host hello; avoid guessing via menu color. */
        matchSessionConfigurePrivateClient(session, PLAYER_NONE);
        matchSessionSetSeatAuthority(session, hostPlayer, MATCH_SEAT_REMOTE);
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (player != hostPlayer && player != remotePlayer)
        {
            matchSessionSetSeatAuthority(session, (enum PlayerType)player, MATCH_SEAT_AI);
        }
    }
}

static void LoadPersistedSettings(void)
{
    struct PersistedSettings settings;

    settingsStoreLoadDefaults(&settings);
    settingsStoreLoad(&settings);

    uiSetTheme(settings.theme);
    locSetLanguage(settings.language);
    uiSetAiSpeedSetting(settings.aiSpeed);
    MainMenuSetAiDifficulty(settings.aiDifficulty);
    MainMenuSetHumanColor(settings.humanColor);
    MainMenuSetMultiplayerHostAddress(settings.multiplayerHostAddress);
    MainMenuSetMultiplayerPort(settings.multiplayerPort);
    uiSetPersistedTotalPlaytimeSeconds(settings.totalPlaytimeSeconds);
    uiSetPersistedMatchRecord(settings.totalWins, settings.totalLosses);
}

static void InitLaunchOptions(struct LaunchOptions *options)
{
    if (options == NULL)
    {
        return;
    }

    memset(options, 0, sizeof(*options));
    options->port = NETPLAY_DEFAULT_PORT;
    options->localPlayer = PLAYER_RED;
    options->remotePlayer = PLAYER_BLUE;
    options->aiDifficulty = AI_DIFFICULTY_MEDIUM;
    options->hostAddress[0] = '\0';
}

static void PrintUsage(const char *programName)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s\n", programName);
    fprintf(stderr, "  %s --host [--player red|blue|green|black] [--remote-player color] [--port number] [--ai-difficulty easy|medium|hard]\n", programName);
    fprintf(stderr, "  %s --join <ip-or-hostname> [--player red|blue|green|black] [--remote-player color] [--port number]\n", programName);
}

static bool ParsePlayerType(const char *value, enum PlayerType *player)
{
    if (value == NULL || player == NULL)
    {
        return false;
    }

    if (strcmp(value, "red") == 0)
    {
        *player = PLAYER_RED;
        return true;
    }
    if (strcmp(value, "blue") == 0)
    {
        *player = PLAYER_BLUE;
        return true;
    }
    if (strcmp(value, "green") == 0)
    {
        *player = PLAYER_GREEN;
        return true;
    }
    if (strcmp(value, "black") == 0)
    {
        *player = PLAYER_BLACK;
        return true;
    }

    return false;
}

static bool ParseAiDifficulty(const char *value, enum AiDifficulty *difficulty)
{
    if (value == NULL || difficulty == NULL)
    {
        return false;
    }

    if (strcmp(value, "easy") == 0)
    {
        *difficulty = AI_DIFFICULTY_EASY;
        return true;
    }
    if (strcmp(value, "medium") == 0)
    {
        *difficulty = AI_DIFFICULTY_MEDIUM;
        return true;
    }
    if (strcmp(value, "hard") == 0)
    {
        *difficulty = AI_DIFFICULTY_HARD;
        return true;
    }

    return false;
}

static bool ParseLaunchOptions(int argc, char **argv, struct LaunchOptions *options)
{
    bool localPlayerSpecified = false;
    bool remotePlayerSpecified = false;

    if (options == NULL)
    {
        return false;
    }

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        if (strcmp(arg, "--host") == 0)
        {
            options->hostMode = true;
        }
        else if (strcmp(arg, "--join") == 0)
        {
            if (i + 1 >= argc)
            {
                return false;
            }
            options->clientMode = true;
            snprintf(options->hostAddress, sizeof(options->hostAddress), "%s", argv[++i]);
        }
        else if (strcmp(arg, "--player") == 0 || strcmp(arg, "--local-player") == 0)
        {
            if (i + 1 >= argc || !ParsePlayerType(argv[++i], &options->localPlayer))
            {
                return false;
            }
            localPlayerSpecified = true;
        }
        else if (strcmp(arg, "--remote-player") == 0)
        {
            if (i + 1 >= argc || !ParsePlayerType(argv[++i], &options->remotePlayer))
            {
                return false;
            }
            remotePlayerSpecified = true;
        }
        else if (strcmp(arg, "--port") == 0)
        {
            long parsedPort = 0;
            if (i + 1 >= argc)
            {
                return false;
            }
            parsedPort = strtol(argv[++i], NULL, 10);
            if (parsedPort <= 0 || parsedPort > 65535)
            {
                return false;
            }
            options->port = (unsigned short)parsedPort;
        }
        else if (strcmp(arg, "--ai-difficulty") == 0)
        {
            if (i + 1 >= argc || !ParseAiDifficulty(argv[++i], &options->aiDifficulty))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    if (options->hostMode && options->clientMode)
    {
        return false;
    }

    if (options->clientMode && options->hostAddress[0] == '\0')
    {
        return false;
    }

    if (!options->hostMode && !options->clientMode)
    {
        return true;
    }

    if (!localPlayerSpecified)
    {
        options->localPlayer = options->hostMode ? PLAYER_RED : PLAYER_BLUE;
    }
    if (!remotePlayerSpecified)
    {
        options->remotePlayer = options->localPlayer == PLAYER_RED ? PLAYER_BLUE : PLAYER_RED;
    }

    return options->localPlayer != options->remotePlayer;
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
