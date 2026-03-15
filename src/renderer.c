#include "ai_controller.h"
#include "board_rules.h"
#include "game_logic.h"
#include "renderer.h"
#include "renderer_internal.h"
#include "renderer_ui.h"
#include "ui_state.h"

#include <math.h>
#include <raylib.h>
#include <stdio.h>

const struct AxialCoord kLandCoords[LAND_TILE_COUNT] = {
    {0, -2}, {1, -2}, {2, -2}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}, {-2, 2}, {-1, 2}, {0, 2}};

const struct PortVisual kPorts[PORT_VISUAL_COUNT] = {
    {{0, -3}, {0, -2}, true, RESOURCE_WOOD},
    {{2, -3}, {2, -2}, false, RESOURCE_WHEAT},
    {{3, -2}, {2, -1}, true, RESOURCE_WOOD},
    {{3, 0}, {2, 0}, false, RESOURCE_STONE},
    {{1, 2}, {1, 1}, true, RESOURCE_WOOD},
    {{-1, 3}, {-1, 2}, false, RESOURCE_CLAY},
    {{-3, 2}, {-2, 1}, true, RESOURCE_WOOD},
    {{-3, 0}, {-2, 0}, false, RESOURCE_SHEEP},
    {{-1, -2}, {-1, -1}, false, RESOURCE_WOOD}};

int gHoveredTileId = -1;
int gHoveredSideIndex = -1;
int gHoveredCornerTileId = -1;
int gHoveredCornerIndex = -1;
enum BuildMode gBuildMode = BUILD_MODE_NONE;
int gTradeGiveResource = RESOURCE_WOOD;
int gTradeReceiveResource = RESOURCE_WHEAT;
int gTradeAmount = 1;
int gPlayerTradeGiveResource = RESOURCE_WOOD;
int gPlayerTradeReceiveResource = RESOURCE_WHEAT;
enum PlayerType gPlayerTradeTarget = PLAYER_BLUE;
int gPlayerTradeGiveAmount = 1;
int gPlayerTradeReceiveAmount = 1;
int gDiscardSelection[5] = {0};
enum PlayerType gDiscardSelectionPlayer = PLAYER_NONE;
enum PlayerType gDiscardRevealPlayer = PLAYER_NONE;
enum PlayerType gThiefVictimRevealPlayer = PLAYER_NONE;
enum ResourceType gDevelopmentPlayPrimaryResource = RESOURCE_WOOD;
enum ResourceType gDevelopmentPlaySecondaryResource = RESOURCE_WHEAT;

#define UI_FONT_SPACING(fontSize) ((float)(fontSize) * 0.04f)

static bool IsMouseNearTile(Vector2 mouse, Vector2 center, float radius);
static float DistancePointToSegment(Vector2 p, Vector2 a, Vector2 b);
static void DrawBeachWedge(Vector2 center, float radius, int sideIndex);
static void DrawHarborMarker(const struct PortVisual *port, Vector2 center, float radius, int sideIndex);
static int FindPortSideIndex(const struct PortVisual *port);
static const char *PortLabel(const struct PortVisual *port);
static void DrawPipRow(Vector2 center, int pipCount, float spacing, float dotRadius, Color color);
Color PlayerColor(enum PlayerType player);
static bool EdgeKeysMatch(int ax1, int ay1, int bx1, int by1, int ax2, int ay2, int bx2, int by2);
static void CanonicalizeHoveredEdge(void);
static bool CornerKeysMatch(int x1, int y1, int x2, int y2);
static void CanonicalizeHoveredCorner(void);
static void NormalizePlayerTradeSelection(const struct Map *map);
static enum PlayerType LocalHumanPlayer(const struct Map *map);
static const char *PlayerNameLabel(enum PlayerType player);
static bool HasAnyValidRoadPlacement(const struct Map *map, Vector2 origin, float radius);
static bool CanOpenDevelopmentPlayOverlay(const struct Map *map, enum DevelopmentCardType type, Vector2 origin, float radius);

static void NormalizePlayerTradeSelection(const struct Map *map)
{
    if (map == NULL || map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
    {
        return;
    }

    if (gPlayerTradeTarget < PLAYER_RED || gPlayerTradeTarget > PLAYER_BLACK || gPlayerTradeTarget == map->currentPlayer)
    {
        for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
        {
            if (player != map->currentPlayer)
            {
                gPlayerTradeTarget = (enum PlayerType)player;
                break;
            }
        }
    }

    int firstGiveResource = -1;
    for (int resource = 0; resource < 5; resource++)
    {
        if (map->players[map->currentPlayer].resources[resource] > 0)
        {
            firstGiveResource = resource;
            break;
        }
    }

    if (firstGiveResource >= 0 && map->players[map->currentPlayer].resources[gPlayerTradeGiveResource] <= 0)
    {
        gPlayerTradeGiveResource = firstGiveResource;
    }

    if (gPlayerTradeReceiveResource == gPlayerTradeGiveResource)
    {
        for (int resource = 0; resource < 5; resource++)
        {
            if (resource != gPlayerTradeGiveResource)
            {
                gPlayerTradeReceiveResource = resource;
                break;
            }
        }
    }

    if (map->players[map->currentPlayer].resources[gPlayerTradeGiveResource] > 0 &&
        gPlayerTradeGiveAmount > map->players[map->currentPlayer].resources[gPlayerTradeGiveResource])
    {
        gPlayerTradeGiveAmount = map->players[map->currentPlayer].resources[gPlayerTradeGiveResource];
    }
    if (gPlayerTradeReceiveAmount > 9)
    {
        gPlayerTradeReceiveAmount = 9;
    }
    if (gPlayerTradeGiveAmount < 1)
    {
        gPlayerTradeGiveAmount = 1;
    }
    if (gPlayerTradeReceiveAmount < 1)
    {
        gPlayerTradeReceiveAmount = 1;
    }
}

static enum PlayerType LocalHumanPlayer(const struct Map *map)
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

static const char *PlayerNameLabel(enum PlayerType player)
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
    case PLAYER_NONE:
    default:
        return "Player";
    }
}

static bool HasAnyValidRoadPlacement(const struct Map *map, Vector2 origin, float radius)
{
    if (map == NULL || map->currentPlayer < PLAYER_RED || map->currentPlayer > PLAYER_BLACK)
    {
        return false;
    }

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            if (!IsCanonicalSharedEdge(tileId, sideIndex) ||
                IsSharedEdgeOccupied(map, tileId, sideIndex))
            {
                continue;
            }

            if (boardIsValidRoadPlacement(map, tileId, sideIndex, map->currentPlayer, origin, radius))
            {
                return true;
            }
        }
    }

    return false;
}

static bool CanOpenDevelopmentPlayOverlay(const struct Map *map, enum DevelopmentCardType type, Vector2 origin, float radius)
{
    if (!gameCanPlayDevelopmentCard(map, type))
    {
        return false;
    }

    if (type == DEVELOPMENT_CARD_ROAD_BUILDING)
    {
        return HasAnyValidRoadPlacement(map, origin, radius);
    }

    return true;
}

void HandleMapInput(struct Map *map)
{
    if (!gAssetsLoaded || map == NULL)
    {
        return;
    }

    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const Vector2 mouse = GetMousePosition();
    float bestDistance = 22.0f;
    const Rectangle buildPanel = GetBuildPanelBounds();
    const Rectangle buildHeaderToggle = {
        buildPanel.x,
        buildPanel.y,
        buildPanel.width,
        46.0f};
    const Rectangle buildButton = {
        buildPanel.x + 18.0f,
        buildPanel.y + 52.0f,
        166.0f,
        56.0f};
    const Rectangle settlementButton = {
        buildPanel.x + 204.0f,
        buildPanel.y + 52.0f,
        170.0f,
        56.0f};
    const Rectangle cityButton = {
        buildPanel.x + 18.0f,
        buildPanel.y + 118.0f,
        170.0f,
        56.0f};
    const Rectangle devButton = {
        buildPanel.x + 204.0f,
        buildPanel.y + 118.0f,
        170.0f,
        56.0f};
    const Rectangle developmentPurchaseOverlay = GetDevelopmentPurchaseOverlayBounds();
    const Rectangle developmentPurchaseConfirmButton = GetDevelopmentPurchaseConfirmButtonBounds();
    const Rectangle developmentPurchaseCancelButton = GetDevelopmentPurchaseCancelButtonBounds();
    const Rectangle developmentPlayOverlay = GetDevelopmentPlayOverlayBounds();
    const Rectangle developmentPlayConfirmButton = GetDevelopmentPlayConfirmButtonBounds();
    const Rectangle developmentPlayCancelButton = GetDevelopmentPlayCancelButtonBounds();
    const Rectangle rollDiceButton = GetRollDiceButtonBounds();
    const Rectangle endTurnButton = GetEndTurnButtonBounds();
    const Rectangle tradeButton = GetTradeButtonBounds();
    const Rectangle playerTradeButton = GetPlayerTradeButtonBounds();
    const Rectangle tradeModal = GetTradeModalBounds();
    const Rectangle playerTradeModal = GetPlayerTradeModalBounds();
    const Rectangle settingsButton = GetSettingsButtonBounds();
    const Rectangle settingsModal = GetSettingsModalBounds();
    const Rectangle discardModal = GetDiscardModalBounds();
    const Rectangle thiefVictimModal = GetThiefVictimModalBounds();
    const Rectangle victoryOverlay = GetVictoryOverlayBounds();
    const Rectangle victoryRestartButton = GetVictoryOverlayRestartButtonBounds();
    const Rectangle victoryMenuButton = GetVictoryOverlayMenuButtonBounds();
    const bool setupSettlementMode = gameIsSetupSettlementTurn(map);
    const bool setupRoadMode = gameIsSetupRoadTurn(map);
    const bool hasPendingDiscards = gameHasPendingDiscards(map);
    const bool needsThiefPlacement = gameNeedsThiefPlacement(map);
    const bool needsThiefVictimSelection = gameNeedsThiefVictimSelection(map);
    const bool hasWinner = gameHasWinner(map);
    const bool aiControlledDecision = aiControlsActiveDecision(map);
    const bool canBuyRoad = setupRoadMode || gameCanAffordRoad(map);
    const bool canBuySettlement = setupSettlementMode || gameCanAffordSettlement(map);
    const bool canBuyCity = map->phase == GAME_PHASE_PLAY && gameCanAffordCity(map);
    const bool canBuyDevelopment = gameCanBuyDevelopment(map);
    const bool hasFreeRoadPlacements = gameHasFreeRoadPlacements(map);
    const Vector2 boardOrigin = origin;

    if (!hasPendingDiscards)
    {
        gDiscardSelectionPlayer = PLAYER_NONE;
        gDiscardRevealPlayer = PLAYER_NONE;
        for (int resource = 0; resource < 5; resource++)
        {
            gDiscardSelection[resource] = 0;
        }
    }
    if (!needsThiefVictimSelection)
    {
        gThiefVictimRevealPlayer = PLAYER_NONE;
    }

    if (hasPendingDiscards)
    {
        const enum PlayerType discardPlayer = gameGetCurrentDiscardPlayer(map);
        const enum PlayerType localHuman = LocalHumanPlayer(map);
        const int discardRequired = gameGetDiscardAmountForPlayer(map, discardPlayer);
        int discardSelected = 0;
        if (gDiscardSelectionPlayer != discardPlayer)
        {
            gDiscardSelectionPlayer = discardPlayer;
            gDiscardRevealPlayer = map->players[discardPlayer].controlMode == PLAYER_CONTROL_AI ||
                                           (discardPlayer != map->currentPlayer && discardPlayer != localHuman)
                                       ? PLAYER_NONE
                                       : discardPlayer;
            for (int resource = 0; resource < 5; resource++)
            {
                gDiscardSelection[resource] = 0;
            }
        }

        if (map->players[discardPlayer].controlMode == PLAYER_CONTROL_AI)
        {
            return;
        }

        if (gDiscardRevealPlayer != discardPlayer)
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, discardModal))
            {
                gDiscardRevealPlayer = discardPlayer;
            }
            return;
        }

        for (int resource = 0; resource < 5; resource++)
        {
            discardSelected += gDiscardSelection[resource];
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, discardModal))
        {
            for (int resource = 0; resource < 5; resource++)
            {
                Rectangle minusButton = {discardModal.x + 182.0f, discardModal.y + 92.0f + resource * 38.0f, 26.0f, 26.0f};
                Rectangle plusButton = {discardModal.x + 322.0f, discardModal.y + 92.0f + resource * 38.0f, 26.0f, 26.0f};
                if (CheckCollisionPointRec(mouse, minusButton) && gDiscardSelection[resource] > 0)
                {
                    gDiscardSelection[resource]--;
                    return;
                }
                if (CheckCollisionPointRec(mouse, plusButton) &&
                    discardSelected < discardRequired &&
                    gDiscardSelection[resource] < map->players[discardPlayer].resources[resource])
                {
                    gDiscardSelection[resource]++;
                    return;
                }
            }

            Rectangle confirmButton = {discardModal.x + 38.0f, discardModal.y + discardModal.height - 52.0f, discardModal.width - 76.0f, 36.0f};
            if (CheckCollisionPointRec(mouse, confirmButton) &&
                discardSelected == discardRequired &&
                gameTrySubmitDiscard(map, discardPlayer, gDiscardSelection))
            {
                gDiscardSelectionPlayer = PLAYER_NONE;
                gDiscardRevealPlayer = PLAYER_NONE;
                for (int resource = 0; resource < 5; resource++)
                {
                    gDiscardSelection[resource] = 0;
                }
                return;
            }
        }
        return;
    }

    if (needsThiefVictimSelection)
    {
        if (map->players[map->currentPlayer].controlMode == PLAYER_CONTROL_AI)
        {
            gThiefVictimRevealPlayer = PLAYER_NONE;
            return;
        }

        if (gThiefVictimRevealPlayer != map->currentPlayer)
        {
            gThiefVictimRevealPlayer = map->currentPlayer;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, thiefVictimModal))
        {
            int victimIndex = 0;
            for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
            {
                enum PlayerType victim = (enum PlayerType)player;
                if (!gameCanStealFromPlayer(map, victim))
                {
                    continue;
                }

                Rectangle victimButton = {thiefVictimModal.x + 26.0f + victimIndex * 118.0f, thiefVictimModal.y + 102.0f, 102.0f, 42.0f};
                if (CheckCollisionPointRec(mouse, victimButton) &&
                    gameStealRandomResource(map, victim))
                {
                    gThiefVictimRevealPlayer = PLAYER_NONE;
                    return;
                }
                victimIndex++;
            }
        }
        return;
    }

    if (!hasWinner && uiGetSettingsMenuOpenAmount() > 0.98f)
    {
        const enum UiSettingsConfirmAction confirmAction = uiGetSettingsConfirmAction();
        Rectangle lightButton = {settingsModal.x + 24.0f, settingsModal.y + 82.0f, settingsModal.width - 48.0f, 42.0f};
        Rectangle darkButton = {settingsModal.x + 24.0f, settingsModal.y + 136.0f, settingsModal.width - 48.0f, 42.0f};
        Rectangle aiSpeedSlider = {settingsModal.x + 28.0f, settingsModal.y + 208.0f, settingsModal.width - 56.0f, 36.0f};
        Rectangle restartButton = {settingsModal.x + 24.0f, settingsModal.y + 292.0f, settingsModal.width - 48.0f, 42.0f};
        Rectangle backToMenuButton = {settingsModal.x + 24.0f, settingsModal.y + 346.0f, settingsModal.width - 48.0f, 42.0f};
        Rectangle quitButton = {settingsModal.x + 24.0f, settingsModal.y + 400.0f, settingsModal.width - 48.0f, 42.0f};
        Rectangle confirmPanel = {settingsModal.x + 26.0f, settingsModal.y + 274.0f, settingsModal.width - 52.0f, 140.0f};
        Rectangle confirmButton = {confirmPanel.x + 18.0f, confirmPanel.y + confirmPanel.height - 46.0f, 132.0f, 30.0f};
        Rectangle cancelButton = {confirmPanel.x + confirmPanel.width - 110.0f, confirmPanel.y + confirmPanel.height - 46.0f, 92.0f, 30.0f};
        Rectangle closeButton = {settingsModal.x + settingsModal.width - 42.0f, settingsModal.y + 12.0f, 28.0f, 28.0f};
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, settingsModal))
        {
            uiSetSettingsMenuOpen(false);
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, closeButton))
        {
            uiSetSettingsMenuOpen(false);
            return;
        }
        if (confirmAction != UI_SETTINGS_CONFIRM_NONE)
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, confirmButton))
            {
                uiSetSettingsConfirmAction(UI_SETTINGS_CONFIRM_NONE);
                if (confirmAction == UI_SETTINGS_CONFIRM_MAIN_MENU)
                {
                    uiRequestReturnToMainMenu();
                }
                else if (confirmAction == UI_SETTINGS_CONFIRM_RESTART)
                {
                    uiRequestRestartGame();
                }
                else if (confirmAction == UI_SETTINGS_CONFIRM_QUIT)
                {
                    uiRequestQuitGame();
                }
                return;
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                (CheckCollisionPointRec(mouse, cancelButton) ||
                 !CheckCollisionPointRec(mouse, confirmPanel)))
            {
                uiSetSettingsConfirmAction(UI_SETTINGS_CONFIRM_NONE);
                return;
            }
            return;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, aiSpeedSlider))
        {
            const float normalized = (mouse.x - aiSpeedSlider.x) / aiSpeedSlider.width;
            uiSetAiSpeedSetting((int)roundf(normalized * 10.0f));
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, lightButton))
        {
            uiSetTheme(UI_THEME_LIGHT);
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, darkButton))
        {
            uiSetTheme(UI_THEME_DARK);
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, restartButton))
        {
            uiSetSettingsConfirmAction(UI_SETTINGS_CONFIRM_RESTART);
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, backToMenuButton))
        {
            uiSetSettingsConfirmAction(UI_SETTINGS_CONFIRM_MAIN_MENU);
            return;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, quitButton))
        {
            uiSetSettingsConfirmAction(UI_SETTINGS_CONFIRM_QUIT);
            return;
        }
    }

    if (uiIsDevelopmentPurchaseConfirmOpen() && !canBuyDevelopment)
    {
        uiSetDevelopmentPurchaseConfirmOpen(false);
    }

    if (uiIsDevelopmentPurchaseConfirmOpen() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse, developmentPurchaseConfirmButton) && canBuyDevelopment)
        {
            enum DevelopmentCardType drawnCard;
            uiSetDevelopmentPurchaseConfirmOpen(false);
            if (gameTryBuyDevelopment(map, &drawnCard))
            {
                uiStartDevelopmentCardDrawAnimation(drawnCard);
                if (gameCheckVictory(map, map->currentPlayer))
                {
                    uiSetBuildPanelOpen(false);
                    uiSetTradeMenuOpen(false);
                    uiSetPlayerTradeMenuOpen(false);
                }
            }
            return;
        }

        if (CheckCollisionPointRec(mouse, developmentPurchaseCancelButton) ||
            !CheckCollisionPointRec(mouse, developmentPurchaseOverlay))
        {
            uiSetDevelopmentPurchaseConfirmOpen(false);
            return;
        }

        return;
    }

    if (uiIsDevelopmentPlayConfirmOpen() &&
        !CanOpenDevelopmentPlayOverlay(map, uiGetDevelopmentPlayCardType(), boardOrigin, radius))
    {
        uiSetDevelopmentPlayConfirmOpen(false);
    }

    if (uiIsDevelopmentPlayConfirmOpen() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        const enum DevelopmentCardType type = uiGetDevelopmentPlayCardType();
        const float buttonWidth = 76.0f;
        const float buttonHeight = 36.0f;
        const float buttonGap = 8.0f;
        const float rowWidth = buttonWidth * 5.0f + buttonGap * 4.0f;
        const float rowX = developmentPlayOverlay.x + developmentPlayOverlay.width * 0.5f - rowWidth * 0.5f;

        if (type == DEVELOPMENT_CARD_YEAR_OF_PLENTY)
        {
            for (int resource = 0; resource < 5; resource++)
            {
                const Rectangle firstButton = {rowX + resource * (buttonWidth + buttonGap), developmentPlayOverlay.y + 132.0f, buttonWidth, buttonHeight};
                const Rectangle secondButton = {rowX + resource * (buttonWidth + buttonGap), developmentPlayOverlay.y + 184.0f, buttonWidth, buttonHeight};
                if (CheckCollisionPointRec(mouse, firstButton))
                {
                    gDevelopmentPlayPrimaryResource = (enum ResourceType)resource;
                    return;
                }
                if (CheckCollisionPointRec(mouse, secondButton))
                {
                    gDevelopmentPlaySecondaryResource = (enum ResourceType)resource;
                    return;
                }
            }
        }
        else if (type == DEVELOPMENT_CARD_MONOPOLY)
        {
            for (int resource = 0; resource < 5; resource++)
            {
                const Rectangle resourceButton = {rowX + resource * (buttonWidth + buttonGap), developmentPlayOverlay.y + 144.0f, buttonWidth, buttonHeight};
                if (CheckCollisionPointRec(mouse, resourceButton))
                {
                    gDevelopmentPlayPrimaryResource = (enum ResourceType)resource;
                    return;
                }
            }
        }

        if (CheckCollisionPointRec(mouse, developmentPlayConfirmButton) &&
            CanOpenDevelopmentPlayOverlay(map, type, boardOrigin, radius))
        {
            bool played = false;
            uiSetDevelopmentPlayConfirmOpen(false);
            if (type == DEVELOPMENT_CARD_KNIGHT)
            {
                played = gameTryPlayKnight(map);
                if (played && gameCheckVictory(map, map->currentPlayer))
                {
                    uiSetBuildPanelOpen(false);
                    uiSetTradeMenuOpen(false);
                    uiSetPlayerTradeMenuOpen(false);
                }
            }
            else if (type == DEVELOPMENT_CARD_ROAD_BUILDING)
            {
                played = gameTryPlayRoadBuilding(map);
                if (played)
                {
                    gBuildMode = BUILD_MODE_ROAD;
                    uiSetBuildPanelOpen(true);
                    if (!HasAnyValidRoadPlacement(map, boardOrigin, radius))
                    {
                        gBuildMode = BUILD_MODE_NONE;
                    }
                }
            }
            else if (type == DEVELOPMENT_CARD_YEAR_OF_PLENTY)
            {
                played = gameTryPlayYearOfPlenty(map, gDevelopmentPlayPrimaryResource, gDevelopmentPlaySecondaryResource);
            }
            else if (type == DEVELOPMENT_CARD_MONOPOLY)
            {
                played = gameTryPlayMonopoly(map, gDevelopmentPlayPrimaryResource);
            }

            if (played)
            {
                uiSetDevelopmentPurchaseConfirmOpen(false);
                uiSetTradeMenuOpen(false);
                uiSetPlayerTradeMenuOpen(false);
            }
            return;
        }

        if (CheckCollisionPointRec(mouse, developmentPlayCancelButton) ||
            !CheckCollisionPointRec(mouse, developmentPlayOverlay))
        {
            uiSetDevelopmentPlayConfirmOpen(false);
            return;
        }

        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        ((!uiIsBuildPanelOpen() && CheckCollisionPointRec(mouse, buildPanel)) ||
         (uiIsBuildPanelOpen() && CheckCollisionPointRec(mouse, buildHeaderToggle) &&
          !CheckCollisionPointRec(mouse, buildButton) &&
          !CheckCollisionPointRec(mouse, settlementButton) &&
          !CheckCollisionPointRec(mouse, cityButton) &&
          !CheckCollisionPointRec(mouse, devButton))))
    {
        uiToggleBuildPanel();
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, settingsButton))
    {
        uiToggleSettingsMenu();
        return;
    }

    if (uiGetBoardUiFadeProgress() < 1.0f)
    {
        uiSetBuildPanelOpen(false);
        uiSetDevelopmentPurchaseConfirmOpen(false);
        uiSetDevelopmentPlayConfirmOpen(false);
        uiSetTradeMenuOpen(false);
        uiSetPlayerTradeMenuOpen(false);
        gBuildMode = BUILD_MODE_NONE;
        gHoveredTileId = -1;
        gHoveredSideIndex = -1;
        gHoveredCornerTileId = -1;
        gHoveredCornerIndex = -1;
        return;
    }

    if (hasWinner)
    {
        uiSetSettingsMenuOpen(false);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, victoryOverlay))
        {
            if (CheckCollisionPointRec(mouse, victoryRestartButton))
            {
                uiRequestRestartGame();
            }
            else if (CheckCollisionPointRec(mouse, victoryMenuButton))
            {
                uiRequestReturnToMainMenu();
            }
        }
        uiSetBuildPanelOpen(false);
        uiSetDevelopmentPurchaseConfirmOpen(false);
        uiSetDevelopmentPlayConfirmOpen(false);
        uiSetTradeMenuOpen(false);
        uiSetPlayerTradeMenuOpen(false);
        gBuildMode = BUILD_MODE_NONE;
        gHoveredTileId = -1;
        gHoveredSideIndex = -1;
        gHoveredCornerTileId = -1;
        gHoveredCornerIndex = -1;
        return;
    }

    if (aiControlledDecision)
    {
        gBuildMode = BUILD_MODE_NONE;
        gHoveredTileId = -1;
        gHoveredSideIndex = -1;
        gHoveredCornerTileId = -1;
        gHoveredCornerIndex = -1;
        return;
    }

    if (!uiIsDevelopmentPlayConfirmOpen() &&
        !uiIsDevelopmentPurchaseConfirmOpen() &&
        !uiIsDiceRolling() &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        enum DevelopmentCardType hoveredCardType = DEVELOPMENT_CARD_COUNT;
        if (GetHoveredDevelopmentHandCard(map, &hoveredCardType) &&
            CanOpenDevelopmentPlayOverlay(map, hoveredCardType, boardOrigin, radius))
        {
            gDevelopmentPlayPrimaryResource = RESOURCE_WOOD;
            gDevelopmentPlaySecondaryResource = RESOURCE_WHEAT;
            gBuildMode = BUILD_MODE_NONE;
            uiSetBuildPanelOpen(false);
            uiSetDevelopmentPurchaseConfirmOpen(false);
            uiSetTradeMenuOpen(false);
            uiSetPlayerTradeMenuOpen(false);
            uiSetDevelopmentPlayCardType(hoveredCardType);
            uiSetDevelopmentPlayConfirmOpen(true);
            return;
        }
        if (GetHoveredDevelopmentHandCard(map, &hoveredCardType))
        {
            if (hoveredCardType == DEVELOPMENT_CARD_VICTORY_POINT)
            {
                uiShowCenteredWarning("Victory point cards are counted automatically.");
            }
            else if (map->players[map->currentPlayer].developmentCards[hoveredCardType] <=
                     map->players[map->currentPlayer].newlyPurchasedDevelopmentCards[hoveredCardType] ||
                     map->playedDevelopmentCardThisTurn)
            {
                uiShowCenteredWarning("Development cards can't be played the turn you buy them.\nOnly 1 development card can be played per turn.");
            }
            return;
        }
    }

    if (hasFreeRoadPlacements)
    {
        gBuildMode = BUILD_MODE_ROAD;
    }
    else if (needsThiefPlacement)
    {
        gBuildMode = BUILD_MODE_NONE;
    }
    else if (setupSettlementMode)
    {
        gBuildMode = BUILD_MODE_SETTLEMENT;
    }
    else if (setupRoadMode)
    {
        gBuildMode = BUILD_MODE_ROAD;
        uiSetBuildPanelOpen(true);
    }
    else if (!hasFreeRoadPlacements && uiIsBuildPanelOpen() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, buildButton) && canBuyRoad)
    {
        gBuildMode = BUILD_MODE_ROAD;
        return;
    }
    else if (!hasFreeRoadPlacements && uiIsBuildPanelOpen() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, settlementButton) && canBuySettlement)
    {
        gBuildMode = BUILD_MODE_SETTLEMENT;
        return;
    }
    else if (!hasFreeRoadPlacements && uiIsBuildPanelOpen() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, cityButton) && canBuyCity)
    {
        gBuildMode = BUILD_MODE_CITY;
        return;
    }
    else if (!hasFreeRoadPlacements && uiIsBuildPanelOpen() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, devButton) && canBuyDevelopment)
    {
        uiSetDevelopmentPurchaseConfirmOpen(true);
        return;
    }

    if (gameCanRollDice(map) &&
        !uiIsDiceRolling() &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, rollDiceButton))
    {
        uiStartDiceRollAnimation();
        return;
    }

    if (gameCanEndTurn(map) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, endTurnButton))
    {
        gameEndTurn(map);
        gBuildMode = BUILD_MODE_NONE;
        gHoveredTileId = -1;
        gHoveredSideIndex = -1;
        gHoveredCornerTileId = -1;
        gHoveredCornerIndex = -1;
        return;
    }

    if (map->phase == GAME_PHASE_PLAY &&
        !hasFreeRoadPlacements &&
        !needsThiefPlacement &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, tradeButton))
    {
        uiToggleTradeMenu();
        uiSetPlayerTradeMenuOpen(false);
        return;
    }

    if (map->phase == GAME_PHASE_PLAY &&
        !hasFreeRoadPlacements &&
        !needsThiefPlacement &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, playerTradeButton))
    {
        uiTogglePlayerTradeMenu();
        uiSetTradeMenuOpen(false);
        return;
    }

    if (uiGetPlayerTradeMenuOpenAmount() > 0.01f && map->phase == GAME_PHASE_PLAY)
    {
        NormalizePlayerTradeSelection(map);
    }

    if (uiGetTradeMenuOpenAmount() > 0.98f && map->phase == GAME_PHASE_PLAY && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        Rectangle closeButton = {tradeModal.x + tradeModal.width - 42.0f, tradeModal.y + 12.0f, 28.0f, 28.0f};
        if (!CheckCollisionPointRec(mouse, tradeModal))
        {
            uiSetTradeMenuOpen(false);
            return;
        }
        if (CheckCollisionPointRec(mouse, closeButton))
        {
            uiSetTradeMenuOpen(false);
            return;
        }

        for (int i = 0; i < 5; i++)
        {
            Rectangle giveOption = {tradeModal.x + 24.0f, tradeModal.y + 74.0f + i * 34.0f, 180.0f, 28.0f};
            if (CheckCollisionPointRec(mouse, giveOption) &&
                map->players[map->currentPlayer].resources[i] >= gameGetMaritimeTradeRate(map, (enum ResourceType)i))
            {
                gTradeGiveResource = i;
                const int maxTradeAmount = map->players[map->currentPlayer].resources[gTradeGiveResource] / gameGetMaritimeTradeRate(map, (enum ResourceType)gTradeGiveResource);
                if (gTradeAmount > maxTradeAmount)
                {
                    gTradeAmount = maxTradeAmount > 0 ? maxTradeAmount : 1;
                }
                if (gTradeReceiveResource == gTradeGiveResource)
                {
                    gTradeReceiveResource = (gTradeReceiveResource + 1) % 5;
                }
                return;
            }
        }

        for (int i = 0; i < 5; i++)
        {
            Rectangle receiveOption = {tradeModal.x + 232.0f, tradeModal.y + 74.0f + i * 34.0f, 180.0f, 28.0f};
            if (CheckCollisionPointRec(mouse, receiveOption) && i != gTradeGiveResource)
            {
                gTradeReceiveResource = i;
                return;
            }
        }

        Rectangle tradeMinus = {tradeModal.x + 118.0f, tradeModal.y + 286.0f, 28.0f, 28.0f};
        Rectangle tradePlus = {tradeModal.x + 314.0f, tradeModal.y + 286.0f, 28.0f, 28.0f};
        const int maxTradeAmount = map->players[map->currentPlayer].resources[gTradeGiveResource] / gameGetMaritimeTradeRate(map, (enum ResourceType)gTradeGiveResource);
        if (CheckCollisionPointRec(mouse, tradeMinus) && gTradeAmount > 1)
        {
            gTradeAmount--;
            return;
        }
        if (CheckCollisionPointRec(mouse, tradePlus) && gTradeAmount < maxTradeAmount)
        {
            gTradeAmount++;
            return;
        }

        Rectangle confirmButton = {tradeModal.x + 26.0f, tradeModal.y + tradeModal.height - 48.0f, 388.0f, 34.0f};
        if (CheckCollisionPointRec(mouse, confirmButton) &&
            gameTryTradeMaritime(map, (enum ResourceType)gTradeGiveResource, gTradeAmount, (enum ResourceType)gTradeReceiveResource))
        {
            uiSetTradeMenuOpen(false);
            return;
        }
    }

    if (uiGetPlayerTradeMenuOpenAmount() > 0.98f && map->phase == GAME_PHASE_PLAY && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        NormalizePlayerTradeSelection(map);
        Rectangle closeButton = {playerTradeModal.x + playerTradeModal.width - 42.0f, playerTradeModal.y + 12.0f, 28.0f, 28.0f};
        if (!CheckCollisionPointRec(mouse, playerTradeModal))
        {
            uiSetPlayerTradeMenuOpen(false);
            return;
        }
        if (CheckCollisionPointRec(mouse, closeButton))
        {
            uiSetPlayerTradeMenuOpen(false);
            return;
        }

        int playerSlot = 0;
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            enum PlayerType candidate = (enum PlayerType)i;
            if (candidate == map->currentPlayer)
            {
                continue;
            }

            Rectangle playerOption = {playerTradeModal.x + 24.0f + playerSlot * 130.0f, playerTradeModal.y + 74.0f, 116.0f, 32.0f};
            if (CheckCollisionPointRec(mouse, playerOption))
            {
                gPlayerTradeTarget = candidate;
                NormalizePlayerTradeSelection(map);
                return;
            }
            playerSlot++;
        }

        for (int i = 0; i < 5; i++)
        {
            Rectangle giveOption = {playerTradeModal.x + 24.0f, playerTradeModal.y + 160.0f + i * 28.0f, 180.0f, 22.0f};
            Rectangle receiveOption = {playerTradeModal.x + 232.0f, playerTradeModal.y + 160.0f + i * 28.0f, 180.0f, 22.0f};
            if (CheckCollisionPointRec(mouse, giveOption) &&
                map->players[map->currentPlayer].resources[i] > 0)
            {
                gPlayerTradeGiveResource = i;
                NormalizePlayerTradeSelection(map);
                return;
            }
            if (CheckCollisionPointRec(mouse, receiveOption) &&
                i != gPlayerTradeGiveResource)
            {
                gPlayerTradeReceiveResource = i;
                NormalizePlayerTradeSelection(map);
                return;
            }
        }

        Rectangle giveMinus = {playerTradeModal.x + 24.0f, playerTradeModal.y + 326.0f, 28.0f, 28.0f};
        Rectangle givePlus = {playerTradeModal.x + 176.0f, playerTradeModal.y + 326.0f, 28.0f, 28.0f};
        Rectangle receiveMinus = {playerTradeModal.x + 232.0f, playerTradeModal.y + 326.0f, 28.0f, 28.0f};
        Rectangle receivePlus = {playerTradeModal.x + 384.0f, playerTradeModal.y + 326.0f, 28.0f, 28.0f};
        if (CheckCollisionPointRec(mouse, giveMinus) && gPlayerTradeGiveAmount > 1)
        {
            gPlayerTradeGiveAmount--;
            return;
        }
        if (CheckCollisionPointRec(mouse, givePlus) && gPlayerTradeGiveAmount < map->players[map->currentPlayer].resources[gPlayerTradeGiveResource])
        {
            gPlayerTradeGiveAmount++;
            return;
        }
        if (CheckCollisionPointRec(mouse, receiveMinus) && gPlayerTradeReceiveAmount > 1)
        {
            gPlayerTradeReceiveAmount--;
            return;
        }
        if (CheckCollisionPointRec(mouse, receivePlus) && gPlayerTradeReceiveAmount < 9)
        {
            gPlayerTradeReceiveAmount++;
            return;
        }

        Rectangle confirmButton = {playerTradeModal.x + 54.0f, playerTradeModal.y + playerTradeModal.height - 48.0f, 332.0f, 34.0f};
        if (CheckCollisionPointRec(mouse, confirmButton))
        {
            const enum ResourceType give = (enum ResourceType)gPlayerTradeGiveResource;
            const enum ResourceType receive = (enum ResourceType)gPlayerTradeReceiveResource;
            const bool targetIsAi = map->players[gPlayerTradeTarget].controlMode == PLAYER_CONTROL_AI;

            if (targetIsAi)
            {
                if (aiShouldAcceptPlayerTradeOffer(map, gPlayerTradeTarget, give, gPlayerTradeGiveAmount, receive, gPlayerTradeReceiveAmount) &&
                    gameTryTradeWithPlayer(map, gPlayerTradeTarget, give, gPlayerTradeGiveAmount, receive, gPlayerTradeReceiveAmount))
                {
                    uiShowCenteredStatus(TextFormat("%s accepts your trade.", PlayerNameLabel(gPlayerTradeTarget)), UI_NOTIFICATION_POSITIVE);
                }
                else
                {
                    uiShowCenteredStatus(TextFormat("%s declines the trade.", PlayerNameLabel(gPlayerTradeTarget)), UI_NOTIFICATION_NEGATIVE);
                }
                uiSetPlayerTradeMenuOpen(false);
                return;
            }

            if (gameTryTradeWithPlayer(map, gPlayerTradeTarget, give, gPlayerTradeGiveAmount, receive, gPlayerTradeReceiveAmount))
            {
                uiSetPlayerTradeMenuOpen(false);
                return;
            }
            return;
        }
    }

    gHoveredTileId = -1;
    gHoveredSideIndex = -1;
    gHoveredCornerTileId = -1;
    gHoveredCornerIndex = -1;

    if (needsThiefPlacement)
    {
        float bestTileDistance = radius * 0.95f;
        for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
        {
            if (!gameCanMoveThiefToTile(map, tileId))
            {
                continue;
            }

            const Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
            const float dx = mouse.x - center.x;
            const float dy = mouse.y - center.y;
            const float distance = sqrtf(dx * dx + dy * dy);
            if (distance < bestTileDistance && IsMouseNearTile(mouse, center, radius))
            {
                bestTileDistance = distance;
                gHoveredTileId = tileId;
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && gHoveredTileId >= 0)
        {
            gameMoveThief(map, gHoveredTileId);
            gHoveredTileId = -1;
        }
        return;
    }

    if (gBuildMode == BUILD_MODE_ROAD)
    {
        for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
        {
            const Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);

            for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
            {
                int aIndex = 0;
                int bIndex = 1;
                GetSideCornerIndices(sideIndex, &aIndex, &bIndex);
                const Vector2 a = PointOnHex(center, radius * 0.94f, aIndex);
                const Vector2 b = PointOnHex(center, radius * 0.94f, bIndex);
                const float distance = DistancePointToSegment(mouse, a, b);

                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    gHoveredTileId = tileId;
                    gHoveredSideIndex = sideIndex;
                }
            }
        }

        CanonicalizeHoveredEdge();
    }
    else if (gBuildMode == BUILD_MODE_SETTLEMENT || gBuildMode == BUILD_MODE_CITY)
    {
        float bestCornerDistance = 22.0f;
        for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
        {
            const Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
            for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
            {
                const Vector2 corner = PointOnHex(center, radius * 0.98f, cornerIndex);
                const float dx = mouse.x - corner.x;
                const float dy = mouse.y - corner.y;
                const float distance = sqrtf(dx * dx + dy * dy);
                if (distance < bestCornerDistance)
                {
                    bestCornerDistance = distance;
                    gHoveredCornerTileId = tileId;
                    gHoveredCornerIndex = cornerIndex;
                }
            }
        }

        CanonicalizeHoveredCorner();
    }

    if (gBuildMode == BUILD_MODE_ROAD &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        gHoveredTileId >= 0 &&
        gHoveredSideIndex >= 0 &&
        !IsSharedEdgeOccupied(map, gHoveredTileId, gHoveredSideIndex) &&
        (setupRoadMode || hasFreeRoadPlacements || gameCanAffordRoad(map)) &&
        boardIsValidRoadPlacement(map, gHoveredTileId, gHoveredSideIndex, map->currentPlayer, boardOrigin, radius) &&
        (!setupRoadMode || boardEdgeTouchesCorner(gHoveredTileId, gHoveredSideIndex, map->setupSettlementTileId, map->setupSettlementCornerIndex, boardOrigin, radius)))
    {
        if (!setupRoadMode && !hasFreeRoadPlacements && !gameTryBuyRoad(map))
        {
            return;
        }
        PlaceRoadOnSharedEdge(map, gHoveredTileId, gHoveredSideIndex, map->currentPlayer);
        gameRefreshAwards(map);
        gHoveredTileId = -1;
        gHoveredSideIndex = -1;

        if (setupRoadMode)
        {
            gameHandlePlacedRoad(map);
            gBuildMode = map->phase == GAME_PHASE_PLAY ? BUILD_MODE_NONE : BUILD_MODE_SETTLEMENT;
        }
        else if (hasFreeRoadPlacements)
        {
            gameConsumeFreeRoadPlacement(map);
            if (gameCheckVictory(map, map->currentPlayer))
            {
                uiSetBuildPanelOpen(false);
                uiSetTradeMenuOpen(false);
                uiSetPlayerTradeMenuOpen(false);
                gBuildMode = BUILD_MODE_NONE;
            }
            else
            {
                gBuildMode = gameHasFreeRoadPlacements(map) && HasAnyValidRoadPlacement(map, boardOrigin, radius)
                                 ? BUILD_MODE_ROAD
                                 : BUILD_MODE_NONE;
            }
        }
        else
        {
            if (gameCheckVictory(map, map->currentPlayer))
            {
                uiSetBuildPanelOpen(false);
                uiSetTradeMenuOpen(false);
                uiSetPlayerTradeMenuOpen(false);
            }
            gBuildMode = BUILD_MODE_NONE;
        }
    }

    if (gBuildMode == BUILD_MODE_SETTLEMENT &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        gHoveredCornerTileId >= 0 &&
        gHoveredCornerIndex >= 0 &&
        (setupSettlementMode || gameCanAffordSettlement(map)) &&
        boardIsValidSettlementPlacement(map, gHoveredCornerTileId, gHoveredCornerIndex, map->currentPlayer, boardOrigin, radius))
    {
        if (!setupSettlementMode && !gameTryBuySettlement(map))
        {
            return;
        }
        PlaceSettlementOnSharedCorner(map, gHoveredCornerTileId, gHoveredCornerIndex, map->currentPlayer, STRUCTURE_TOWN);
        gameRefreshAwards(map);
        if (setupSettlementMode)
        {
            gameHandlePlacedSettlement(map, gHoveredCornerTileId, gHoveredCornerIndex);
            gBuildMode = BUILD_MODE_ROAD;
        }
        else
        {
            if (gameCheckVictory(map, map->currentPlayer))
            {
                uiSetBuildPanelOpen(false);
                uiSetTradeMenuOpen(false);
                uiSetPlayerTradeMenuOpen(false);
            }
            gBuildMode = BUILD_MODE_NONE;
        }
        gHoveredCornerTileId = -1;
        gHoveredCornerIndex = -1;
    }

    if (gBuildMode == BUILD_MODE_CITY &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        gHoveredCornerTileId >= 0 &&
        gHoveredCornerIndex >= 0 &&
        boardIsValidCityPlacement(map, gHoveredCornerTileId, gHoveredCornerIndex, map->currentPlayer))
    {
        if (!gameTryBuyCity(map))
        {
            return;
        }
        PlaceSettlementOnSharedCorner(map, gHoveredCornerTileId, gHoveredCornerIndex, map->currentPlayer, STRUCTURE_CITY);
        gameRefreshAwards(map);
        if (gameCheckVictory(map, map->currentPlayer))
        {
            uiSetBuildPanelOpen(false);
            uiSetTradeMenuOpen(false);
            uiSetPlayerTradeMenuOpen(false);
        }
        gBuildMode = BUILD_MODE_NONE;
        gHoveredCornerTileId = -1;
        gHoveredCornerIndex = -1;
    }
}

Vector2 AxialToWorld(struct AxialCoord coord, Vector2 origin, float radius)
{
    const float root3 = 1.7320508f;
    return (Vector2){
        origin.x + radius * root3 * ((float)coord.q + (float)coord.r * 0.5f),
        origin.y + radius * 1.5f * (float)coord.r};
}

void DrawRoad(Vector2 center, float radius, int sideIndex, enum PlayerType player, bool placed, bool available, bool hovered, float popAmount)
{
    int aIndex = 0;
    int bIndex = 1;
    GetSideCornerIndices(sideIndex, &aIndex, &bIndex);

    const Vector2 edgeA = PointOnHex(center, radius * 0.98f, aIndex);
    const Vector2 edgeB = PointOnHex(center, radius * 0.98f, bIndex);
    const Vector2 mid = LerpVec2(edgeA, edgeB, 0.5f);
    const float popScale = 1.0f + popAmount * 0.24f;
    const Vector2 a = Vec2Add(mid, Vec2Scale((Vector2){LerpVec2(edgeA, edgeB, 0.18f).x - mid.x, LerpVec2(edgeA, edgeB, 0.18f).y - mid.y}, popScale));
    const Vector2 b = Vec2Add(mid, Vec2Scale((Vector2){LerpVec2(edgeA, edgeB, 0.82f).x - mid.x, LerpVec2(edgeA, edgeB, 0.82f).y - mid.y}, popScale));
    const Color availableColor = hovered ? (Color){235, 199, 67, 255} : Fade((Color){248, 227, 161, 255}, 0.82f);
    const Color roadColor = placed ? PlayerColor(player) : availableColor;
    const float shadowWidth = (hovered ? 13.0f : 12.0f) * popScale;
    const float bodyWidth = (hovered ? 11.0f : (available ? 10.0f : 9.0f)) * popScale;
    const float highlightWidth = (hovered ? 3.0f : 2.0f) * popScale;

    if (placed || available || hovered)
    {
        DrawLineEx(a, b, shadowWidth, Fade(BLACK, placed ? 0.14f : (hovered ? 0.14f : 0.10f)));
    }

    DrawLineEx(a, b, bodyWidth, roadColor);
    if (!placed)
    {
        DrawLineEx(a, b, highlightWidth, Fade(RAYWHITE, hovered ? 0.34f : 0.24f));
    }
}

void DrawStructure(Vector2 center, float radius, int cornerIndex, enum PlayerType player, enum StructureType structure, bool available, bool hovered, float popAmount)
{
    const Vector2 corner = PointOnHex(center, radius * 0.98f, cornerIndex);
    const float popScale = 1.0f + popAmount * 0.20f;
    const Color previewFill = hovered ? (Color){235, 199, 67, 235} : Fade((Color){248, 227, 161, 255}, 0.84f);
    const Color fill = available ? previewFill : (hovered ? Fade(PlayerColor(player), 0.60f) : PlayerColor(player));
    const Color outline = available ? (hovered ? (Color){120, 84, 22, 255} : (Color){143, 116, 47, 255}) : (hovered ? Fade((Color){236, 220, 187, 255}, 0.75f) : (Color){80, 50, 28, 255});
    const Color roof = hovered ? Fade(ColorBrightness(fill, -0.18f), 0.72f) : ColorBrightness(fill, -0.20f);
    const Color shadow = Fade(BLACK, available ? (hovered ? 0.14f : 0.10f) : (hovered ? 0.10f : 0.14f));

    if (structure == STRUCTURE_CITY)
    {
        const float width = radius * 0.30f * popScale;
        const float height = radius * 0.18f * popScale;
        const float roofHeight = radius * 0.12f * popScale;
        const Rectangle base = {
            corner.x - width * 0.5f,
            corner.y - height * 0.06f,
            width,
            height};
        const Rectangle keep = {
            corner.x - width * 0.18f,
            base.y - height * 0.58f,
            width * 0.36f,
            height * 0.72f};
        const Rectangle leftTower = {
            base.x + width * 0.04f,
            base.y - height * 0.42f,
            width * 0.20f,
            height * 0.58f};
        const Rectangle rightTower = {
            base.x + width * 0.76f,
            base.y - height * 0.42f,
            width * 0.20f,
            height * 0.58f};

        DrawRectangleRounded((Rectangle){base.x + radius * 0.01f, base.y + radius * 0.03f, base.width, base.height}, 0.12f, 6, shadow);
        DrawRectangleRounded(base, 0.12f, 6, fill);
        DrawRectangleLinesEx(base, 1.8f, outline);

        DrawRectangleRounded((Rectangle){keep.x + radius * 0.008f, keep.y + radius * 0.02f, keep.width, keep.height}, 0.10f, 6, shadow);
        DrawRectangleRounded(keep, 0.10f, 6, fill);
        DrawRectangleLinesEx(keep, 1.6f, outline);

        DrawRectangleRounded((Rectangle){leftTower.x + radius * 0.008f, leftTower.y + radius * 0.02f, leftTower.width, leftTower.height}, 0.10f, 6, shadow);
        DrawRectangleRounded(leftTower, 0.10f, 6, fill);
        DrawRectangleLinesEx(leftTower, 1.5f, outline);

        DrawRectangleRounded((Rectangle){rightTower.x + radius * 0.008f, rightTower.y + radius * 0.02f, rightTower.width, rightTower.height}, 0.10f, 6, shadow);
        DrawRectangleRounded(rightTower, 0.10f, 6, fill);
        DrawRectangleLinesEx(rightTower, 1.5f, outline);

        DrawTriangle(
            (Vector2){corner.x, keep.y - roofHeight},
            (Vector2){keep.x - radius * 0.01f, keep.y + radius * 0.02f},
            (Vector2){keep.x + keep.width + radius * 0.01f, keep.y + radius * 0.02f},
            roof);
        DrawTriangleLines(
            (Vector2){corner.x, keep.y - roofHeight},
            (Vector2){keep.x - radius * 0.01f, keep.y + radius * 0.02f},
            (Vector2){keep.x + keep.width + radius * 0.01f, keep.y + radius * 0.02f},
            outline);
        DrawTriangle(
            (Vector2){leftTower.x + leftTower.width * 0.5f, leftTower.y - roofHeight * 0.72f},
            (Vector2){leftTower.x - radius * 0.006f, leftTower.y + radius * 0.01f},
            (Vector2){leftTower.x + leftTower.width + radius * 0.006f, leftTower.y + radius * 0.01f},
            roof);
        DrawTriangleLines(
            (Vector2){leftTower.x + leftTower.width * 0.5f, leftTower.y - roofHeight * 0.72f},
            (Vector2){leftTower.x - radius * 0.006f, leftTower.y + radius * 0.01f},
            (Vector2){leftTower.x + leftTower.width + radius * 0.006f, leftTower.y + radius * 0.01f},
            outline);
        DrawTriangle(
            (Vector2){rightTower.x + rightTower.width * 0.5f, rightTower.y - roofHeight * 0.72f},
            (Vector2){rightTower.x - radius * 0.006f, rightTower.y + radius * 0.01f},
            (Vector2){rightTower.x + rightTower.width + radius * 0.006f, rightTower.y + radius * 0.01f},
            roof);
        DrawTriangleLines(
            (Vector2){rightTower.x + rightTower.width * 0.5f, rightTower.y - roofHeight * 0.72f},
            (Vector2){rightTower.x - radius * 0.006f, rightTower.y + radius * 0.01f},
            (Vector2){rightTower.x + rightTower.width + radius * 0.006f, rightTower.y + radius * 0.01f},
            outline);

        DrawRectangle((int)(keep.x + keep.width * 0.38f), (int)(base.y + base.height * 0.38f), (int)(keep.width * 0.24f), (int)(base.height * 0.62f), Fade(outline, 0.60f));
        return;
    }

    const float width = radius * 0.22f * popScale;
    const float height = radius * 0.18f * popScale;
    const float roofHeight = radius * 0.14f * popScale;
    const Rectangle body = {
        corner.x - width * 0.5f,
        corner.y - height * 0.10f,
        width,
        height};

    DrawRectangleRounded((Rectangle){body.x + radius * 0.01f, body.y + radius * 0.03f, body.width, body.height}, 0.12f, 6, shadow);
    DrawRectangleRounded(body, 0.12f, 6, fill);
    DrawRectangleLinesEx(body, 1.8f, outline);
    DrawTriangle(
        (Vector2){corner.x, body.y - roofHeight},
        (Vector2){body.x - radius * 0.01f, body.y + radius * 0.02f},
        (Vector2){body.x + body.width + radius * 0.01f, body.y + radius * 0.02f},
        roof);
    DrawTriangleLines(
        (Vector2){corner.x, body.y - roofHeight},
        (Vector2){body.x - radius * 0.01f, body.y + radius * 0.02f},
        (Vector2){body.x + body.width + radius * 0.01f, body.y + radius * 0.02f},
        outline);
}

void DrawPort(const struct PortVisual *port, Vector2 center, float radius)
{
    const int sideIndex = FindPortSideIndex(port);
    if (sideIndex < 0)
    {
        return;
    }

    DrawBeachWedge(center, radius, sideIndex);
    DrawHarborMarker(port, center, radius, sideIndex);
}

static void DrawBeachWedge(Vector2 center, float radius, int sideIndex)
{
    int aIndex = 0;
    int bIndex = 1;
    GetSideCornerIndices(sideIndex, &aIndex, &bIndex);
    const Vector2 a = PointOnHex(center, radius * 1.02f, aIndex);
    const Vector2 b = PointOnHex(center, radius * 1.02f, bIndex);
    const Vector2 innerA = LerpVec2(a, center, 0.38f);
    const Vector2 innerB = LerpVec2(b, center, 0.38f);
    const Color sand = (Color){224, 203, 149, 255};
    const Color sandDark = (Color){190, 164, 108, 255};

    DrawTriangle(a, b, innerA, sand);
    DrawTriangle(innerA, b, innerB, sand);
    DrawLineEx(a, b, 3.0f, Fade((Color){244, 230, 189, 255}, 0.75f));
    DrawLineEx(innerA, innerB, 2.0f, Fade(sandDark, 0.55f));
    DrawCircleV(LerpVec2(innerA, innerB, 0.33f), radius * 0.03f, Fade((Color){245, 236, 205, 255}, 0.45f));
    DrawCircleV(LerpVec2(innerA, innerB, 0.72f), radius * 0.025f, Fade((Color){245, 236, 205, 255}, 0.36f));
}

static void DrawHarborMarker(const struct PortVisual *port, Vector2 center, float radius, int sideIndex)
{
    int aIndex = 0;
    int bIndex = 1;
    GetSideCornerIndices(sideIndex, &aIndex, &bIndex);
    const Vector2 a = PointOnHex(center, radius * 0.98f, aIndex);
    const Vector2 b = PointOnHex(center, radius * 0.98f, bIndex);
    const Vector2 edgeMid = LerpVec2(a, b, 0.5f);
    const Vector2 inward = Vec2NormalizeSafe((Vector2){center.x - edgeMid.x, center.y - edgeMid.y});
    const Vector2 tangent = Vec2NormalizeSafe((Vector2){b.x - a.x, b.y - a.y});
    const Vector2 dockCenter = Vec2Add(edgeMid, Vec2Scale(inward, radius * 0.12f));
    const Vector2 hutCenter = Vec2Add(edgeMid, Vec2Scale(inward, radius * 0.24f));
    const Vector2 dockA = Vec2Add(dockCenter, Vec2Scale(tangent, -radius * 0.22f));
    const Vector2 dockB = Vec2Add(dockCenter, Vec2Scale(tangent, radius * 0.22f));

    DrawLineEx(dockA, dockB, 6.0f, (Color){115, 79, 46, 255});
    DrawLineEx(Vec2Add(dockA, Vec2Scale(inward, -radius * 0.10f)), dockA, 4.5f, (Color){115, 79, 46, 255});
    DrawLineEx(Vec2Add(dockB, Vec2Scale(inward, -radius * 0.10f)), dockB, 4.5f, (Color){115, 79, 46, 255});

    DrawRectangleRounded(
        (Rectangle){hutCenter.x - radius * 0.12f, hutCenter.y - radius * 0.07f, radius * 0.24f, radius * 0.14f},
        0.18f, 6, (Color){164, 108, 65, 255});
    DrawTriangle(
        (Vector2){hutCenter.x - radius * 0.15f, hutCenter.y - radius * 0.02f},
        (Vector2){hutCenter.x, hutCenter.y - radius * 0.17f},
        (Vector2){hutCenter.x + radius * 0.15f, hutCenter.y - radius * 0.02f},
        (Color){124, 71, 44, 255});

    const char *label = PortLabel(port);
    const int fontSize = (int)(radius * 0.21f);
    const int textWidth = MeasureUiText(label, fontSize);
    Vector2 textPos = Vec2Add(hutCenter, Vec2Scale(inward, radius * 0.18f));
    Rectangle token = {
        textPos.x - radius * 0.26f,
        textPos.y - radius * 0.14f,
        radius * 0.52f,
        radius * 0.24f};
    DrawRectangleRounded(token, 0.45f, 8, Fade((Color){244, 232, 202, 255}, 0.92f));
    DrawRectangleLinesEx(token, 2.0f, Fade((Color){124, 91, 45, 255}, 0.86f));
    DrawUiText(label, textPos.x - textWidth / 2.0f, token.y + token.height * 0.5f - fontSize * 0.5f, fontSize, (Color){44, 32, 25, 255});
}

static int FindPortSideIndex(const struct PortVisual *port)
{
    static const struct AxialCoord directions[HEX_CORNERS] = {
        {1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};

    const int dq = port->landCoord.q - port->oceanCoord.q;
    const int dr = port->landCoord.r - port->oceanCoord.r;

    for (int i = 0; i < HEX_CORNERS; i++)
    {
        if (directions[i].q == dq && directions[i].r == dr)
        {
            return i;
        }
    }

    return -1;
}

void GetSideCornerIndices(int sideIndex, int *cornerA, int *cornerB)
{
    static const int sideCorners[HEX_CORNERS][2] = {
        {0, 1}, // east
        {5, 0}, // northeast
        {4, 5}, // northwest
        {3, 4}, // west
        {2, 3}, // southwest
        {1, 2}  // southeast
    };

    const int index = ((sideIndex % HEX_CORNERS) + HEX_CORNERS) % HEX_CORNERS;
    *cornerA = sideCorners[index][0];
    *cornerB = sideCorners[index][1];
}

Vector2 PointOnHex(Vector2 center, float radius, int cornerIndex)
{
    const float angle = DEG2RAD * (60.0f * (float)cornerIndex - 30.0f);
    return (Vector2){
        center.x + cosf(angle) * radius,
        center.y + sinf(angle) * radius};
}

Vector2 LerpVec2(Vector2 a, Vector2 b, float t)
{
    return (Vector2){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t};
}

Vector2 Vec2Add(Vector2 a, Vector2 b)
{
    return (Vector2){a.x + b.x, a.y + b.y};
}

Vector2 Vec2Scale(Vector2 v, float s)
{
    return (Vector2){v.x * s, v.y * s};
}

Vector2 Vec2NormalizeSafe(Vector2 v)
{
    const float len = sqrtf(v.x * v.x + v.y * v.y);
    if (len <= 0.0001f)
    {
        return (Vector2){0.0f, 0.0f};
    }
    return (Vector2){v.x / len, v.y / len};
}

static const char *PortLabel(const struct PortVisual *port)
{
    if (port->generic)
    {
        return "3:1";
    }

    switch (port->resource)
    {
    case RESOURCE_WOOD:
        return "2:1 W";
    case RESOURCE_WHEAT:
        return "2:1 H";
    case RESOURCE_CLAY:
        return "2:1 C";
    case RESOURCE_SHEEP:
        return "2:1 S";
    case RESOURCE_STONE:
        return "2:1 O";
    default:
        return "2:1";
    }
}

static float DistancePointToSegment(Vector2 p, Vector2 a, Vector2 b)
{
    const Vector2 ab = {b.x - a.x, b.y - a.y};
    const Vector2 ap = {p.x - a.x, p.y - a.y};
    const float abLenSq = ab.x * ab.x + ab.y * ab.y;
    float t = abLenSq > 0.0f ? (ap.x * ab.x + ap.y * ab.y) / abLenSq : 0.0f;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;

    const Vector2 closest = {a.x + ab.x * t, a.y + ab.y * t};
    const float dx = p.x - closest.x;
    const float dy = p.y - closest.y;
    return sqrtf(dx * dx + dy * dy);
}

Color PlayerColor(enum PlayerType player)
{
    switch (player)
    {
    case PLAYER_RED:
        return (Color){230, 41, 55, 255};
    case PLAYER_BLUE:
        return (Color){42, 98, 214, 255};
    case PLAYER_GREEN:
        return (Color){52, 144, 76, 255};
    case PLAYER_BLACK:
        return (Color){52, 52, 58, 255};
    default:
        return (Color){236, 220, 187, 255};
    }
}

void RendererGetRoadEdgeKey(Vector2 center, float radius, int sideIndex, int *ax, int *ay, int *bx, int *by)
{
    int aIndex = 0;
    int bIndex = 1;
    GetSideCornerIndices(sideIndex, &aIndex, &bIndex);
    Vector2 a = PointOnHex(center, radius, aIndex);
    Vector2 b = PointOnHex(center, radius, bIndex);

    int x1 = (int)roundf(a.x * 10.0f);
    int y1 = (int)roundf(a.y * 10.0f);
    int x2 = (int)roundf(b.x * 10.0f);
    int y2 = (int)roundf(b.y * 10.0f);

    if (x1 > x2 || (x1 == x2 && y1 > y2))
    {
        int tx = x1;
        x1 = x2;
        x2 = tx;
        int ty = y1;
        y1 = y2;
        y2 = ty;
    }

    *ax = x1;
    *ay = y1;
    *bx = x2;
    *by = y2;
}

static bool EdgeKeysMatch(int ax1, int ay1, int bx1, int by1, int ax2, int ay2, int bx2, int by2)
{
    return ax1 == ax2 && ay1 == ay2 && bx1 == bx2 && by1 == by2;
}

bool IsCanonicalSharedEdge(int tileId, int sideIndex)
{
    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
    int tax, tay, tbx, tby;
    RendererGetRoadEdgeKey(center, radius, sideIndex, &tax, &tay, &tbx, &tby);

    for (int otherTile = 0; otherTile < tileId; otherTile++)
    {
        const Vector2 otherCenter = AxialToWorld(kLandCoords[otherTile], origin, radius);
        for (int otherSide = 0; otherSide < HEX_CORNERS; otherSide++)
        {
            int oax, oay, obx, oby;
            RendererGetRoadEdgeKey(otherCenter, radius, otherSide, &oax, &oay, &obx, &oby);
            if (EdgeKeysMatch(tax, tay, tbx, tby, oax, oay, obx, oby))
            {
                return false;
            }
        }
    }

    return true;
}

static void CanonicalizeHoveredEdge(void)
{
    if (gHoveredTileId < 0 || gHoveredSideIndex < 0)
    {
        return;
    }

    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const Vector2 center = AxialToWorld(kLandCoords[gHoveredTileId], origin, radius);
    int tax, tay, tbx, tby;
    RendererGetRoadEdgeKey(center, radius, gHoveredSideIndex, &tax, &tay, &tbx, &tby);

    int bestTile = gHoveredTileId;
    int bestSide = gHoveredSideIndex;

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        const Vector2 otherCenter = AxialToWorld(kLandCoords[tileId], origin, radius);
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            int oax, oay, obx, oby;
            RendererGetRoadEdgeKey(otherCenter, radius, sideIndex, &oax, &oay, &obx, &oby);
            if (EdgeKeysMatch(tax, tay, tbx, tby, oax, oay, obx, oby))
            {
                if (tileId < bestTile || (tileId == bestTile && sideIndex < bestSide))
                {
                    bestTile = tileId;
                    bestSide = sideIndex;
                }
            }
        }
    }

    gHoveredTileId = bestTile;
    gHoveredSideIndex = bestSide;
}

bool IsSharedEdgeOccupied(const struct Map *map, int tileId, int sideIndex)
{
    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
    int tax, tay, tbx, tby;
    RendererGetRoadEdgeKey(center, radius, sideIndex, &tax, &tay, &tbx, &tby);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = AxialToWorld(kLandCoords[otherTile], origin, radius);
        for (int otherSide = 0; otherSide < HEX_CORNERS; otherSide++)
        {
            if (!map->tiles[otherTile].sides[otherSide].isset)
            {
                continue;
            }
            int oax, oay, obx, oby;
            RendererGetRoadEdgeKey(otherCenter, radius, otherSide, &oax, &oay, &obx, &oby);
            if (EdgeKeysMatch(tax, tay, tbx, tby, oax, oay, obx, oby))
            {
                return true;
            }
        }
    }

    return false;
}

void PlaceRoadOnSharedEdge(struct Map *map, int tileId, int sideIndex, enum PlayerType player)
{
    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const Vector2 targetCenter = AxialToWorld(kLandCoords[tileId], origin, radius);
    int tax, tay, tbx, tby;
    RendererGetRoadEdgeKey(targetCenter, radius, sideIndex, &tax, &tay, &tbx, &tby);
    uiRecordRoadPlacement(tax, tay, tbx, tby);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = AxialToWorld(kLandCoords[otherTile], origin, radius);
        for (int otherSide = 0; otherSide < HEX_CORNERS; otherSide++)
        {
            int oax, oay, obx, oby;
            RendererGetRoadEdgeKey(otherCenter, radius, otherSide, &oax, &oay, &obx, &oby);

            if (EdgeKeysMatch(tax, tay, tbx, tby, oax, oay, obx, oby))
            {
                map->tiles[otherTile].sides[otherSide].isset = true;
                map->tiles[otherTile].sides[otherSide].player = player;
            }
        }
    }
}

void RendererGetCornerKey(Vector2 center, float radius, int cornerIndex, int *x, int *y)
{
    Vector2 point = PointOnHex(center, radius, cornerIndex);
    *x = (int)roundf(point.x * 10.0f);
    *y = (int)roundf(point.y * 10.0f);
}

static bool CornerKeysMatch(int x1, int y1, int x2, int y2)
{
    return x1 == x2 && y1 == y2;
}

bool IsCanonicalSharedCorner(int tileId, int cornerIndex)
{
    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
    int tx, ty;
    RendererGetCornerKey(center, radius, cornerIndex, &tx, &ty);

    for (int otherTile = 0; otherTile < tileId; otherTile++)
    {
        const Vector2 otherCenter = AxialToWorld(kLandCoords[otherTile], origin, radius);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int ox, oy;
            RendererGetCornerKey(otherCenter, radius, otherCorner, &ox, &oy);
            if (CornerKeysMatch(tx, ty, ox, oy))
            {
                return false;
            }
        }
    }

    return true;
}

static void CanonicalizeHoveredCorner(void)
{
    if (gHoveredCornerTileId < 0 || gHoveredCornerIndex < 0)
    {
        return;
    }

    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const Vector2 center = AxialToWorld(kLandCoords[gHoveredCornerTileId], origin, radius);
    int tx, ty;
    RendererGetCornerKey(center, radius, gHoveredCornerIndex, &tx, &ty);

    int bestTile = gHoveredCornerTileId;
    int bestCorner = gHoveredCornerIndex;
    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        const Vector2 otherCenter = AxialToWorld(kLandCoords[tileId], origin, radius);
        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            int ox, oy;
            RendererGetCornerKey(otherCenter, radius, cornerIndex, &ox, &oy);
            if (CornerKeysMatch(tx, ty, ox, oy))
            {
                if (tileId < bestTile || (tileId == bestTile && cornerIndex < bestCorner))
                {
                    bestTile = tileId;
                    bestCorner = cornerIndex;
                }
            }
        }
    }

    gHoveredCornerTileId = bestTile;
    gHoveredCornerIndex = bestCorner;
}

void PlaceSettlementOnSharedCorner(struct Map *map, int tileId, int cornerIndex, enum PlayerType player, enum StructureType structure)
{
    const float radius = BOARD_HEX_RADIUS;
    const Vector2 origin = {(float)GetScreenWidth() * BOARD_ORIGIN_X_FACTOR, (float)GetScreenHeight() * BOARD_ORIGIN_Y_FACTOR};
    const Vector2 center = AxialToWorld(kLandCoords[tileId], origin, radius);
    int tx, ty;
    RendererGetCornerKey(center, radius, cornerIndex, &tx, &ty);
    uiRecordStructurePlacement(tx, ty);

    for (int otherTile = 0; otherTile < LAND_TILE_COUNT; otherTile++)
    {
        const Vector2 otherCenter = AxialToWorld(kLandCoords[otherTile], origin, radius);
        for (int otherCorner = 0; otherCorner < HEX_CORNERS; otherCorner++)
        {
            int ox, oy;
            RendererGetCornerKey(otherCenter, radius, otherCorner, &ox, &oy);
            if (CornerKeysMatch(tx, ty, ox, oy))
            {
                map->tiles[otherTile].corners[otherCorner].owner = player;
                map->tiles[otherTile].corners[otherCorner].structure = structure;
            }
        }
    }
}

void DrawNumberToken(int number, Vector2 center, float radius)
{
    const float tokenRadius = radius * 0.28f;
    const bool isHot = number == 6 || number == 8;
    const Color fill = (Color){244, 232, 202, 140};
    const Color edge = (Color){124, 91, 45, 165};
    const Color textColor = isHot ? (Color){191, 40, 37, 255} : (Color){36, 27, 22, 255};
    const int fontSize = (int)(radius * 0.34f);
    char label[3];
    snprintf(label, sizeof(label), "%d", number);

    DrawCircleV((Vector2){center.x, center.y + radius * 0.06f}, tokenRadius + 3.0f, Fade((Color){58, 40, 18, 255}, 0.10f));
    DrawCircleSector((Vector2){center.x, center.y + radius * 0.04f}, tokenRadius, 0.0f, 360.0f, 48, fill);
    DrawRingLines((Vector2){center.x, center.y + radius * 0.04f}, tokenRadius - 4.0f, tokenRadius, 0.0f, 360.0f, 48, edge);

    const Vector2 tokenCenter = {center.x, center.y + radius * 0.04f};
    const Vector2 textSize = MeasureTextEx(RendererGetUiFont(), label, (float)fontSize, UI_FONT_SPACING(fontSize));
    DrawUiText(label, tokenCenter.x - textSize.x * 0.5f, tokenCenter.y - textSize.y * 0.5f - radius * 0.01f, fontSize, textColor);

    int pips = 6 - abs(7 - number);
    if (pips > 0)
    {
        DrawPipRow((Vector2){center.x, center.y + radius * 0.24f}, pips, radius * 0.07f, radius * 0.022f, textColor);
    }
}

static void DrawPipRow(Vector2 center, int pipCount, float spacing, float dotRadius, Color color)
{
    const float width = (float)(pipCount - 1) * spacing;
    for (int i = 0; i < pipCount; i++)
    {
        Vector2 dot = {center.x - width * 0.5f + spacing * (float)i, center.y};
        DrawCircleV(dot, dotRadius, color);
    }
}

void DrawThief(Vector2 center, float radius)
{
    const Vector2 body = {center.x + radius * 0.32f, center.y - radius * 0.18f};
    const Color cloak = (Color){48, 45, 55, 255};
    const Color shadow = Fade(BLACK, 0.24f);

    DrawEllipse((int)body.x, (int)(body.y + radius * 0.18f), radius * 0.12f, radius * 0.17f, shadow);
    DrawCircleV((Vector2){body.x, body.y - radius * 0.12f}, radius * 0.08f, (Color){226, 210, 182, 255});
    DrawTriangle(
        (Vector2){body.x, body.y - radius * 0.25f},
        (Vector2){body.x - radius * 0.18f, body.y + radius * 0.18f},
        (Vector2){body.x + radius * 0.18f, body.y + radius * 0.18f},
        cloak);
    DrawRectangleRounded(
        (Rectangle){body.x - radius * 0.07f, body.y + radius * 0.12f, radius * 0.14f, radius * 0.18f},
        0.35f, 8, cloak);
    DrawLineEx(
        (Vector2){body.x + radius * 0.12f, body.y - radius * 0.08f},
        (Vector2){body.x + radius * 0.24f, body.y + radius * 0.22f},
        4.0f,
        (Color){109, 75, 45, 255});
}

void DrawTileHighlightBorder(Vector2 center, float radius, Color glowColor, Color borderColor, float intensity)
{
    if (intensity <= 0.0f)
    {
        return;
    }

    const float pulse = 0.94f + 0.06f * sinf((float)GetTime() * 8.0f);
    const float outerWidth = (7.0f + 7.0f * intensity) * pulse;
    const float innerWidth = 3.0f + 2.5f * intensity;

    DrawPolyLinesEx(center, HEX_CORNERS, radius * 1.05f, -30.0f, outerWidth, Fade(glowColor, 0.16f + 0.18f * intensity));
    DrawPolyLinesEx(center, HEX_CORNERS, radius * 1.012f, -30.0f, innerWidth, Fade(borderColor, 0.76f + 0.20f * intensity));
}

void DrawTileHighlightWash(Vector2 center, float radius, Color color, float intensity)
{
    if (intensity <= 0.0f)
    {
        return;
    }

    DrawPoly(center, HEX_CORNERS, radius * 0.985f, -30.0f, Fade(color, 0.14f + 0.08f * intensity));
    DrawPoly(center, HEX_CORNERS, radius * 0.86f, -30.0f, Fade(color, 0.05f + 0.04f * intensity));
}

static bool IsMouseNearTile(Vector2 mouse, Vector2 center, float radius)
{
    const float dx = fabsf(mouse.x - center.x);
    const float dy = fabsf(mouse.y - center.y);
    if (dx > radius * 0.92f || dy > radius * 0.88f)
    {
        return false;
    }

    return dx * 0.68f + dy <= radius * 0.98f;
}
