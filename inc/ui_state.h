#ifndef UI_STATE_H
#define UI_STATE_H

#include <stdbool.h>

#include "map.h"

enum UiTheme {
    UI_THEME_LIGHT,
    UI_THEME_DARK
};

enum UiNotificationTone {
    UI_NOTIFICATION_NEUTRAL,
    UI_NOTIFICATION_POSITIVE,
    UI_NOTIFICATION_NEGATIVE,
    UI_NOTIFICATION_VICTORY
};

enum UiSettingsConfirmAction {
    UI_SETTINGS_CONFIRM_NONE,
    UI_SETTINGS_CONFIRM_MAIN_MENU,
    UI_SETTINGS_CONFIRM_RESTART,
    UI_SETTINGS_CONFIRM_QUIT
};

void initUiState(void);
void uiResetForNewGame(void);
void uiBeginMatch(void);
void updateUiState(struct Map *map);
void handleUiGlobalInput(void);

void uiToggleBuildPanel(void);
void uiSetBuildPanelOpen(bool open);
bool uiIsBuildPanelOpen(void);
float uiGetBuildPanelOpenAmount(void);
void uiSetDevelopmentPurchaseConfirmOpen(bool open);
bool uiIsDevelopmentPurchaseConfirmOpen(void);
float uiGetDevelopmentPurchaseConfirmOpenAmount(void);
void uiSetDevelopmentPlayConfirmOpen(bool open);
bool uiIsDevelopmentPlayConfirmOpen(void);
float uiGetDevelopmentPlayConfirmOpenAmount(void);
void uiSetDevelopmentPlayCardType(enum DevelopmentCardType type);
enum DevelopmentCardType uiGetDevelopmentPlayCardType(void);
void uiShowCenteredWarning(const char *text);
bool uiHasCenteredWarning(void);
const char *uiGetCenteredWarningText(void);
float uiGetCenteredWarningAlpha(void);
float uiGetCenteredWarningVerticalOffset(void);
void uiShowCenteredStatus(const char *text, enum UiNotificationTone tone);
bool uiHasCenteredStatus(void);
const char *uiGetCenteredStatusText(void);
enum UiNotificationTone uiGetCenteredStatusTone(void);
float uiGetCenteredStatusAlpha(void);
float uiGetCenteredStatusVerticalOffset(void);

void uiToggleTradeMenu(void);
void uiSetTradeMenuOpen(bool open);
bool uiIsTradeMenuOpen(void);
float uiGetTradeMenuOpenAmount(void);
void uiTogglePlayerTradeMenu(void);
void uiSetPlayerTradeMenuOpen(bool open);
bool uiIsPlayerTradeMenuOpen(void);
float uiGetPlayerTradeMenuOpenAmount(void);

void uiToggleSettingsMenu(void);
void uiSetSettingsMenuOpen(bool open);
bool uiIsSettingsMenuOpen(void);
float uiGetSettingsMenuOpenAmount(void);
void uiToggleSettingsMultiplayerInfoExpanded(void);
void uiSetSettingsMultiplayerInfoExpanded(bool expanded);
bool uiIsSettingsMultiplayerInfoExpanded(void);
void uiSetTheme(enum UiTheme theme);
enum UiTheme uiGetTheme(void);
void uiSetAiSpeedSetting(int speed);
int uiGetAiSpeedSetting(void);
void uiSetSettingsConfirmAction(enum UiSettingsConfirmAction action);
enum UiSettingsConfirmAction uiGetSettingsConfirmAction(void);
void uiRequestReturnToMainMenu(void);
bool uiConsumeReturnToMainMenuRequest(void);
void uiRequestRestartGame(void);
bool uiConsumeRestartGameRequest(void);
void uiRequestQuitGame(void);
bool uiConsumeQuitGameRequest(void);

void uiSetPersistedTotalPlaytimeSeconds(unsigned long long seconds);
void uiSetPersistedMatchRecord(unsigned long long wins, unsigned long long losses);
unsigned long long uiGetCurrentMatchPlaytimeSeconds(void);
unsigned long long uiGetTotalPlaytimeSeconds(void);
unsigned long long uiGetTotalWins(void);
unsigned long long uiGetTotalLosses(void);

void uiStartDiceRollAnimation(void);
bool uiIsDiceRolling(void);
int uiGetDisplayedDieA(void);
int uiGetDisplayedDieB(void);
void uiStartDevelopmentCardDrawAnimation(enum DevelopmentCardType type);
bool uiIsDevelopmentCardDrawAnimating(void);
float uiGetDevelopmentCardDrawAnimationProgress(void);
enum DevelopmentCardType uiGetAnimatedDevelopmentCardType(void);
bool uiIsBoardCreationAnimating(void);
float uiGetBoardCreationAnimationProgress(void);
float uiGetBoardUiFadeProgress(void);
int uiGetRecentRollHighlightValue(void);
float uiGetRecentRollHighlightProgress(void);
int uiGetPlayerNotificationCount(enum PlayerType player);
const char *uiGetPlayerNotificationText(enum PlayerType player, int index);
enum UiNotificationTone uiGetPlayerNotificationTone(enum PlayerType player, int index);
float uiGetPlayerNotificationAge(enum PlayerType player, int index);
bool uiIsPlayerNotificationDismissing(enum PlayerType player, int index);
float uiGetPlayerNotificationDismissProgress(enum PlayerType player, int index);
int uiGetCurrentTurnResourceGain(enum ResourceType resource);

void uiRecordRoadPlacement(int ax, int ay, int bx, int by);
float uiGetRoadPlacementPopAmount(int ax, int ay, int bx, int by);

void uiRecordStructurePlacement(int x, int y);
float uiGetStructurePlacementPopAmount(int x, int y);

#endif
