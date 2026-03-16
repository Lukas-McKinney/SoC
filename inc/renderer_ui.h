#ifndef RENDERER_UI_H
#define RENDERER_UI_H

#include "map.h"

#include <raylib.h>

enum BuildMode {
    BUILD_MODE_NONE,
    BUILD_MODE_ROAD,
    BUILD_MODE_SETTLEMENT,
    BUILD_MODE_CITY
};

extern enum BuildMode gBuildMode;
extern int gTradeGiveResource;
extern int gTradeReceiveResource;
extern int gTradeAmount;
extern int gPlayerTradeGiveResource;
extern int gPlayerTradeReceiveResource;
extern enum PlayerType gPlayerTradeTarget;
extern int gPlayerTradeGiveAmount;
extern int gPlayerTradeReceiveAmount;
extern int gDiscardSelection[5];
extern enum PlayerType gDiscardSelectionPlayer;
extern enum PlayerType gDiscardRevealPlayer;
extern enum PlayerType gThiefVictimRevealPlayer;
extern enum ResourceType gDevelopmentPlayPrimaryResource;
extern enum ResourceType gDevelopmentPlaySecondaryResource;

/* Draws UI text with the renderer's shared font and spacing settings. */
void DrawUiText(const char *text, float x, float y, int fontSize, Color color);

/* Measures UI text using the renderer's shared font and spacing settings. */
int MeasureUiText(const char *text, int fontSize);

/* Returns the consistent player color used across the board and HUD. */
Color PlayerColor(enum PlayerType player);

void DrawBuildPanel(const struct Map *map);
void DrawTradeButton(const struct Map *map);
void DrawTradeModal(const struct Map *map);
void DrawPlayerTradeButton(const struct Map *map);
void DrawPlayerTradeModal(const struct Map *map);
void DrawIncomingTradeOfferModal(const struct Map *map);
void DrawSettingsButton(void);
void DrawSettingsModal(void);
void DrawDiscardModal(const struct Map *map);
void DrawThiefVictimModal(const struct Map *map);
void DrawAwardCards(const struct Map *map);
void DrawOpponentVictoryBar(const struct Map *map);
void DrawPlayerPanel(const struct Map *map);
void DrawDevelopmentHand(const struct Map *map);
bool GetHoveredDevelopmentHandCard(const struct Map *map, enum DevelopmentCardType *type);
void DrawDevelopmentCardDrawAnimation(const struct Map *map);
void DrawDevelopmentPurchaseOverlay(const struct Map *map);
void DrawDevelopmentPlayOverlay(const struct Map *map);
void DrawCenteredStatus(void);
void DrawCenteredWarning(void);
void DrawTurnPanel(const struct Map *map);
void DrawVictoryOverlay(const struct Map *map);

Rectangle GetPlayerPanelBounds(void);
Rectangle GetTurnPanelBounds(void);
Rectangle GetTradeButtonBounds(void);
Rectangle GetPlayerTradeButtonBounds(void);
Rectangle GetTradeModalBounds(void);
Rectangle GetPlayerTradeModalBounds(void);
Rectangle GetIncomingTradeOfferModalBounds(void);
Rectangle GetIncomingTradeOfferAcceptButtonBounds(void);
Rectangle GetIncomingTradeOfferDeclineButtonBounds(void);
Rectangle GetSettingsButtonBounds(void);
Rectangle GetSettingsModalBounds(void);
Rectangle GetDiscardModalBounds(void);
Rectangle GetThiefVictimModalBounds(void);
Rectangle GetRollDiceButtonBounds(void);
Rectangle GetEndTurnButtonBounds(void);
Rectangle GetBuildPanelBounds(void);
Rectangle GetBuildPanelDevelopmentCardBounds(void);
Rectangle GetDevelopmentPurchaseOverlayBounds(void);
Rectangle GetDevelopmentPurchaseConfirmButtonBounds(void);
Rectangle GetDevelopmentPurchaseCancelButtonBounds(void);
Rectangle GetDevelopmentPlayOverlayBounds(void);
Rectangle GetDevelopmentPlayConfirmButtonBounds(void);
Rectangle GetDevelopmentPlayCancelButtonBounds(void);
Rectangle GetVictoryOverlayBounds(void);
Rectangle GetVictoryOverlayRestartButtonBounds(void);
Rectangle GetVictoryOverlayMenuButtonBounds(void);

#endif
