#include "ui_state.h"
#include "debug_log.h"
#include "game_action.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <raylib.h>
#include "game_logic.h"
#include "localization.h"
#include "match_session.h"

#define PLAYER_NOTIFICATION_SLOTS 8
#define PLAYER_NOTIFICATION_TEXT_CAPACITY 96
#define CENTERED_WARNING_TEXT_CAPACITY 192

struct UiPlayerNotification
{
    bool active;
    bool dismissing;
    char text[PLAYER_NOTIFICATION_TEXT_CAPACITY];
    enum UiNotificationTone tone;
    double startTime;
    double dismissStartTime;
};

static bool gTradeMenuOpen = false;
static float gTradeMenuOpenAmount = 0.0f;
static bool gPlayerTradeMenuOpen = false;
static float gPlayerTradeMenuOpenAmount = 0.0f;
static bool gSettingsMenuOpen = false;
static float gSettingsMenuOpenAmount = 0.0f;
static bool gSettingsMultiplayerInfoExpanded = false;
static enum UiSettingsConfirmAction gSettingsConfirmAction = UI_SETTINGS_CONFIRM_NONE;
static bool gBuildPanelOpen = false;
static float gBuildPanelOpenAmount = 0.0f;
static bool gDevelopmentPurchaseConfirmOpen = false;
static float gDevelopmentPurchaseConfirmOpenAmount = 0.0f;
static bool gDevelopmentPlayConfirmOpen = false;
static float gDevelopmentPlayConfirmOpenAmount = 0.0f;
static enum DevelopmentCardType gDevelopmentPlayCardType = DEVELOPMENT_CARD_COUNT;
static enum UiTheme gTheme = UI_THEME_LIGHT;
static int gAiSpeedSetting = 3;
static bool gReturnToMainMenuRequested = false;
static bool gRestartGameRequested = false;
static bool gQuitGameRequested = false;
static unsigned long long gCommittedTotalPlaytimeSeconds = 0ULL;
static unsigned long long gCommittedTotalWins = 0ULL;
static unsigned long long gCommittedTotalLosses = 0ULL;
static double gCurrentMatchStartTime = -1.0;
static double gCurrentMatchFrozenDurationSeconds = -1.0;
static bool gCurrentMatchResultCommitted = false;
static double gLastRoadPlacementTime = -1.0;
static int gLastRoadAx = 0;
static int gLastRoadAy = 0;
static int gLastRoadBx = 0;
static int gLastRoadBy = 0;
static double gLastStructurePlacementTime = -1.0;
static int gLastStructureX = 0;
static int gLastStructureY = 0;
static bool gDiceRolling = false;
static double gDiceRollStartTime = 0.0;
static double gLastDiceShuffleTime = 0.0;
static int gPendingDieA = 1;
static int gPendingDieB = 1;
static int gDisplayedDieA = 1;
static int gDisplayedDieB = 1;
static double gBoardCreationAnimationStartTime = 0.0;
static int gRecentRollHighlightValue = 0;
static double gRecentRollHighlightStartTime = -1.0;
static bool gDevelopmentCardDrawAnimating = false;
static double gDevelopmentCardDrawAnimationStartTime = 0.0;
static enum DevelopmentCardType gAnimatedDevelopmentCardType = DEVELOPMENT_CARD_KNIGHT;
static bool gPlayerDeltaTrackerInitialized = false;
static enum PlayerType gTrackedCurrentPlayer = PLAYER_NONE;
static int gCurrentTurnResourceGain[5] = {0};
static int gTrackedPlayerResources[MAX_PLAYERS][5] = {0};
static int gTrackedVisibleVictoryPoints[MAX_PLAYERS] = {0};
static struct UiPlayerNotification gPlayerNotifications[MAX_PLAYERS][PLAYER_NOTIFICATION_SLOTS] = {0};
static char gCenteredWarningText[CENTERED_WARNING_TEXT_CAPACITY] = {0};
static double gCenteredWarningStartTime = -1.0;
static char gCenteredStatusText[CENTERED_WARNING_TEXT_CAPACITY] = {0};
static enum UiNotificationTone gCenteredStatusTone = UI_NOTIFICATION_NEUTRAL;
static double gCenteredStatusStartTime = -1.0;

static void update_build_panel_animation(void);
static void update_development_purchase_confirm_animation(void);
static void update_development_play_confirm_animation(void);
static void update_trade_menu_animation(void);
static void update_player_trade_menu_animation(void);
static void update_settings_menu_animation(void);
static void update_dice_animation(struct Map *map);
static void update_development_card_draw_animation(void);
static void update_player_delta_notifications(const struct Map *map);
static void snapshot_player_delta_state(const struct Map *map);
static void prune_player_notifications(void);
static void push_player_notification(enum PlayerType player, const char *text, enum UiNotificationTone tone);
static void dismiss_player_notifications(void);
static const char *resource_label(enum ResourceType resource, bool abbreviated);
static const char *player_label(enum PlayerType player);
static enum PlayerType local_human_player(const struct Map *map);
static void show_local_resource_feedback(const struct Map *map, enum PlayerType localPlayer,
                                         const int positiveDeltas[5], const int negativeDeltas[5],
                                         bool localTurnStarted, bool *showedStatus);
static void format_resource_delta_list(const int deltas[5], char *buffer, size_t bufferSize);
static void reset_ui_state(bool resetTheme);
static unsigned long long current_match_elapsed_seconds(void);
static void commit_match_result_if_needed(const struct Map *map);

void initUiState(void)
{
    reset_ui_state(true);
}

void uiResetForNewGame(void)
{
    reset_ui_state(false);
}

void uiBeginMatch(void)
{
    gCurrentMatchStartTime = GetTime();
    gCurrentMatchFrozenDurationSeconds = -1.0;
    gCurrentMatchResultCommitted = false;
}

static void reset_ui_state(bool resetTheme)
{
    gCommittedTotalPlaytimeSeconds += current_match_elapsed_seconds();
    gCurrentMatchStartTime = -1.0;
    gCurrentMatchFrozenDurationSeconds = -1.0;
    gCurrentMatchResultCommitted = false;
    gTradeMenuOpen = false;
    gTradeMenuOpenAmount = 0.0f;
    gPlayerTradeMenuOpen = false;
    gPlayerTradeMenuOpenAmount = 0.0f;
    gSettingsMenuOpen = false;
    gSettingsMenuOpenAmount = 0.0f;
    gSettingsMultiplayerInfoExpanded = false;
    gSettingsConfirmAction = UI_SETTINGS_CONFIRM_NONE;
    gBuildPanelOpen = false;
    gBuildPanelOpenAmount = 0.0f;
    gDevelopmentPurchaseConfirmOpen = false;
    gDevelopmentPurchaseConfirmOpenAmount = 0.0f;
    gDevelopmentPlayConfirmOpen = false;
    gDevelopmentPlayConfirmOpenAmount = 0.0f;
    gDevelopmentPlayCardType = DEVELOPMENT_CARD_COUNT;
    gLastRoadPlacementTime = -1.0;
    gLastStructurePlacementTime = -1.0;
    gDiceRolling = false;
    gDisplayedDieA = 1;
    gDisplayedDieB = 1;
    gBoardCreationAnimationStartTime = GetTime();
    gRecentRollHighlightValue = 0;
    gRecentRollHighlightStartTime = -1.0;
    gDevelopmentCardDrawAnimating = false;
    gDevelopmentCardDrawAnimationStartTime = 0.0;
    gAnimatedDevelopmentCardType = DEVELOPMENT_CARD_KNIGHT;
    gPlayerDeltaTrackerInitialized = false;
    gTrackedCurrentPlayer = PLAYER_NONE;
    memset(gCurrentTurnResourceGain, 0, sizeof(gCurrentTurnResourceGain));
    memset(gTrackedPlayerResources, 0, sizeof(gTrackedPlayerResources));
    memset(gTrackedVisibleVictoryPoints, 0, sizeof(gTrackedVisibleVictoryPoints));
    memset(gPlayerNotifications, 0, sizeof(gPlayerNotifications));
    memset(gCenteredWarningText, 0, sizeof(gCenteredWarningText));
    gCenteredWarningStartTime = -1.0;
    memset(gCenteredStatusText, 0, sizeof(gCenteredStatusText));
    gCenteredStatusTone = UI_NOTIFICATION_NEUTRAL;
    gCenteredStatusStartTime = -1.0;
    gReturnToMainMenuRequested = false;
    gRestartGameRequested = false;
    gQuitGameRequested = false;
    if (resetTheme)
    {
        gTheme = UI_THEME_LIGHT;
        gAiSpeedSetting = 3;
        gCommittedTotalPlaytimeSeconds = 0ULL;
        gCommittedTotalWins = 0ULL;
        gCommittedTotalLosses = 0ULL;
    }
}

void updateUiState(struct Map *map)
{
    if (map != NULL &&
        gCurrentMatchStartTime >= 0.0 &&
        gCurrentMatchFrozenDurationSeconds < 0.0 &&
        gameHasWinner(map))
    {
        gCurrentMatchFrozenDurationSeconds = GetTime() - gCurrentMatchStartTime;
    }
    commit_match_result_if_needed(map);

    update_build_panel_animation();
    update_development_purchase_confirm_animation();
    update_development_play_confirm_animation();
    update_trade_menu_animation();
    update_player_trade_menu_animation();
    update_settings_menu_animation();
    update_dice_animation(map);
    update_development_card_draw_animation();
    update_player_delta_notifications(map);
}

void handleUiGlobalInput(void)
{
    if (IsKeyPressed(KEY_B) || IsKeyPressed(KEY_ONE))
    {
        uiToggleBuildPanel();
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        uiToggleSettingsMenu();
    }
}

void uiToggleBuildPanel(void)
{
    gBuildPanelOpen = !gBuildPanelOpen;
    if (!gBuildPanelOpen)
    {
        gDevelopmentPurchaseConfirmOpen = false;
    }
}

void uiSetBuildPanelOpen(bool open)
{
    gBuildPanelOpen = open;
    if (!open)
    {
        gDevelopmentPurchaseConfirmOpen = false;
    }
}

bool uiIsBuildPanelOpen(void)
{
    return gBuildPanelOpen;
}

float uiGetBuildPanelOpenAmount(void)
{
    return gBuildPanelOpenAmount;
}

void uiSetDevelopmentPurchaseConfirmOpen(bool open)
{
    gDevelopmentPurchaseConfirmOpen = open;
}

bool uiIsDevelopmentPurchaseConfirmOpen(void)
{
    return gDevelopmentPurchaseConfirmOpen;
}

float uiGetDevelopmentPurchaseConfirmOpenAmount(void)
{
    return gDevelopmentPurchaseConfirmOpenAmount;
}

void uiSetDevelopmentPlayConfirmOpen(bool open)
{
    gDevelopmentPlayConfirmOpen = open;
    if (!open)
    {
        gDevelopmentPlayCardType = DEVELOPMENT_CARD_COUNT;
    }
}

bool uiIsDevelopmentPlayConfirmOpen(void)
{
    return gDevelopmentPlayConfirmOpen;
}

float uiGetDevelopmentPlayConfirmOpenAmount(void)
{
    return gDevelopmentPlayConfirmOpenAmount;
}

void uiSetDevelopmentPlayCardType(enum DevelopmentCardType type)
{
    gDevelopmentPlayCardType = type;
}

enum DevelopmentCardType uiGetDevelopmentPlayCardType(void)
{
    return gDevelopmentPlayCardType;
}

void uiShowCenteredWarning(const char *text)
{
    if (text == NULL || text[0] == '\0')
    {
        gCenteredWarningText[0] = '\0';
        gCenteredWarningStartTime = -1.0;
        return;
    }

    strncpy(gCenteredWarningText, text, sizeof(gCenteredWarningText) - 1);
    gCenteredWarningText[sizeof(gCenteredWarningText) - 1] = '\0';
    gCenteredWarningStartTime = GetTime();
}

bool uiHasCenteredWarning(void)
{
    return gCenteredWarningText[0] != '\0' && uiGetCenteredWarningAlpha() > 0.01f;
}

const char *uiGetCenteredWarningText(void)
{
    return gCenteredWarningText;
}

float uiGetCenteredWarningAlpha(void)
{
    const float fadeIn = 0.12f;
    const float hold = 2.8f;
    const float fadeOut = 0.34f;
    const float totalDuration = fadeIn + hold + fadeOut;
    const float elapsed = gCenteredWarningStartTime < 0.0 ? totalDuration : (float)(GetTime() - gCenteredWarningStartTime);

    if (gCenteredWarningText[0] == '\0')
    {
        return 0.0f;
    }
    if (elapsed <= 0.0f)
    {
        return 0.0f;
    }
    if (elapsed < fadeIn)
    {
        return elapsed / fadeIn;
    }
    if (elapsed < fadeIn + hold)
    {
        return 1.0f;
    }
    if (elapsed < totalDuration)
    {
        return 1.0f - (elapsed - fadeIn - hold) / fadeOut;
    }
    return 0.0f;
}

float uiGetCenteredWarningVerticalOffset(void)
{
    const float fadeIn = 0.12f;
    const float hold = 2.8f;
    const float fadeOut = 0.34f;
    const float totalDuration = fadeIn + hold + fadeOut;
    const float elapsed = gCenteredWarningStartTime < 0.0 ? totalDuration : (float)(GetTime() - gCenteredWarningStartTime);

    if (gCenteredWarningText[0] == '\0')
    {
        return 0.0f;
    }
    if (elapsed <= 0.0f)
    {
        return -20.0f;
    }
    if (elapsed < fadeIn)
    {
        return -20.0f + 20.0f * (elapsed / fadeIn);
    }
    if (elapsed < fadeIn + hold)
    {
        return 0.0f;
    }
    if (elapsed < totalDuration)
    {
        return 24.0f * ((elapsed - fadeIn - hold) / fadeOut);
    }
    return 24.0f;
}

void uiShowCenteredStatus(const char *text, enum UiNotificationTone tone)
{
    if (text == NULL || text[0] == '\0')
    {
        gCenteredStatusText[0] = '\0';
        gCenteredStatusTone = UI_NOTIFICATION_NEUTRAL;
        gCenteredStatusStartTime = -1.0;
        return;
    }

    strncpy(gCenteredStatusText, text, sizeof(gCenteredStatusText) - 1);
    gCenteredStatusText[sizeof(gCenteredStatusText) - 1] = '\0';
    gCenteredStatusTone = tone;
    gCenteredStatusStartTime = GetTime();
}

bool uiHasCenteredStatus(void)
{
    return gCenteredStatusText[0] != '\0' && uiGetCenteredStatusAlpha() > 0.01f;
}

const char *uiGetCenteredStatusText(void)
{
    return gCenteredStatusText;
}

enum UiNotificationTone uiGetCenteredStatusTone(void)
{
    return gCenteredStatusTone;
}

float uiGetCenteredStatusAlpha(void)
{
    const float fadeIn = 0.16f;
    const float hold = 2.25f;
    const float fadeOut = 0.34f;
    const float totalDuration = fadeIn + hold + fadeOut;
    const float elapsed = gCenteredStatusStartTime < 0.0 ? totalDuration : (float)(GetTime() - gCenteredStatusStartTime);

    if (gCenteredStatusText[0] == '\0')
    {
        return 0.0f;
    }
    if (elapsed <= 0.0f)
    {
        return 0.0f;
    }
    if (elapsed < fadeIn)
    {
        return elapsed / fadeIn;
    }
    if (elapsed < fadeIn + hold)
    {
        return 1.0f;
    }
    if (elapsed < totalDuration)
    {
        return 1.0f - (elapsed - fadeIn - hold) / fadeOut;
    }
    return 0.0f;
}

float uiGetCenteredStatusVerticalOffset(void)
{
    const float fadeIn = 0.16f;
    const float hold = 2.25f;
    const float fadeOut = 0.34f;
    const float totalDuration = fadeIn + hold + fadeOut;
    const float elapsed = gCenteredStatusStartTime < 0.0 ? totalDuration : (float)(GetTime() - gCenteredStatusStartTime);

    if (gCenteredStatusText[0] == '\0')
    {
        return 0.0f;
    }
    if (elapsed <= 0.0f)
    {
        return -26.0f;
    }
    if (elapsed < fadeIn)
    {
        return -26.0f + 26.0f * (elapsed / fadeIn);
    }
    if (elapsed < fadeIn + hold)
    {
        return 0.0f;
    }
    if (elapsed < totalDuration)
    {
        return 22.0f * ((elapsed - fadeIn - hold) / fadeOut);
    }
    return 22.0f;
}

void uiToggleTradeMenu(void)
{
    gTradeMenuOpen = !gTradeMenuOpen;
}

void uiSetTradeMenuOpen(bool open)
{
    gTradeMenuOpen = open;
}

bool uiIsTradeMenuOpen(void)
{
    return gTradeMenuOpen;
}

float uiGetTradeMenuOpenAmount(void)
{
    return gTradeMenuOpenAmount;
}

void uiTogglePlayerTradeMenu(void)
{
    gPlayerTradeMenuOpen = !gPlayerTradeMenuOpen;
}

void uiSetPlayerTradeMenuOpen(bool open)
{
    gPlayerTradeMenuOpen = open;
}

bool uiIsPlayerTradeMenuOpen(void)
{
    return gPlayerTradeMenuOpen;
}

float uiGetPlayerTradeMenuOpenAmount(void)
{
    return gPlayerTradeMenuOpenAmount;
}

void uiToggleSettingsMenu(void)
{
    gSettingsMenuOpen = !gSettingsMenuOpen;
    if (!gSettingsMenuOpen)
    {
        gSettingsMultiplayerInfoExpanded = false;
        gSettingsConfirmAction = UI_SETTINGS_CONFIRM_NONE;
    }
}

void uiSetSettingsMenuOpen(bool open)
{
    gSettingsMenuOpen = open;
    if (!open)
    {
        gSettingsMultiplayerInfoExpanded = false;
        gSettingsConfirmAction = UI_SETTINGS_CONFIRM_NONE;
    }
}

bool uiIsSettingsMenuOpen(void)
{
    return gSettingsMenuOpen;
}

float uiGetSettingsMenuOpenAmount(void)
{
    return gSettingsMenuOpenAmount;
}

void uiToggleSettingsMultiplayerInfoExpanded(void)
{
    gSettingsMultiplayerInfoExpanded = !gSettingsMultiplayerInfoExpanded;
}

void uiSetSettingsMultiplayerInfoExpanded(bool expanded)
{
    gSettingsMultiplayerInfoExpanded = expanded;
}

bool uiIsSettingsMultiplayerInfoExpanded(void)
{
    return gSettingsMultiplayerInfoExpanded;
}

void uiSetTheme(enum UiTheme theme)
{
    gTheme = theme;
}

enum UiTheme uiGetTheme(void)
{
    return gTheme;
}

void uiSetAiSpeedSetting(int speed)
{
    if (speed < 0)
    {
        speed = 0;
    }
    if (speed > 10)
    {
        speed = 10;
    }
    gAiSpeedSetting = speed;
}

int uiGetAiSpeedSetting(void)
{
    return gAiSpeedSetting;
}

void uiSetSettingsConfirmAction(enum UiSettingsConfirmAction action)
{
    gSettingsConfirmAction = action;
}

enum UiSettingsConfirmAction uiGetSettingsConfirmAction(void)
{
    return gSettingsConfirmAction;
}

void uiRequestReturnToMainMenu(void)
{
    gReturnToMainMenuRequested = true;
}

bool uiConsumeReturnToMainMenuRequest(void)
{
    const bool requested = gReturnToMainMenuRequested;
    gReturnToMainMenuRequested = false;
    return requested;
}

void uiRequestRestartGame(void)
{
    gRestartGameRequested = true;
}

bool uiConsumeRestartGameRequest(void)
{
    const bool requested = gRestartGameRequested;
    gRestartGameRequested = false;
    return requested;
}

void uiRequestQuitGame(void)
{
    gQuitGameRequested = true;
}

bool uiConsumeQuitGameRequest(void)
{
    const bool requested = gQuitGameRequested;
    gQuitGameRequested = false;
    return requested;
}

void uiSetPersistedTotalPlaytimeSeconds(unsigned long long seconds)
{
    gCommittedTotalPlaytimeSeconds = seconds;
}

void uiSetPersistedMatchRecord(unsigned long long wins, unsigned long long losses)
{
    gCommittedTotalWins = wins;
    gCommittedTotalLosses = losses;
}

unsigned long long uiGetCurrentMatchPlaytimeSeconds(void)
{
    return current_match_elapsed_seconds();
}

unsigned long long uiGetTotalPlaytimeSeconds(void)
{
    return gCommittedTotalPlaytimeSeconds + current_match_elapsed_seconds();
}

unsigned long long uiGetTotalWins(void)
{
    return gCommittedTotalWins;
}

unsigned long long uiGetTotalLosses(void)
{
    return gCommittedTotalLosses;
}

void uiStartDiceRollAnimation(void)
{
    gDiceRolling = true;
    gDiceRollStartTime = GetTime();
    gLastDiceShuffleTime = 0.0;
    gPendingDieA = GetRandomValue(1, 6);
    gPendingDieB = GetRandomValue(1, 6);
    gDisplayedDieA = GetRandomValue(1, 6);
    gDisplayedDieB = GetRandomValue(1, 6);
    debugLog("UI", "dice animation start pending=%d+%d total=%d",
             gPendingDieA,
             gPendingDieB,
             gPendingDieA + gPendingDieB);
}

bool uiIsDiceRolling(void)
{
    return gDiceRolling;
}

int uiGetDisplayedDieA(void)
{
    return gDisplayedDieA;
}

int uiGetDisplayedDieB(void)
{
    return gDisplayedDieB;
}

void uiStartDevelopmentCardDrawAnimation(enum DevelopmentCardType type)
{
    gDevelopmentCardDrawAnimating = true;
    gDevelopmentCardDrawAnimationStartTime = GetTime();
    gAnimatedDevelopmentCardType = type;
}

bool uiIsDevelopmentCardDrawAnimating(void)
{
    return gDevelopmentCardDrawAnimating;
}

float uiGetDevelopmentCardDrawAnimationProgress(void)
{
    if (!gDevelopmentCardDrawAnimating)
    {
        return 1.0f;
    }

    {
        const float progress = (float)((GetTime() - gDevelopmentCardDrawAnimationStartTime) / 0.90);
        if (progress < 0.0f)
        {
            return 0.0f;
        }
        if (progress > 1.0f)
        {
            return 1.0f;
        }
        return progress;
    }
}

enum DevelopmentCardType uiGetAnimatedDevelopmentCardType(void)
{
    return gAnimatedDevelopmentCardType;
}

bool uiIsBoardCreationAnimating(void)
{
    return uiGetBoardCreationAnimationProgress() < 1.0f;
}

float uiGetBoardCreationAnimationProgress(void)
{
    const float duration = 1.15f;
    const float elapsed = (float)(GetTime() - gBoardCreationAnimationStartTime);

    if (elapsed <= 0.0f)
    {
        return 0.0f;
    }
    if (elapsed >= duration)
    {
        return 1.0f;
    }
    return elapsed / duration;
}

float uiGetBoardUiFadeProgress(void)
{
    const float boardDuration = 1.15f;
    const float fadeDuration = 0.38f;
    const float elapsed = (float)(GetTime() - gBoardCreationAnimationStartTime);

    if (elapsed <= boardDuration)
    {
        return 0.0f;
    }
    if (elapsed >= boardDuration + fadeDuration)
    {
        return 1.0f;
    }
    return (elapsed - boardDuration) / fadeDuration;
}

int uiGetRecentRollHighlightValue(void)
{
    return gRecentRollHighlightValue;
}

float uiGetRecentRollHighlightProgress(void)
{
    if (gRecentRollHighlightStartTime < 0.0)
    {
        return 0.0f;
    }
    return 1.0f;
}

int uiGetPlayerNotificationCount(enum PlayerType player)
{
    int count = 0;
    if (player < PLAYER_RED || player > PLAYER_BLACK)
    {
        return 0;
    }

    for (int i = 0; i < PLAYER_NOTIFICATION_SLOTS; i++)
    {
        if (gPlayerNotifications[player][i].active)
        {
            count++;
        }
    }
    return count;
}

const char *uiGetPlayerNotificationText(enum PlayerType player, int index)
{
    int activeIndex = 0;
    if (player < PLAYER_RED || player > PLAYER_BLACK || index < 0)
    {
        return "";
    }

    for (int i = 0; i < PLAYER_NOTIFICATION_SLOTS; i++)
    {
        if (!gPlayerNotifications[player][i].active)
        {
            continue;
        }
        if (activeIndex == index)
        {
            return gPlayerNotifications[player][i].text;
        }
        activeIndex++;
    }
    return "";
}

enum UiNotificationTone uiGetPlayerNotificationTone(enum PlayerType player, int index)
{
    int activeIndex = 0;
    if (player < PLAYER_RED || player > PLAYER_BLACK || index < 0)
    {
        return UI_NOTIFICATION_NEUTRAL;
    }

    for (int i = 0; i < PLAYER_NOTIFICATION_SLOTS; i++)
    {
        if (!gPlayerNotifications[player][i].active)
        {
            continue;
        }
        if (activeIndex == index)
        {
            return gPlayerNotifications[player][i].tone;
        }
        activeIndex++;
    }
    return UI_NOTIFICATION_NEUTRAL;
}

float uiGetPlayerNotificationAge(enum PlayerType player, int index)
{
    int activeIndex = 0;
    if (player < PLAYER_RED || player > PLAYER_BLACK || index < 0)
    {
        return 999.0f;
    }

    for (int i = 0; i < PLAYER_NOTIFICATION_SLOTS; i++)
    {
        if (!gPlayerNotifications[player][i].active)
        {
            continue;
        }
        if (activeIndex == index)
        {
            return (float)(GetTime() - gPlayerNotifications[player][i].startTime);
        }
        activeIndex++;
    }
    return 999.0f;
}

bool uiIsPlayerNotificationDismissing(enum PlayerType player, int index)
{
    int activeIndex = 0;
    if (player < PLAYER_RED || player > PLAYER_BLACK || index < 0)
    {
        return false;
    }

    for (int i = 0; i < PLAYER_NOTIFICATION_SLOTS; i++)
    {
        if (!gPlayerNotifications[player][i].active)
        {
            continue;
        }
        if (activeIndex == index)
        {
            return gPlayerNotifications[player][i].dismissing;
        }
        activeIndex++;
    }
    return false;
}

float uiGetPlayerNotificationDismissProgress(enum PlayerType player, int index)
{
    int activeIndex = 0;
    if (player < PLAYER_RED || player > PLAYER_BLACK || index < 0)
    {
        return 1.0f;
    }

    for (int i = 0; i < PLAYER_NOTIFICATION_SLOTS; i++)
    {
        if (!gPlayerNotifications[player][i].active)
        {
            continue;
        }
        if (activeIndex == index)
        {
            if (!gPlayerNotifications[player][i].dismissing)
            {
                return 0.0f;
            }

            {
                const float progress = (float)((GetTime() - gPlayerNotifications[player][i].dismissStartTime) / 0.45);
                if (progress < 0.0f)
                {
                    return 0.0f;
                }
                if (progress > 1.0f)
                {
                    return 1.0f;
                }
                return progress;
            }
        }
        activeIndex++;
    }
    return 1.0f;
}

int uiGetCurrentTurnResourceGain(enum ResourceType resource)
{
    if (resource < RESOURCE_WOOD || resource > RESOURCE_STONE)
    {
        return 0;
    }
    return gCurrentTurnResourceGain[resource];
}

void uiRecordRoadPlacement(int ax, int ay, int bx, int by)
{
    gLastRoadAx = ax;
    gLastRoadAy = ay;
    gLastRoadBx = bx;
    gLastRoadBy = by;
    gLastRoadPlacementTime = GetTime();
}

float uiGetRoadPlacementPopAmount(int ax, int ay, int bx, int by)
{
    if (gLastRoadPlacementTime < 0.0)
    {
        return 0.0f;
    }

    const bool sameDirection = ax == gLastRoadAx && ay == gLastRoadAy && bx == gLastRoadBx && by == gLastRoadBy;
    const bool oppositeDirection = ax == gLastRoadBx && ay == gLastRoadBy && bx == gLastRoadAx && by == gLastRoadAy;
    if (!sameDirection && !oppositeDirection)
    {
        return 0.0f;
    }

    const double elapsed = GetTime() - gLastRoadPlacementTime;
    if (elapsed < 0.0 || elapsed > 0.22)
    {
        return 0.0f;
    }

    return 1.0f - (float)(elapsed / 0.22);
}

void uiRecordStructurePlacement(int x, int y)
{
    gLastStructureX = x;
    gLastStructureY = y;
    gLastStructurePlacementTime = GetTime();
}

float uiGetStructurePlacementPopAmount(int x, int y)
{
    if (gLastStructurePlacementTime < 0.0)
    {
        return 0.0f;
    }

    if (x != gLastStructureX || y != gLastStructureY)
    {
        return 0.0f;
    }

    const double elapsed = GetTime() - gLastStructurePlacementTime;
    if (elapsed < 0.0 || elapsed > 0.24)
    {
        return 0.0f;
    }

    return 1.0f - (float)(elapsed / 0.24);
}

static void update_build_panel_animation(void)
{
    const float target = gBuildPanelOpen ? 1.0f : 0.0f;
    const float speed = 8.0f * GetFrameTime();
    gBuildPanelOpenAmount += (target - gBuildPanelOpenAmount) * (speed > 1.0f ? 1.0f : speed);
    if (fabsf(gBuildPanelOpenAmount - target) < 0.01f)
    {
        gBuildPanelOpenAmount = target;
    }
}

static unsigned long long current_match_elapsed_seconds(void)
{
    double elapsed = 0.0;

    if (gCurrentMatchStartTime < 0.0)
    {
        return 0ULL;
    }

    elapsed = gCurrentMatchFrozenDurationSeconds >= 0.0
                  ? gCurrentMatchFrozenDurationSeconds
                  : (GetTime() - gCurrentMatchStartTime);

    if (elapsed <= 0.0)
    {
        return 0ULL;
    }

    return (unsigned long long)elapsed;
}

static void commit_match_result_if_needed(const struct Map *map)
{
    enum PlayerType localHuman = PLAYER_NONE;
    enum PlayerType winner = PLAYER_NONE;

    if (map == NULL || gCurrentMatchResultCommitted || !gameHasWinner(map))
    {
        return;
    }

    localHuman = local_human_player(map);
    winner = gameGetWinner(map);
    if (localHuman != PLAYER_NONE)
    {
        if (winner == localHuman)
        {
            gCommittedTotalWins++;
        }
        else
        {
            gCommittedTotalLosses++;
        }
    }

    gCurrentMatchResultCommitted = true;
}

static void update_development_purchase_confirm_animation(void)
{
    const float target = gDevelopmentPurchaseConfirmOpen ? 1.0f : 0.0f;
    const float speed = 10.0f * GetFrameTime();
    gDevelopmentPurchaseConfirmOpenAmount += (target - gDevelopmentPurchaseConfirmOpenAmount) * (speed > 1.0f ? 1.0f : speed);
    if (fabsf(gDevelopmentPurchaseConfirmOpenAmount - target) < 0.01f)
    {
        gDevelopmentPurchaseConfirmOpenAmount = target;
    }
}

static void update_development_play_confirm_animation(void)
{
    const float target = gDevelopmentPlayConfirmOpen ? 1.0f : 0.0f;
    const float speed = 10.0f * GetFrameTime();
    gDevelopmentPlayConfirmOpenAmount += (target - gDevelopmentPlayConfirmOpenAmount) * (speed > 1.0f ? 1.0f : speed);
    if (fabsf(gDevelopmentPlayConfirmOpenAmount - target) < 0.01f)
    {
        gDevelopmentPlayConfirmOpenAmount = target;
    }
}

static void update_trade_menu_animation(void)
{
    const float target = gTradeMenuOpen ? 1.0f : 0.0f;
    const float speed = 10.0f * GetFrameTime();
    gTradeMenuOpenAmount += (target - gTradeMenuOpenAmount) * (speed > 1.0f ? 1.0f : speed);
    if (fabsf(gTradeMenuOpenAmount - target) < 0.01f)
    {
        gTradeMenuOpenAmount = target;
    }
}

static void update_player_trade_menu_animation(void)
{
    const float target = gPlayerTradeMenuOpen ? 1.0f : 0.0f;
    const float speed = 10.0f * GetFrameTime();
    gPlayerTradeMenuOpenAmount += (target - gPlayerTradeMenuOpenAmount) * (speed > 1.0f ? 1.0f : speed);
    if (fabsf(gPlayerTradeMenuOpenAmount - target) < 0.01f)
    {
        gPlayerTradeMenuOpenAmount = target;
    }
}

static void update_settings_menu_animation(void)
{
    const float target = gSettingsMenuOpen ? 1.0f : 0.0f;
    const float speed = 10.0f * GetFrameTime();
    gSettingsMenuOpenAmount += (target - gSettingsMenuOpenAmount) * (speed > 1.0f ? 1.0f : speed);
    if (fabsf(gSettingsMenuOpenAmount - target) < 0.01f)
    {
        gSettingsMenuOpenAmount = target;
    }
}

static void update_dice_animation(struct Map *map)
{
    if (!gDiceRolling || map == NULL)
    {
        return;
    }

    const double now = GetTime();
    if (now - gLastDiceShuffleTime >= 0.06)
    {
        gDisplayedDieA = GetRandomValue(1, 6);
        gDisplayedDieB = GetRandomValue(1, 6);
        gLastDiceShuffleTime = now;
    }

    if (now - gDiceRollStartTime >= 0.85)
    {
        const double rollResolveStarted = GetTime();
        const int resolvedRoll = gPendingDieA + gPendingDieB;
        gDiceRolling = false;
        gDisplayedDieA = gPendingDieA;
        gDisplayedDieB = gPendingDieB;
        gRecentRollHighlightValue = resolvedRoll;
        gRecentRollHighlightStartTime = now;
        debugLog("UI", "dice animation resolve total=%d", resolvedRoll);
        if (!(matchSessionGetActiveMutable() != NULL && &matchSessionGetActiveMutable()->map == map
                  ? matchSessionSubmitAction(matchSessionGetActiveMutable(),
                                             &(struct GameAction){
                                                 .type = GAME_ACTION_ROLL_DICE,
                                                 .diceRoll = resolvedRoll},
                                             NULL,
                                             NULL)
                  : gameApplyAction(map,
                                    &(struct GameAction){
                                        .type = GAME_ACTION_ROLL_DICE,
                                        .diceRoll = resolvedRoll},
                                    NULL,
                                    NULL)))
        {
            debugLog("UI", "dice animation apply failed total=%d sessionHash=%u",
                     resolvedRoll,
                     matchSessionGetActive() != NULL ? matchSessionGetStateHash(matchSessionGetActive()) : 0u);
        }
        debugLog("UI", "dice animation resolved total=%d elapsed=%.3f pendingDiscards=%d thiefPlacement=%d thiefVictim=%d",
                 resolvedRoll,
                 GetTime() - rollResolveStarted,
                 gameHasPendingDiscards(map) ? 1 : 0,
                 gameNeedsThiefPlacement(map) ? 1 : 0,
                 gameNeedsThiefVictimSelection(map) ? 1 : 0);
    }
}

static void update_development_card_draw_animation(void)
{
    if (!gDevelopmentCardDrawAnimating)
    {
        return;
    }

    if (GetTime() - gDevelopmentCardDrawAnimationStartTime >= 0.90)
    {
        gDevelopmentCardDrawAnimating = false;
    }
}

static void update_player_delta_notifications(const struct Map *map)
{
    enum PlayerType localPlayer;
    int localPositive[5] = {0};
    int localNegative[5] = {0};
    bool localTurnStarted = false;
    bool showedStatus = false;

    if (map == NULL)
    {
        return;
    }

    localPlayer = local_human_player(map);
    prune_player_notifications();
    if (!gPlayerDeltaTrackerInitialized)
    {
        snapshot_player_delta_state(map);
        gTrackedCurrentPlayer = map->currentPlayer;
        memset(gCurrentTurnResourceGain, 0, sizeof(gCurrentTurnResourceGain));
        gPlayerDeltaTrackerInitialized = true;
        localTurnStarted = localPlayer != PLAYER_NONE && map->currentPlayer == localPlayer;
        if (localTurnStarted)
        {
            uiShowCenteredStatus(loc("Your turn."), UI_NOTIFICATION_NEUTRAL);
        }
        return;
    }

    if (map->currentPlayer != gTrackedCurrentPlayer)
    {
        dismiss_player_notifications();
        gTrackedCurrentPlayer = map->currentPlayer;
        memset(gCurrentTurnResourceGain, 0, sizeof(gCurrentTurnResourceGain));
        gRecentRollHighlightValue = 0;
        gRecentRollHighlightStartTime = -1.0;
        localTurnStarted = localPlayer != PLAYER_NONE && map->currentPlayer == localPlayer;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        {
            const int visibleVp = gameComputeVisibleVictoryPoints(map, (enum PlayerType)player);
            const int visibleVpDelta = visibleVp - gTrackedVisibleVictoryPoints[player];
            if (visibleVpDelta > 0)
            {
                char vpBuffer[32];
                snprintf(vpBuffer, sizeof(vpBuffer), "+%d%s", visibleVpDelta, loc("VP"));
                push_player_notification((enum PlayerType)player, vpBuffer, UI_NOTIFICATION_VICTORY);
            }
        }

        for (int resource = RESOURCE_STONE; resource >= RESOURCE_WOOD; resource--)
        {
            const int current = map->players[player].resources[resource];
            const int previous = gTrackedPlayerResources[player][resource];
            const int delta = current - previous;
            char segment[32];

            if (delta == 0)
            {
                continue;
            }

            snprintf(segment, sizeof(segment), "%s%d %s",
                     delta > 0 ? "+" : "",
                     delta,
                     resource_label((enum ResourceType)resource, false));

            if (player == map->currentPlayer)
            {
                gCurrentTurnResourceGain[resource] += delta;
            }
            if (player == localPlayer)
            {
                if (delta > 0)
                {
                    localPositive[resource] += delta;
                }
                else
                {
                    localNegative[resource] += -delta;
                }
            }

            if (delta > 0)
            {
                push_player_notification((enum PlayerType)player, segment, UI_NOTIFICATION_POSITIVE);
            }
            else
            {
                push_player_notification((enum PlayerType)player, segment, UI_NOTIFICATION_NEGATIVE);
            }
        }
    }

    if (localPlayer != PLAYER_NONE)
    {
        show_local_resource_feedback(map, localPlayer, localPositive, localNegative, localTurnStarted, &showedStatus);
    }

    if (localTurnStarted && !showedStatus)
    {
        uiShowCenteredStatus(loc("Your turn."), UI_NOTIFICATION_NEUTRAL);
    }
    snapshot_player_delta_state(map);
}

static void snapshot_player_delta_state(const struct Map *map)
{
    if (map == NULL)
    {
        return;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        for (int resource = 0; resource < 5; resource++)
        {
            gTrackedPlayerResources[player][resource] = map->players[player].resources[resource];
        }
        gTrackedVisibleVictoryPoints[player] = gameComputeVisibleVictoryPoints(map, (enum PlayerType)player);
    }
}

static void prune_player_notifications(void)
{
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        struct UiPlayerNotification compacted[PLAYER_NOTIFICATION_SLOTS] = {0};
        int writeIndex = 0;
        for (int i = 0; i < PLAYER_NOTIFICATION_SLOTS; i++)
        {
            if (!gPlayerNotifications[player][i].active)
            {
                continue;
            }
            if (gPlayerNotifications[player][i].dismissing &&
                GetTime() - gPlayerNotifications[player][i].dismissStartTime > 0.45)
            {
                continue;
            }
            compacted[writeIndex++] = gPlayerNotifications[player][i];
        }
        memcpy(gPlayerNotifications[player], compacted, sizeof(compacted));
    }
}

static void push_player_notification(enum PlayerType player, const char *text, enum UiNotificationTone tone)
{
    if (player < PLAYER_RED || player > PLAYER_BLACK || text == NULL || text[0] == '\0')
    {
        return;
    }

    for (int i = PLAYER_NOTIFICATION_SLOTS - 1; i > 0; i--)
    {
        gPlayerNotifications[player][i] = gPlayerNotifications[player][i - 1];
    }

    gPlayerNotifications[player][0].active = true;
    gPlayerNotifications[player][0].dismissing = false;
    gPlayerNotifications[player][0].tone = tone;
    gPlayerNotifications[player][0].startTime = GetTime();
    gPlayerNotifications[player][0].dismissStartTime = 0.0;
    snprintf(gPlayerNotifications[player][0].text, sizeof(gPlayerNotifications[player][0].text), "%s", text);
}

static void dismiss_player_notifications(void)
{
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        for (int i = 0; i < PLAYER_NOTIFICATION_SLOTS; i++)
        {
            if (!gPlayerNotifications[player][i].active || gPlayerNotifications[player][i].dismissing)
            {
                continue;
            }
            gPlayerNotifications[player][i].dismissing = true;
            gPlayerNotifications[player][i].dismissStartTime = GetTime();
        }
    }
}

static const char *resource_label(enum ResourceType resource, bool abbreviated)
{
    if (abbreviated)
    {
        switch (resource)
        {
        case RESOURCE_WOOD:
            return locResourceShort(RESOURCE_WOOD);
        case RESOURCE_WHEAT:
            return locResourceShort(RESOURCE_WHEAT);
        case RESOURCE_CLAY:
            return locResourceShort(RESOURCE_CLAY);
        case RESOURCE_SHEEP:
            return locResourceShort(RESOURCE_SHEEP);
        case RESOURCE_STONE:
            return locResourceShort(RESOURCE_STONE);
        default:
            return "";
        }
    }

    return locResourceName(resource);
}

static enum PlayerType local_human_player(const struct Map *map)
{
    enum PlayerType humanPlayer = PLAYER_NONE;
    int humanCount = 0;
    int aiCount = 0;

    if (map == NULL)
    {
        return PLAYER_NONE;
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (map->players[player].controlMode == PLAYER_CONTROL_AI)
        {
            aiCount++;
        }
        else
        {
            humanPlayer = (enum PlayerType)player;
            humanCount++;
        }
    }

    return aiCount > 0 && humanCount == 1 ? humanPlayer : PLAYER_NONE;
}

static void show_local_resource_feedback(const struct Map *map, enum PlayerType localPlayer,
                                         const int positiveDeltas[5], const int negativeDeltas[5],
                                         bool localTurnStarted, bool *showedStatus)
{
    int positiveTotal = 0;
    int negativeTotal = 0;
    int negativeKinds = 0;
    int lastNegativeResource = -1;
    char resourceText[128];
    char message[192];

    if (showedStatus != NULL)
    {
        *showedStatus = false;
    }

    if (map == NULL || localPlayer == PLAYER_NONE)
    {
        return;
    }

    for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
    {
        positiveTotal += positiveDeltas[resource];
        negativeTotal += negativeDeltas[resource];
        if (negativeDeltas[resource] > 0)
        {
            negativeKinds++;
            lastNegativeResource = resource;
        }
    }

    if (positiveTotal > 0 && negativeTotal == 0)
    {
        format_resource_delta_list(positiveDeltas, resourceText, sizeof(resourceText));
        snprintf(message, sizeof(message),
                 localTurnStarted ? loc("Your turn. You got %s.") : loc("You got %s."),
                 resourceText);
        uiShowCenteredStatus(message, UI_NOTIFICATION_POSITIVE);
        if (showedStatus != NULL)
        {
            *showedStatus = true;
        }
        return;
    }

    if (negativeTotal > 0 && positiveTotal == 0 &&
        map->currentPlayer != localPlayer &&
        !gameHasPendingDiscards(map))
    {
        if (negativeTotal == 1 && negativeKinds == 1 && lastNegativeResource >= RESOURCE_WOOD && lastNegativeResource <= RESOURCE_STONE)
        {
            snprintf(message, sizeof(message), loc("%s stole %s."),
                     map->currentPlayer >= PLAYER_RED && map->currentPlayer <= PLAYER_BLACK ? player_label(map->currentPlayer) : loc("A player"),
                     resource_label((enum ResourceType)lastNegativeResource, false));
        }
        else
        {
            format_resource_delta_list(negativeDeltas, resourceText, sizeof(resourceText));
            snprintf(message, sizeof(message), loc("Lost %s."), resourceText);
        }
        uiShowCenteredStatus(message, UI_NOTIFICATION_NEGATIVE);
        if (showedStatus != NULL)
        {
            *showedStatus = true;
        }
    }
}

static void format_resource_delta_list(const int deltas[5], char *buffer, size_t bufferSize)
{
    bool first = true;

    if (buffer == NULL || bufferSize == 0)
    {
        return;
    }

    buffer[0] = '\0';
    for (int resource = RESOURCE_WOOD; resource <= RESOURCE_STONE; resource++)
    {
        char segment[32];
        if (deltas[resource] <= 0)
        {
            continue;
        }

        snprintf(segment, sizeof(segment), "%s%d %s", first ? "" : ", ", deltas[resource], resource_label((enum ResourceType)resource, false));
        strncat(buffer, segment, bufferSize - strlen(buffer) - 1);
        first = false;
    }
}

static const char *player_label(enum PlayerType player)
{
    return locPlayerName(player);
}
