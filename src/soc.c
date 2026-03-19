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
#include "renderer_ui.h"
#include "settings_store.h"
#include "ui_state.h"

enum AppScreen
{
    APP_SCREEN_MAIN_MENU,
    APP_SCREEN_LOCAL_LOBBY,
    APP_SCREEN_NETPLAY_LOBBY,
    APP_SCREEN_GAME
};

enum LocalLobbyAction
{
    LOCAL_LOBBY_ACTION_NONE,
    LOCAL_LOBBY_ACTION_RETURN_TO_MENU,
    LOCAL_LOBBY_ACTION_START_MATCH
};

enum NetplayLobbyAction
{
    NETPLAY_LOBBY_ACTION_NONE,
    NETPLAY_LOBBY_ACTION_RETURN_TO_MENU,
    NETPLAY_LOBBY_ACTION_START_MATCH
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
static void DrawLocalLobby(const struct MatchSession *session);
static enum LocalLobbyAction HandleLocalLobbyInput(struct MatchSession *session,
                                                   struct MainMenuLobbyConfig *config,
                                                   enum AiDifficulty *aiDifficulty);
static void DrawNetplayLobby(const struct MatchSession *session);
static enum NetplayLobbyAction HandleNetplayLobbyInput(struct MatchSession *session);
static void DrawLobbyButton(Rectangle bounds, const char *label, Color fill, Color border, Color text, bool emphasized, bool enabled);
static const char *NetplayLobbyStatusLabel(const struct MatchSession *session);
static const char *NetplayLobbySeatRoleLabel(const struct MatchSession *session, enum PlayerType player);
static void DrawProfileBadge(void);
static int LocalLobbyCountHumanSeats(const struct MainMenuLobbyConfig *config);
static enum PlayerType LocalLobbyFirstHumanSeat(const struct MainMenuLobbyConfig *config);
static void ToggleLocalLobbySeat(struct MatchSession *session,
                                 struct MainMenuLobbyConfig *config,
                                 enum PlayerType player,
                                 enum AiDifficulty *aiDifficulty);
static void CycleLocalLobbyDifficulty(struct MatchSession *session,
                                      struct MainMenuLobbyConfig *config,
                                      enum AiDifficulty *aiDifficulty);
static void ConfigureLocalMatch(struct MatchSession *session, const struct MainMenuLobbyConfig *config);
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
static bool gNetplayLobbyRevealEndpoint = false;

/* Keep launch/bootstrap helpers file-local while keeping the main loop readable. */
#include "soc_launch.inc"

int main(int argc, char **argv)
{
    struct LaunchOptions launchOptions;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1440, 980, "Catan Map");
    SetWindowMinSize(1024, 720);
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
    bool hasLocalLobbyConfig = false;
    struct MainMenuLobbyConfig localLobbyConfig;
    enum AiDifficulty aiDifficulty = MainMenuGetAiDifficulty();

    if (launchOptions.hostMode || launchOptions.clientMode)
    {
        const bool localIsHost = launchOptions.hostMode;
        const enum PlayerType hostPlayer = localIsHost ? launchOptions.localPlayer : launchOptions.remotePlayer;
        const enum PlayerType remotePlayer = localIsHost ? launchOptions.remotePlayer : launchOptions.localPlayer;

        setupMap(&session.map);
        ConfigurePrivateMatch(&session, hostPlayer, remotePlayer, launchOptions.aiDifficulty, localIsHost);
        aiResetController();
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
        gNetplayLobbyRevealEndpoint = false;
        appScreen = APP_SCREEN_NETPLAY_LOBBY;
    }

    while (!WindowShouldClose() && !shouldQuit)
    {
        SetWindowTitle(loc("Catan Map"));
        if (appScreen == APP_SCREEN_MAIN_MENU)
        {
            switch (HandleMainMenuInput())
            {
            case MAIN_MENU_ACTION_START_GAME:
            case MAIN_MENU_ACTION_START_AI_GAME:
                setupMap(&session.map);
                MainMenuGetLobbyConfig(&localLobbyConfig);
                hasLocalLobbyConfig = true;
                aiDifficulty = localLobbyConfig.aiDifficulty;
                ConfigureLocalMatch(&session, &localLobbyConfig);
                aiResetController();
                appScreen = APP_SCREEN_LOCAL_LOBBY;
                break;
            case MAIN_MENU_ACTION_START_PRIVATE_HOST:
                setupMap(&session.map);
                aiDifficulty = MainMenuGetMultiplayerAiDifficulty();
                ConfigurePrivateMatch(&session,
                                      MainMenuGetMultiplayerLocalColor(),
                                      MainMenuGetMultiplayerRemoteColor(),
                                      aiDifficulty,
                                      true);
                aiResetController();
                if (!matchSessionOpenPrivateHost(&session, MainMenuGetMultiplayerPort()))
                {
                    MainMenuSetMultiplayerError(matchSessionGetConnectionError(&session));
                    MainMenuSetMultiplayerOpen(true);
                    appScreen = APP_SCREEN_MAIN_MENU;
                    break;
                }

                MainMenuSetMultiplayerOpen(false);
                gNetplayLobbyRevealEndpoint = false;
                appScreen = APP_SCREEN_NETPLAY_LOBBY;
                break;
            case MAIN_MENU_ACTION_START_PRIVATE_JOIN:
                setupMap(&session.map);
                aiDifficulty = MainMenuGetMultiplayerAiDifficulty();
                ConfigurePrivateMatch(&session,
                                      MainMenuGetMultiplayerRemoteColor(),
                                      MainMenuGetMultiplayerLocalColor(),
                                      aiDifficulty,
                                      false);
                aiResetController();
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
                gNetplayLobbyRevealEndpoint = false;
                appScreen = APP_SCREEN_NETPLAY_LOBBY;
                break;
            case MAIN_MENU_ACTION_CYCLE_AI_DIFFICULTY:
                aiDifficulty = MainMenuGetAiDifficulty();
                break;
            case MAIN_MENU_ACTION_CYCLE_HUMAN_COLOR:
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
        else if (appScreen == APP_SCREEN_LOCAL_LOBBY)
        {
            switch (HandleLocalLobbyInput(&session, &localLobbyConfig, &aiDifficulty))
            {
            case LOCAL_LOBBY_ACTION_RETURN_TO_MENU:
                matchSessionShutdown(&session);
                setupMap(&session.map);
                matchSessionConfigureHotseat(&session);
                aiResetController();
                uiResetForNewGame();
                settingsStoreSaveCurrent();
                appScreen = APP_SCREEN_MAIN_MENU;
                break;
            case LOCAL_LOBBY_ACTION_START_MATCH:
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
                appScreen = APP_SCREEN_GAME;
                break;
            case LOCAL_LOBBY_ACTION_NONE:
            default:
                break;
            }
        }
        else if (appScreen == APP_SCREEN_NETPLAY_LOBBY)
        {
            matchSessionUpdate(&session);

            if (matchSessionConsumePendingMatchInitUiReset(&session))
            {
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
                gNetplayLobbyRevealEndpoint = false;
                appScreen = APP_SCREEN_GAME;
                continue;
            }

            switch (HandleNetplayLobbyInput(&session))
            {
            case NETPLAY_LOBBY_ACTION_RETURN_TO_MENU:
                matchSessionShutdown(&session);
                aiResetController();
                uiResetForNewGame();
                settingsStoreSaveCurrent();
                gNetplayLobbyRevealEndpoint = false;
                appScreen = APP_SCREEN_MAIN_MENU;
                break;
            case NETPLAY_LOBBY_ACTION_START_MATCH:
                if (matchSessionStartNetplayMatch(&session))
                {
                    aiResetController();
                    uiResetForNewGame();
                    uiBeginMatch();
                    gNetplayLobbyRevealEndpoint = false;
                    appScreen = APP_SCREEN_GAME;
                }
                break;
            case NETPLAY_LOBBY_ACTION_NONE:
            default:
                break;
            }

            if (appScreen == APP_SCREEN_NETPLAY_LOBBY && matchSessionHasStarted(&session))
            {
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
                gNetplayLobbyRevealEndpoint = false;
                appScreen = APP_SCREEN_GAME;
            }
        }
        else
        {
            matchSessionUpdate(&session);
            if (matchSessionConsumePendingMatchInitUiReset(&session))
            {
                aiResetController();
                uiResetForNewGame();
                uiBeginMatch();
            }
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

                    if (!matchSessionRestartNetplayMatch(&session))
                    {
                        uiShowCenteredWarning(loc("Private multiplayer error"));
                        continue;
                    }

                    aiResetController();
                    uiResetForNewGame();
                    uiBeginMatch();
                    settingsStoreSaveCurrent();
                    continue;
                }

                setupMap(&session.map);
                if (!hasLocalLobbyConfig)
                {
                    MainMenuGetLobbyConfig(&localLobbyConfig);
                    hasLocalLobbyConfig = true;
                }

                aiDifficulty = localLobbyConfig.aiDifficulty;
                ConfigureLocalMatch(&session, &localLobbyConfig);
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
                gNetplayLobbyRevealEndpoint = false;
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
        else if (appScreen == APP_SCREEN_LOCAL_LOBBY)
        {
            DrawLocalLobby(&session);
        }
        else if (appScreen == APP_SCREEN_NETPLAY_LOBBY)
        {
            DrawNetplayLobby(&session);
        }
        else
        {
            DrawMap(&session.map);
        }

        DrawProfileBadge();

        EndDrawing();
    }

    settingsStoreSaveCurrent();
    matchSessionShutdown(&session);
    UnloadRendererAssets();
    CloseWindow();
    return 0;
}

static enum LocalLobbyAction HandleLocalLobbyInput(struct MatchSession *session,
                                                   struct MainMenuLobbyConfig *config,
                                                   enum AiDifficulty *aiDifficulty)
{
    const Vector2 mouse = GetMousePosition();
    const Rectangle panel = {
        (float)GetScreenWidth() * 0.5f - 760.0f * 0.5f,
        (float)GetScreenHeight() * 0.5f - 540.0f * 0.5f,
        760.0f,
        540.0f};
    const float gutter = 18.0f;
    const float cardWidth = (panel.width - 52.0f - gutter) * 0.5f;
    const float cardHeight = 98.0f;
    const float startX = panel.x + 26.0f;
    const float startY = panel.y + 136.0f;
    const Rectangle aiDifficultyButton = {panel.x + 26.0f, panel.y + panel.height - 116.0f, panel.width - 52.0f, 42.0f};
    const Rectangle primaryButton = {panel.x + 26.0f, panel.y + panel.height - 58.0f, panel.width - 168.0f, 40.0f};
    const Rectangle secondaryButton = {panel.x + panel.width - 122.0f, panel.y + panel.height - 58.0f, 96.0f, 40.0f};

    if (IsKeyPressed(KEY_ESCAPE))
    {
        return LOCAL_LOBBY_ACTION_RETURN_TO_MENU;
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        return LOCAL_LOBBY_ACTION_START_MATCH;
    }

    if (IsKeyPressed(KEY_D) && session != NULL && config != NULL)
    {
        CycleLocalLobbyDifficulty(session, config, aiDifficulty);
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        return LOCAL_LOBBY_ACTION_NONE;
    }

    if (CheckCollisionPointRec(mouse, secondaryButton))
    {
        return LOCAL_LOBBY_ACTION_RETURN_TO_MENU;
    }

    if (session != NULL &&
        config != NULL &&
        LocalLobbyCountHumanSeats(config) < MAX_PLAYERS &&
        CheckCollisionPointRec(mouse, aiDifficultyButton))
    {
        CycleLocalLobbyDifficulty(session, config, aiDifficulty);
        return LOCAL_LOBBY_ACTION_NONE;
    }

    if (session != NULL && config != NULL)
    {
        for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
        {
            const int column = player % 2;
            const int row = player / 2;
            const Rectangle card = {
                startX + column * (cardWidth + gutter),
                startY + row * (cardHeight + 18.0f),
                cardWidth,
                cardHeight};

            if (CheckCollisionPointRec(mouse, card))
            {
                ToggleLocalLobbySeat(session, config, (enum PlayerType)player, aiDifficulty);
                return LOCAL_LOBBY_ACTION_NONE;
            }
        }
    }

    if (CheckCollisionPointRec(mouse, primaryButton))
    {
        return LOCAL_LOBBY_ACTION_START_MATCH;
    }

    return LOCAL_LOBBY_ACTION_NONE;
}

static void DrawLocalLobby(const struct MatchSession *session)
{
    char detailLine[96];
    char difficultyLabel[48];
    char profileLine[96];
    const bool darkTheme = uiGetTheme() == UI_THEME_DARK;
    const Rectangle panel = {
        (float)GetScreenWidth() * 0.5f - 760.0f * 0.5f,
        (float)GetScreenHeight() * 0.5f - 540.0f * 0.5f,
        760.0f,
        540.0f};
    const Rectangle primaryButton = {panel.x + 26.0f, panel.y + panel.height - 58.0f, panel.width - 168.0f, 40.0f};
    const Rectangle secondaryButton = {panel.x + panel.width - 122.0f, panel.y + panel.height - 58.0f, 96.0f, 40.0f};
    const Rectangle aiDifficultyButton = {panel.x + 26.0f, panel.y + panel.height - 116.0f, panel.width - 52.0f, 42.0f};
    const float gutter = 18.0f;
    const float cardWidth = (panel.width - 52.0f - gutter) * 0.5f;
    const float cardHeight = 98.0f;
    const float startX = panel.x + 26.0f;
    const float startY = panel.y + 136.0f;
    const Color panelColor = darkTheme ? (Color){35, 43, 55, 252} : (Color){245, 237, 217, 252};
    const Color borderColor = darkTheme ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
    const Color textColor = darkTheme ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
    const Color bodyColor = darkTheme ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
    const Color accentColor = darkTheme ? (Color){188, 135, 83, 255} : (Color){171, 82, 54, 255};
    const Color sectionFill = darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255};
    int humanSeats = 0;
    enum AiDifficulty displayedDifficulty = AI_DIFFICULTY_MEDIUM;

    if (session != NULL)
    {
        for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
        {
            if (session->map.players[player].controlMode != PLAYER_CONTROL_AI)
            {
                humanSeats++;
            }
            else
            {
                displayedDifficulty = session->map.players[player].aiDifficulty;
            }
        }
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, darkTheme ? 0.46f : 0.28f));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText(loc("Lobby"), panel.x + 26.0f, panel.y + 24.0f, 30, textColor);
    DrawUiText(loc("See all four seats before the match starts."), panel.x + 26.0f, panel.y + 60.0f, 17, bodyColor);
    DrawUiText(loc("Press Enter or click Start Match when ready."), panel.x + 26.0f, panel.y + 86.0f, 18, accentColor);
    DrawUiText(loc("Click a seat to toggle Human or AI."), panel.x + 26.0f, panel.y + 108.0f, 16, bodyColor);
    snprintf(profileLine, sizeof(profileLine), loc("Profile: %s"), uiGetProfileName());
    DrawUiText(profileLine, panel.x + 26.0f, panel.y + 124.0f, 15, bodyColor);

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        const int column = player % 2;
        const int row = player / 2;
        const Rectangle card = {
            startX + column * (cardWidth + gutter),
            startY + row * (cardHeight + 18.0f),
            cardWidth,
            cardHeight};
        const bool aiSeat = session != NULL && session->map.players[player].controlMode == PLAYER_CONTROL_AI;
        const Color playerColor = PlayerColor((enum PlayerType)player);
        const Color fillColor = aiSeat
                                    ? sectionFill
                                    : Fade(playerColor, darkTheme ? 0.18f : 0.14f);
        const Color border = aiSeat ? Fade(borderColor, 0.95f) : Fade(playerColor, 0.95f);
        const char *roleLabel = aiSeat
                                    ? loc("AI")
                                    : (session != NULL &&
                                       matchSessionGetLocalPlayer(session) == (enum PlayerType)player &&
                                       matchSessionGetLocalPlayer(session) != PLAYER_NONE)
                                          ? loc("You")
                                          : loc("Human");
        const Rectangle badge = {card.x + card.width - 112.0f, card.y + 14.0f, 92.0f, 24.0f};

        if (aiSeat)
        {
            snprintf(detailLine, sizeof(detailLine), "%s", aiDifficultyLabel(session->map.players[player].aiDifficulty));
        }
        else if (session != NULL && matchSessionGetLocalPlayer(session) == player)
        {
            snprintf(detailLine, sizeof(detailLine), "%s", uiGetProfileName());
        }
        else
        {
            snprintf(detailLine, sizeof(detailLine), "%s", loc("Ready"));
        }

        DrawRectangleRounded((Rectangle){card.x + 4.0f, card.y + 6.0f, card.width, card.height}, 0.18f, 8, Fade(BLACK, 0.12f));
        DrawRectangleRounded(card, 0.18f, 8, fillColor);
        DrawRectangleLinesEx(card, 1.9f, border);
        DrawCircleV((Vector2){card.x + 22.0f, card.y + 24.0f}, 7.0f, playerColor);
        DrawUiText(locPlayerName((enum PlayerType)player), card.x + 38.0f, card.y + 12.0f, 24, playerColor);
        DrawRectangleRounded(badge, 0.40f, 8, aiSeat ? sectionFill : Fade(playerColor, 0.92f));
        DrawRectangleLinesEx(badge, 1.4f, aiSeat ? Fade(borderColor, 0.95f) : Fade(playerColor, 0.98f));
        DrawUiText(roleLabel,
                   badge.x + badge.width * 0.5f - MeasureUiText(roleLabel, 16) * 0.5f,
                   badge.y + 4.0f,
                   16,
                   aiSeat ? textColor : RAYWHITE);
        DrawUiText(detailLine, card.x + 18.0f, card.y + 56.0f, 18, textColor);
    }

    snprintf(difficultyLabel,
             sizeof(difficultyLabel),
             loc("AI Difficulty: %s"),
             aiDifficultyLabel(displayedDifficulty));
    DrawLobbyButton(aiDifficultyButton,
                    difficultyLabel,
                    humanSeats < MAX_PLAYERS ? sectionFill : Fade(sectionFill, 0.60f),
                    borderColor,
                    humanSeats < MAX_PLAYERS ? textColor : Fade(textColor, 0.60f),
                    false,
                    humanSeats < MAX_PLAYERS);

    DrawLobbyButton(primaryButton,
                    loc("Start Match"),
                    accentColor,
                    borderColor,
                    RAYWHITE,
                    true,
                    true);
    DrawLobbyButton(secondaryButton, loc("Back to Menu"), sectionFill, borderColor, textColor, false, true);
}

static int LocalLobbyCountHumanSeats(const struct MainMenuLobbyConfig *config)
{
    int humans = 0;

    if (config == NULL)
    {
        return 0;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (config->seatControl[player] != PLAYER_CONTROL_AI)
        {
            humans++;
        }
    }

    return humans;
}

static enum PlayerType LocalLobbyFirstHumanSeat(const struct MainMenuLobbyConfig *config)
{
    if (config == NULL)
    {
        return PLAYER_NONE;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (config->seatControl[player] != PLAYER_CONTROL_AI)
        {
            return (enum PlayerType)player;
        }
    }

    return PLAYER_NONE;
}

static void ToggleLocalLobbySeat(struct MatchSession *session,
                                 struct MainMenuLobbyConfig *config,
                                 enum PlayerType player,
                                 enum AiDifficulty *aiDifficulty)
{
    if (session == NULL || config == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return;
    }

    if (config->seatControl[player] == PLAYER_CONTROL_AI)
    {
        config->seatControl[player] = PLAYER_CONTROL_HUMAN;
        config->primaryHumanColor = player;
    }
    else
    {
        if (LocalLobbyCountHumanSeats(config) <= 1)
        {
            uiShowCenteredWarning(loc("At least one human player is required."));
            return;
        }

        config->seatControl[player] = PLAYER_CONTROL_AI;
        if (config->primaryHumanColor == player)
        {
            config->primaryHumanColor = LocalLobbyFirstHumanSeat(config);
        }
    }

    ConfigureLocalMatch(session, config);
    if (aiDifficulty != NULL)
    {
        *aiDifficulty = config->aiDifficulty;
    }
}

static void CycleLocalLobbyDifficulty(struct MatchSession *session,
                                      struct MainMenuLobbyConfig *config,
                                      enum AiDifficulty *aiDifficulty)
{
    if (session == NULL || config == NULL)
    {
        return;
    }

    config->aiDifficulty = (enum AiDifficulty)(((int)config->aiDifficulty + 1) % 3);
    ConfigureLocalMatch(session, config);
    if (aiDifficulty != NULL)
    {
        *aiDifficulty = config->aiDifficulty;
    }
}

static enum NetplayLobbyAction HandleNetplayLobbyInput(struct MatchSession *session)
{
    const Vector2 mouse = GetMousePosition();
    const Rectangle panel = {
        (float)GetScreenWidth() * 0.5f - 760.0f * 0.5f,
        (float)GetScreenHeight() * 0.5f - 540.0f * 0.5f,
        760.0f,
        540.0f};
    const float gutter = 18.0f;
    const float cardWidth = (panel.width - 52.0f - gutter) * 0.5f;
    const float cardHeight = 98.0f;
    const float startX = panel.x + 26.0f;
    const float startY = panel.y + 136.0f;
    const Rectangle aiDifficultyButton = {panel.x + 26.0f, panel.y + panel.height - 116.0f, panel.width - 52.0f, 42.0f};
    const Rectangle endpointButton = {panel.x + panel.width - 96.0f, panel.y + 100.0f, 70.0f, 26.0f};
    const Rectangle copyButton = {panel.x + panel.width - 174.0f, panel.y + 100.0f, 70.0f, 26.0f};
    const Rectangle primaryButton = {panel.x + 26.0f, panel.y + panel.height - 58.0f, panel.width - 168.0f, 40.0f};
    const Rectangle secondaryButton = {panel.x + panel.width - 122.0f, panel.y + panel.height - 58.0f, 96.0f, 40.0f};

    if (IsKeyPressed(KEY_ESCAPE))
    {
        return NETPLAY_LOBBY_ACTION_RETURN_TO_MENU;
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        return NETPLAY_LOBBY_ACTION_NONE;
    }

    if (CheckCollisionPointRec(mouse, secondaryButton))
    {
        return NETPLAY_LOBBY_ACTION_RETURN_TO_MENU;
    }

    if (session != NULL &&
        matchSessionIsHost(session) &&
        !matchSessionHasStarted(session) &&
        CheckCollisionPointRec(mouse, aiDifficultyButton))
    {
        (void)matchSessionHostCycleNetplayLobbyAiDifficulty(session);
        return NETPLAY_LOBBY_ACTION_NONE;
    }

    if (session != NULL &&
        matchSessionIsHost(session) &&
        !matchSessionHasStarted(session))
    {
        for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
        {
            const int column = player % 2;
            const int row = player / 2;
            const Rectangle card = {
                startX + column * (cardWidth + gutter),
                startY + row * (cardHeight + 18.0f),
                cardWidth,
                cardHeight};

            if (CheckCollisionPointRec(mouse, card))
            {
                (void)matchSessionHostToggleNetplayLobbySeat(session, (enum PlayerType)player);
                return NETPLAY_LOBBY_ACTION_NONE;
            }
        }
    }

    if (session != NULL &&
        session->netplay != NULL &&
        CheckCollisionPointRec(mouse, copyButton))
    {
        char endpoint[96];
        const char *address = matchSessionIsHost(session)
                                  ? netplayGetLocalAddress(session->netplay)
                                  : netplayGetPeerAddress(session->netplay);
        snprintf(endpoint,
                 sizeof(endpoint),
                 "%s:%u",
                 (address != NULL && address[0] != '\0') ? address : "127.0.0.1",
                 (unsigned int)netplayGetPort(session->netplay));
        SetClipboardText(endpoint);
        uiShowCenteredStatus(loc("Endpoint copied to clipboard."), UI_NOTIFICATION_POSITIVE);
        return NETPLAY_LOBBY_ACTION_NONE;
    }

    if (session != NULL &&
        session->netplay != NULL &&
        CheckCollisionPointRec(mouse, endpointButton))
    {
        gNetplayLobbyRevealEndpoint = !gNetplayLobbyRevealEndpoint;
        return NETPLAY_LOBBY_ACTION_NONE;
    }

    if (CheckCollisionPointRec(mouse, primaryButton) && matchSessionCanStartNetplayMatch(session))
    {
        return NETPLAY_LOBBY_ACTION_START_MATCH;
    }

    return NETPLAY_LOBBY_ACTION_NONE;
}

static void DrawLobbyButton(Rectangle bounds, const char *label, Color fill, Color border, Color text, bool emphasized, bool enabled)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = enabled && CheckCollisionPointRec(mouse, bounds);
    const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const float yOffset = pressed ? 2.0f : (hovered ? -2.0f : 0.0f);
    const Rectangle button = {bounds.x, bounds.y + yOffset, bounds.width, bounds.height};
    const int fontSize = emphasized ? 24 : 21;
    const int labelWidth = MeasureUiText(label, fontSize);
    const Color shadow = emphasized ? Fade(BLACK, 0.18f) : Fade(BLACK, 0.12f);
    const Color drawFill = enabled ? (hovered ? ColorBrightness(fill, 0.08f) : fill) : Fade(fill, 0.60f);
    const Color drawBorder = enabled ? (hovered ? ColorBrightness(border, 0.08f) : border) : Fade(border, 0.65f);
    const Color drawText = enabled ? text : Fade(text, 0.65f);

    DrawRectangleRounded((Rectangle){button.x + 4.0f, button.y + 6.0f, button.width, button.height}, 0.18f, 8, shadow);
    DrawRectangleRounded(button, 0.18f, 8, drawFill);
    DrawRectangleLinesEx(button, emphasized ? 2.2f : 1.8f, drawBorder);
    DrawUiText(label, button.x + button.width * 0.5f - labelWidth * 0.5f, button.y + button.height * 0.5f - (float)fontSize * 0.5f - 1.0f, fontSize, drawText);
}

static void DrawProfileBadge(void)
{
    const char *name = uiGetProfileName();
    const char *label = TextFormat(loc("Profile: %s"), (name != NULL && name[0] != '\0') ? name : loc("Player"));
    const int fontSize = 16;
    const int labelWidth = MeasureUiText(label, fontSize);
    const Rectangle badge = {16.0f, 12.0f, (float)labelWidth + 26.0f, 30.0f};
    const Color fill = uiGetTheme() == UI_THEME_DARK
                           ? (Color){42, 54, 70, 236}
                           : (Color){248, 240, 223, 236};
    const Color border = uiGetTheme() == UI_THEME_DARK
                             ? (Color){133, 151, 176, 255}
                             : (Color){118, 88, 56, 255};
    const Color text = uiGetTheme() == UI_THEME_DARK
                           ? (Color){234, 241, 246, 255}
                           : (Color){54, 39, 29, 255};

    DrawRectangleRounded((Rectangle){badge.x + 3.0f, badge.y + 4.0f, badge.width, badge.height}, 0.28f, 8, Fade(BLACK, 0.11f));
    DrawRectangleRounded(badge, 0.28f, 8, fill);
    DrawRectangleLinesEx(badge, 1.6f, border);
    DrawUiText(label, badge.x + 12.0f, badge.y + 7.0f, fontSize, text);
}

static const char *NetplayLobbyStatusLabel(const struct MatchSession *session)
{
    if (session == NULL || !matchSessionIsNetplay(session))
    {
        return "";
    }

    switch (matchSessionGetConnectionStatus(session))
    {
    case MATCH_CONNECTION_WAITING_FOR_PLAYER:
        return loc("Waiting for remote player...");
    case MATCH_CONNECTION_CONNECTING:
        return loc("Connecting to host...");
    case MATCH_CONNECTION_SYNCING:
        return loc("Syncing match state...");
    case MATCH_CONNECTION_CONNECTED:
        return matchSessionHasStarted(session) ? loc("Match starting...") : loc("Private multiplayer connected");
    case MATCH_CONNECTION_DISCONNECTED:
        return loc("Disconnected from remote player");
    case MATCH_CONNECTION_ERROR:
        return loc("Private multiplayer error");
    case MATCH_CONNECTION_LOCAL:
    default:
        return "";
    }
}

static const char *NetplayLobbySeatRoleLabel(const struct MatchSession *session, enum PlayerType player)
{
    if (session == NULL || player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return "";
    }

    if (session->map.players[player].controlMode == PLAYER_CONTROL_AI)
    {
        return loc("AI");
    }

    if (matchSessionGetLocalPlayer(session) == player)
    {
        return matchSessionIsHost(session) ? loc("Host") : loc("You");
    }

    if (matchSessionGetSeatAuthority(session, player) == MATCH_SEAT_REMOTE)
    {
        if (matchSessionIsHost(session) &&
            matchSessionGetConnectionStatus(session) != MATCH_CONNECTION_CONNECTED)
        {
            return loc("Waiting");
        }

        return loc("Remote");
    }

    return loc("Human");
}

static void DrawNetplayLobby(const struct MatchSession *session)
{
    char endpointLine[128];
    char detailLine[96];
    char endpointValue[96];
    char difficultyLabel[48];
    char profileLine[96];
    const char *connectionError = session != NULL ? matchSessionGetConnectionError(session) : "";
    const bool darkTheme = uiGetTheme() == UI_THEME_DARK;
    const Rectangle panel = {
        (float)GetScreenWidth() * 0.5f - 760.0f * 0.5f,
        (float)GetScreenHeight() * 0.5f - 540.0f * 0.5f,
        760.0f,
        540.0f};
    const Rectangle endpointButton = {panel.x + panel.width - 96.0f, panel.y + 100.0f, 70.0f, 26.0f};
    const Rectangle copyButton = {panel.x + panel.width - 174.0f, panel.y + 100.0f, 70.0f, 26.0f};
    const Rectangle aiDifficultyButton = {panel.x + 26.0f, panel.y + panel.height - 116.0f, panel.width - 52.0f, 42.0f};
    const Rectangle primaryButton = {panel.x + 26.0f, panel.y + panel.height - 58.0f, panel.width - 168.0f, 40.0f};
    const Rectangle secondaryButton = {panel.x + panel.width - 122.0f, panel.y + panel.height - 58.0f, 96.0f, 40.0f};
    const float gutter = 18.0f;
    const float cardWidth = (panel.width - 52.0f - gutter) * 0.5f;
    const float cardHeight = 98.0f;
    const float startX = panel.x + 26.0f;
    const float startY = panel.y + 136.0f;
    const Color panelColor = darkTheme ? (Color){35, 43, 55, 252} : (Color){245, 237, 217, 252};
    const Color borderColor = darkTheme ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
    const Color textColor = darkTheme ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
    const Color bodyColor = darkTheme ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
    const Color accentColor = darkTheme ? (Color){188, 135, 83, 255} : (Color){171, 82, 54, 255};
    const Color errorColor = darkTheme ? (Color){242, 126, 116, 255} : (Color){171, 82, 54, 255};
    const Color sectionFill = darkTheme ? (Color){63, 77, 95, 255} : (Color){233, 226, 207, 255};
    const bool hostCanStart = matchSessionCanStartNetplayMatch(session);
    int aiSeatCount = 0;
    enum AiDifficulty displayedDifficulty = AI_DIFFICULTY_MEDIUM;

    endpointLine[0] = '\0';
    if (session != NULL && session->netplay != NULL)
    {
        if (matchSessionIsHost(session))
        {
            snprintf(endpointValue,
                     sizeof(endpointValue),
                     "%s:%u",
                     gNetplayLobbyRevealEndpoint ? netplayGetLocalAddress(session->netplay) : "***.***.***.***",
                     (unsigned int)netplayGetPort(session->netplay));
            snprintf(endpointLine,
                     sizeof(endpointLine),
                     loc("Open on %s"),
                     endpointValue);
        }
        else
        {
            snprintf(endpointValue,
                     sizeof(endpointValue),
                     "%s:%u",
                     gNetplayLobbyRevealEndpoint ? netplayGetPeerAddress(session->netplay) : "***.***.***.***",
                     (unsigned int)netplayGetPort(session->netplay));
            snprintf(endpointLine,
                     sizeof(endpointLine),
                     loc("Connected to %s"),
                     endpointValue);
        }
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, darkTheme ? 0.46f : 0.28f));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText(loc("Lobby"), panel.x + 26.0f, panel.y + 24.0f, 30, textColor);
    DrawUiText(loc("Private multiplayer supports up to 4 humans and AI."), panel.x + 26.0f, panel.y + 60.0f, 17, bodyColor);
    DrawUiText(NetplayLobbyStatusLabel(session), panel.x + 26.0f, panel.y + 86.0f, 18, accentColor);
    snprintf(profileLine, sizeof(profileLine), loc("Profile: %s"), uiGetProfileName());
    DrawUiText(profileLine, panel.x + 26.0f, panel.y + 124.0f, 15, bodyColor);
    DrawUiText(matchSessionIsHost(session)
                   ? loc("Click a seat to toggle Human or AI.")
                   : loc("Waiting for host to start the match."),
               panel.x + 26.0f,
               panel.y + 108.0f,
               16,
               bodyColor);
    if (endpointLine[0] != '\0')
    {
        DrawUiText(endpointLine, panel.x + 26.0f, panel.y + 108.0f, 16, bodyColor);
        DrawLobbyButton(copyButton,
                        loc("Copy"),
                        sectionFill,
                        borderColor,
                        textColor,
                        false,
                        true);
        DrawLobbyButton(endpointButton,
                        gNetplayLobbyRevealEndpoint ? loc("Hide") : loc("Show"),
                        sectionFill,
                        borderColor,
                        textColor,
                        false,
                        true);
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        const int column = player % 2;
        const int row = player / 2;
        const Rectangle card = {
            startX + column * (cardWidth + gutter),
            startY + row * (cardHeight + 18.0f),
            cardWidth,
            cardHeight};
        const bool aiSeat = session != NULL && session->map.players[player].controlMode == PLAYER_CONTROL_AI;
        const Color playerColor = PlayerColor((enum PlayerType)player);
        const Color fillColor = aiSeat
                                    ? sectionFill
                                    : Fade(playerColor, darkTheme ? 0.18f : 0.14f);
        const Color border = aiSeat ? Fade(borderColor, 0.95f) : Fade(playerColor, 0.95f);
        const char *roleLabel = NetplayLobbySeatRoleLabel(session, (enum PlayerType)player);
        const Rectangle badge = {card.x + card.width - 112.0f, card.y + 14.0f, 92.0f, 24.0f};

        if (aiSeat)
        {
            aiSeatCount++;
            displayedDifficulty = session->map.players[player].aiDifficulty;
            snprintf(detailLine, sizeof(detailLine), "%s", aiDifficultyLabel(session->map.players[player].aiDifficulty));
        }
        else if (matchSessionGetConnectionStatus(session) != MATCH_CONNECTION_CONNECTED)
        {
            snprintf(detailLine, sizeof(detailLine), "%s", NetplayLobbyStatusLabel(session));
        }
        else if (matchSessionGetSeatAuthority(session, (enum PlayerType)player) == MATCH_SEAT_REMOTE && matchSessionIsHost(session))
        {
            snprintf(detailLine,
                     sizeof(detailLine),
                     "%s",
                     matchSessionGetConnectionStatus(session) == MATCH_CONNECTION_CONNECTED ? loc("Connected") : loc("Waiting for remote player..."));
        }
        else if (matchSessionGetLocalPlayer(session) == player)
        {
            snprintf(detailLine, sizeof(detailLine), "%s", uiGetProfileName());
        }
        else
        {
            snprintf(detailLine, sizeof(detailLine), "%s", loc("Connected"));
        }

        DrawRectangleRounded((Rectangle){card.x + 4.0f, card.y + 6.0f, card.width, card.height}, 0.18f, 8, Fade(BLACK, 0.12f));
        DrawRectangleRounded(card, 0.18f, 8, fillColor);
        DrawRectangleLinesEx(card, 1.9f, border);
        DrawCircleV((Vector2){card.x + 22.0f, card.y + 24.0f}, 7.0f, playerColor);
        DrawUiText(locPlayerName((enum PlayerType)player), card.x + 38.0f, card.y + 12.0f, 24, playerColor);
        DrawRectangleRounded(badge, 0.40f, 8, aiSeat ? sectionFill : Fade(playerColor, 0.92f));
        DrawRectangleLinesEx(badge, 1.4f, aiSeat ? Fade(borderColor, 0.95f) : Fade(playerColor, 0.98f));
        DrawUiText(roleLabel,
                   badge.x + badge.width * 0.5f - MeasureUiText(roleLabel, 16) * 0.5f,
                   badge.y + 4.0f,
                   16,
                   aiSeat ? textColor : RAYWHITE);
        DrawUiText(detailLine, card.x + 18.0f, card.y + 56.0f, 18, textColor);
    }

    snprintf(difficultyLabel,
             sizeof(difficultyLabel),
             loc("AI Difficulty: %s"),
             aiDifficultyLabel(displayedDifficulty));
    DrawLobbyButton(aiDifficultyButton,
                    difficultyLabel,
                    aiSeatCount > 0 ? sectionFill : Fade(sectionFill, 0.60f),
                    borderColor,
                    aiSeatCount > 0 ? textColor : Fade(textColor, 0.60f),
                    false,
                    matchSessionIsHost(session) && !matchSessionHasStarted(session) && aiSeatCount > 0);

    if (session != NULL && !matchSessionIsHost(session))
    {
        if (connectionError[0] != '\0')
        {
            DrawUiText(connectionError, panel.x + 26.0f, primaryButton.y - 30.0f, 16, errorColor);
        }
        else
        {
            DrawUiText(loc("Waiting for host to start the match."), panel.x + 26.0f, primaryButton.y - 30.0f, 16, bodyColor);
        }
    }
    else if (connectionError[0] != '\0')
    {
        DrawUiText(connectionError, panel.x + 26.0f, primaryButton.y - 30.0f, 16, errorColor);
    }
    else if (!hostCanStart)
    {
        DrawUiText(loc("Host can start once the remote player connects."), panel.x + 26.0f, primaryButton.y - 30.0f, 16, bodyColor);
    }

    DrawLobbyButton(primaryButton,
                    matchSessionIsHost(session) ? loc("Start Match") : loc("Waiting for host to start the match."),
                    accentColor,
                    borderColor,
                    RAYWHITE,
                    true,
                    matchSessionIsHost(session) && hostCanStart);
    DrawLobbyButton(secondaryButton, loc("Back to Menu"), sectionFill, borderColor, textColor, false, true);
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
