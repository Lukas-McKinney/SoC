#include "renderer_ui.h"

#include "game_logic.h"
#include "localization.h"
#include "match_session.h"
#include "renderer.h"
#include "ui_state.h"
#include "netplay.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct ButtonAnimationState
{
    float hoverAmount;
    float pressAmount;
    float activeAmount;
};

struct DevelopmentHandCard
{
    enum DevelopmentCardType type;
    int count;
    Rectangle bounds;
    float rotation;
    bool playable;
};

static struct ButtonAnimationState gRollDiceButtonAnimation = {0};
static struct ButtonAnimationState gEndTurnButtonAnimation = {0};

static void DrawDie(Rectangle bounds, int value, float tilt, float alpha);
static enum PlayerType LocalHumanPlayer(const struct Map *map);
static bool IsPrivateInfoPinnedToLocalHuman(const struct Map *map);
static const struct PlayerState *CurrentPlayerState(const struct Map *map);
static const char *PlayerName(enum PlayerType player);
static const char *ResourceName(enum ResourceType resource);
static const char *DevelopmentCardTitle(enum DevelopmentCardType type);
static const char *DevelopmentCardDescription(enum DevelopmentCardType type);
static float EaseAnimationValue(float current, float target, float speed);
static Rectangle ScaleRectangleFromCenter(Rectangle bounds, float scale);
static void UpdateTurnButtonAnimation(struct ButtonAnimationState *state, Rectangle bounds, bool interactive);
static void DrawTurnActionButton(Rectangle bounds, const char *label, int fontSize, Color fillColor, Color borderColor, Color textColor, Color glowColor, const struct ButtonAnimationState *state);
static Color DevelopmentCardAccent(enum DevelopmentCardType type);
static float Clamp01(float value);
static float LerpFloat(float a, float b, float t);
static Rectangle LerpRectangle(Rectangle a, Rectangle b, float t);
static float EaseOutCubic(float t);
static float EaseInOutCubic(float t);
static void DrawDevelopmentCardVisual(Font font, Rectangle card, float rotation, enum DevelopmentCardType type, int count, float alpha, bool emphasized);
static void DrawRotatedCardLayer(Rectangle bounds, float rotationDegrees, Color color);
static bool PointInRotatedRectangle(Vector2 point, Rectangle bounds, float rotationDegrees);
static Vector2 RotateVector(Vector2 v, float radians);
static Vector2 TransformCardPoint(Rectangle bounds, float rotationDegrees, Vector2 localPoint);
static void DrawCardText(Font font, Rectangle bounds, float rotationDegrees, Vector2 localPoint, const char *text, int fontSize, Color color);
static int BuildDevelopmentHandLayout(const struct Map *map, struct DevelopmentHandCard cards[DEVELOPMENT_CARD_COUNT]);
static int BuildWrappedUiLines(const char *message, int fontSize, int maxWidth, char lines[][96], int maxLines);
static int FitUiTextFontSize(const char *text, int preferredFontSize, int minFontSize, int maxWidth);
static int BuildWrappedUiLinesFitted(const char *message, int preferredFontSize, int minFontSize, int maxWidth, char lines[][96], int maxLines, int *outFontSize);
static int DrawWrappedUiTextBlock(const char *message, float x, float y, int fontSize, int maxWidth, float lineHeight, Color color);
static Rectangle DevelopmentHandHitBounds(Rectangle bounds);
static void DrawAwardCard(Rectangle bounds, const char *title, const char *subtitle, const char *detail, enum PlayerType owner, Color accent);
static void BuildVictoryHeadline(const struct Map *map, enum PlayerType winner, char *buffer, size_t bufferSize);
static void BuildVictorySubheadline(const struct Map *map, enum PlayerType winner, char *buffer, size_t bufferSize);
static void FormatElapsedDuration(unsigned long long totalSeconds, char *buffer, size_t bufferSize);
static void DrawTurnPanelPlaytime(const struct Map *map, Rectangle panel, Color color);
static const char *NetplayStatusLabel(const struct MatchSession *session);
static bool IsDarkUiTheme(void);
static float GameplayUiScale(void);
static float UiScaled(float value);
static int UiScaledFont(int fontSize);
static Color UiPanelColor(void);
static Color UiPanelHighlightColor(void);
static Color UiSectionFillColor(void);
static Color UiButtonFillColor(void);
static Color UiTrackFillColor(void);
static Color UiPanelBorderColor(void);
static Color UiTextColor(void);
static Color UiMutedTextColor(void);
static Color UiDisabledFillColor(void);
static Color UiDisabledBorderColor(void);
static Color UiDisabledTextColor(void);
static Color UiCloseButtonFillColor(void);
static Color UiReadableAccent(Color color);
static Color UiReadablePlayerColor(enum PlayerType player);
static void DrawPanelFrame(Rectangle panel, float roundness, float shadowOffsetX, float shadowOffsetY, float shadowAlpha, Color fill, Color border);
static void DrawCenteredUiTextFitted(const char *text, Rectangle bounds, int preferredFontSize, int minFontSize, float yOffset, Color color);
static void DrawLeftUiTextFitted(const char *text, Rectangle bounds, float padding, int preferredFontSize, int minFontSize, float yOffset, Color color);
static void DrawModalCloseButton(Rectangle bounds, float alpha, Color fill, Color border, Color textColor);

/* Shared UI copy/layout helpers stay file-local without bloating the main UI file. */
#include "renderer_ui_common.inc"

void DrawBuildPanel(const struct Map *map)
{
    const Rectangle panel = GetBuildPanelBounds();
    const float panelX = panel.x;
    const float panelY = panel.y;
    const float openAmount = uiGetBuildPanelOpenAmount();
    const Color panelColor = UiPanelColor();
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    const float cardWidth = UiScaled(175.0f);
    const float cardHeight = UiScaled(60.0f);
    const Rectangle roadCard = {panelX + UiScaled(10.0f), panelY + UiScaled(50.0f), cardWidth, cardHeight};
    const Rectangle settlementCard = {panelX + UiScaled(196.0f), panelY + UiScaled(50.0f), cardWidth, cardHeight};
    const Rectangle cityCard = {panelX + UiScaled(10.0f), panelY + UiScaled(112.0f), cardWidth, cardHeight};
    const Rectangle devCard = GetBuildPanelDevelopmentCardBounds();
    const Color activeCard = UiPanelHighlightColor();
    const Color armedAccent = (Color){171, 82, 54, 255};
    const Color inactiveCard = UiSectionFillColor();
    const Color disabledCard = UiDisabledFillColor();
    const Color disabledBorder = UiDisabledBorderColor();
    const Color disabledText = UiDisabledTextColor();
    const bool setupSettlementMode = gameIsSetupSettlementTurn(map);
    const bool setupRoadMode = gameIsSetupRoadTurn(map);
    
    /* Get local player to show their affordances, not the current player's */
    const struct MatchSession *session = matchSessionGetActive();
    const enum PlayerType localPlayer = session != NULL ? matchSessionGetLocalPlayer(session) : map->currentPlayer;
    
    /* Check what the LOCAL PLAYER can afford, not what the current player can afford */
    bool canBuyRoad = false;
    bool canBuySettlement = false;
    bool canBuyCity = false;
    bool canBuyDevelopment = false;
    
    if (localPlayer >= PLAYER_RED && localPlayer <= PLAYER_BLACK)
    {
        const struct PlayerState *localPlayerState = &map->players[localPlayer];
        canBuyRoad = setupRoadMode || (localPlayerState->resources[RESOURCE_WOOD] >= 1 && localPlayerState->resources[RESOURCE_CLAY] >= 1);
        canBuySettlement = setupSettlementMode || (localPlayerState->resources[RESOURCE_WOOD] >= 1 && localPlayerState->resources[RESOURCE_WHEAT] >= 1 && 
                                                    localPlayerState->resources[RESOURCE_CLAY] >= 1 && localPlayerState->resources[RESOURCE_SHEEP] >= 1);
        canBuyCity = map->phase == GAME_PHASE_PLAY && (localPlayerState->resources[RESOURCE_WHEAT] >= 2 && localPlayerState->resources[RESOURCE_STONE] >= 3);
        canBuyDevelopment = map->developmentDeckCount > 0 && (localPlayerState->resources[RESOURCE_WHEAT] >= 1 && 
                                                              localPlayerState->resources[RESOURCE_SHEEP] >= 1 && 
                                                              localPlayerState->resources[RESOURCE_STONE] >= 1);
    }
    else
    {
        /* Fallback to current player if local player is invalid */
        canBuyRoad = setupRoadMode || gameCanAffordRoad(map);
        canBuySettlement = setupSettlementMode || gameCanAffordSettlement(map);
        canBuyCity = map->phase == GAME_PHASE_PLAY && gameCanAffordCity(map);
        canBuyDevelopment = gameCanBuyDevelopment(map);
    }
    
    const unsigned char contentAlpha = (unsigned char)(255.0f * ((openAmount - 0.55f) / 0.45f < 0.0f ? 0.0f : ((openAmount - 0.55f) / 0.45f > 1.0f ? 1.0f : (openAmount - 0.55f) / 0.45f)));
    const float contentFade = contentAlpha / 255.0f;

    if (openAmount < 0.98f)
    {
        DrawPanelFrame(panel, 0.12f, UiScaled(6.0f), UiScaled(8.0f), 0.10f, panelColor, borderColor);
        DrawUiText(loc("Build (B)"), panelX + UiScaled(18.0f), panelY + UiScaled(14.0f), UiScaledFont(24), textColor);
        if (!uiIsBuildPanelOpen())
        {
            return;
        }
    }

    if (openAmount < 0.98f)
    {
        return;
    }

    DrawPanelFrame(panel, 0.08f, UiScaled(6.0f), UiScaled(8.0f), 0.10f, panelColor, borderColor);
    DrawUiText(loc("Build (B)"), panelX + UiScaled(18.0f), panelY + UiScaled(14.0f), UiScaledFont(24), textColor);

    DrawRectangleRounded(roadCard, 0.14f, 8, Fade(canBuyRoad ? activeCard : disabledCard, contentFade));
    DrawRectangleLinesEx(roadCard, 2.0f, Fade(gBuildMode == BUILD_MODE_ROAD ? armedAccent : (canBuyRoad ? borderColor : disabledBorder), contentFade));
    DrawLeftUiTextFitted(loc("Road"), (Rectangle){roadCard.x + UiScaled(12.0f), roadCard.y + UiScaled(4.0f), UiScaled(100.0f), UiScaled(21.0f)}, 0.0f, 21, 16, 0.0f, Fade(canBuyRoad ? textColor : disabledText, contentFade));
    DrawLeftUiTextFitted(loc("Wood + Clay"), (Rectangle){roadCard.x + UiScaled(12.0f), roadCard.y + UiScaled(27.0f), UiScaled(102.0f), UiScaled(19.0f)}, 0.0f, 15, 12, 0.0f, Fade(canBuyRoad ? mutedText : disabledText, contentFade));
    DrawLineEx((Vector2){roadCard.x + UiScaled(116.0f), roadCard.y + UiScaled(19.0f)}, (Vector2){roadCard.x + UiScaled(157.0f), roadCard.y + UiScaled(41.0f)}, UiScaled(8.0f), Fade(canBuyRoad ? (Color){230, 41, 55, 255} : Fade((Color){230, 41, 55, 255}, 0.35f), contentFade));
    DrawLineEx((Vector2){roadCard.x + UiScaled(116.0f), roadCard.y + UiScaled(19.0f)}, (Vector2){roadCard.x + UiScaled(157.0f), roadCard.y + UiScaled(41.0f)}, UiScaled(2.0f), Fade(RAYWHITE, 0.24f * contentFade));
    DrawRectangleRounded(settlementCard, 0.14f, 8, Fade(canBuySettlement ? inactiveCard : disabledCard, contentFade));
    DrawRectangleLinesEx(settlementCard, 2.0f, Fade(gBuildMode == BUILD_MODE_SETTLEMENT ? armedAccent : (canBuySettlement ? borderColor : disabledBorder), contentFade));
    DrawLeftUiTextFitted(loc("Settlement"), (Rectangle){settlementCard.x + UiScaled(12.0f), settlementCard.y + UiScaled(4.0f), UiScaled(114.0f), UiScaled(21.0f)}, 0.0f, 21, 15, 0.0f, Fade(canBuySettlement ? textColor : disabledText, contentFade));
    DrawLeftUiTextFitted(loc("Wood Clay Wheat Sheep"), (Rectangle){settlementCard.x + UiScaled(12.0f), settlementCard.y + UiScaled(27.0f), UiScaled(112.0f), UiScaled(19.0f)}, 0.0f, 12, 10, 0.0f, Fade(canBuySettlement ? mutedText : disabledText, contentFade));
    DrawRectangle((int)(settlementCard.x + UiScaled(129.0f)), (int)(settlementCard.y + UiScaled(23.0f)), (int)UiScaled(25.0f), (int)UiScaled(19.0f), Fade(canBuySettlement ? (Color){170, 118, 72, 255} : Fade((Color){170, 118, 72, 255}, 0.45f), contentFade));
    DrawTriangle((Vector2){settlementCard.x + UiScaled(141.0f), settlementCard.y + UiScaled(11.0f)}, (Vector2){settlementCard.x + UiScaled(127.0f), settlementCard.y + UiScaled(26.0f)}, (Vector2){settlementCard.x + UiScaled(155.0f), settlementCard.y + UiScaled(26.0f)}, Fade(canBuySettlement ? (Color){132, 80, 49, 255} : Fade((Color){132, 80, 49, 255}, 0.45f), contentFade));
    DrawRectangleRounded(cityCard, 0.14f, 8, Fade(canBuyCity ? inactiveCard : disabledCard, contentFade));
    DrawRectangleLinesEx(cityCard, 2.0f, Fade(gBuildMode == BUILD_MODE_CITY ? armedAccent : (canBuyCity ? borderColor : disabledBorder), contentFade));
    DrawLeftUiTextFitted(loc("City"), (Rectangle){cityCard.x + UiScaled(12.0f), cityCard.y + UiScaled(4.0f), UiScaled(98.0f), UiScaled(21.0f)}, 0.0f, 21, 16, 0.0f, Fade(canBuyCity ? textColor : disabledText, contentFade));
    DrawLeftUiTextFitted(loc("2 Wheat + 3 Stone"), (Rectangle){cityCard.x + UiScaled(12.0f), cityCard.y + UiScaled(27.0f), UiScaled(110.0f), UiScaled(19.0f)}, 0.0f, 14, 11, 0.0f, Fade(canBuyCity ? mutedText : disabledText, contentFade));
    DrawRectangle((int)(cityCard.x + UiScaled(125.0f)), (int)(cityCard.y + UiScaled(25.0f)), (int)UiScaled(32.0f), (int)UiScaled(19.0f), Fade(canBuyCity ? (Color){170, 118, 72, 255} : Fade((Color){170, 118, 72, 255}, 0.45f), contentFade));
    DrawRectangle((int)(cityCard.x + UiScaled(130.0f)), (int)(cityCard.y + UiScaled(16.0f)), (int)UiScaled(22.0f), (int)UiScaled(13.0f), Fade(canBuyCity ? (Color){170, 118, 72, 255} : Fade((Color){170, 118, 72, 255}, 0.45f), contentFade));
    DrawTriangle((Vector2){cityCard.x + UiScaled(141.0f), cityCard.y + UiScaled(6.0f)}, (Vector2){cityCard.x + UiScaled(129.0f), cityCard.y + UiScaled(17.0f)}, (Vector2){cityCard.x + UiScaled(153.0f), cityCard.y + UiScaled(17.0f)}, Fade(canBuyCity ? (Color){132, 80, 49, 255} : Fade((Color){132, 80, 49, 255}, 0.45f), contentFade));
    DrawRectangleRounded(devCard, 0.14f, 8, Fade(canBuyDevelopment ? inactiveCard : disabledCard, contentFade));
    DrawRectangleLinesEx(devCard, 2.0f, Fade(canBuyDevelopment ? borderColor : disabledBorder, contentFade));
    DrawLeftUiTextFitted(loc("Development"), (Rectangle){devCard.x + UiScaled(12.0f), devCard.y + UiScaled(4.0f), UiScaled(116.0f), UiScaled(21.0f)}, 0.0f, 19, 14, 0.0f, Fade(canBuyDevelopment ? textColor : disabledText, contentFade));
    DrawLeftUiTextFitted(loc("Wheat Sheep Stone"), (Rectangle){devCard.x + UiScaled(12.0f), devCard.y + UiScaled(27.0f), UiScaled(112.0f), UiScaled(19.0f)}, 0.0f, 14, 11, 0.0f, Fade(canBuyDevelopment ? mutedText : disabledText, contentFade));
    DrawRectangleRounded((Rectangle){devCard.x + UiScaled(129.0f), devCard.y + UiScaled(11.0f), UiScaled(23.0f), UiScaled(34.0f)}, 0.12f, 6, Fade(canBuyDevelopment ? (Color){191, 171, 111, 255} : Fade((Color){191, 171, 111, 255}, 0.45f), contentFade));
    DrawRectangleLinesEx((Rectangle){devCard.x + UiScaled(129.0f), devCard.y + UiScaled(11.0f), UiScaled(23.0f), UiScaled(34.0f)}, UiScaled(1.5f), Fade(canBuyDevelopment ? (Color){120, 96, 53, 255} : disabledBorder, contentFade));
}

void DrawTradeButton(const struct Map *map)
{
    const Rectangle button = GetTradeButtonBounds();
    const bool isPlayable = map->phase == GAME_PHASE_PLAY;
    DrawRectangleRounded(button, 0.18f, 8, isPlayable ? UiButtonFillColor() : UiDisabledFillColor());
    DrawRectangleLinesEx(button, 2.0f, UiPanelBorderColor());
    DrawCenteredUiTextFitted(loc("Water Trade (W)"), button, 20, 14, 0.0f, isPlayable ? UiTextColor() : UiDisabledTextColor());
}

void DrawPlayerTradeButton(const struct Map *map)
{
    const Rectangle button = GetPlayerTradeButtonBounds();
    const bool isPlayable = map->phase == GAME_PHASE_PLAY;
    DrawRectangleRounded(button, 0.18f, 8, isPlayable ? UiButtonFillColor() : UiDisabledFillColor());
    DrawRectangleLinesEx(button, 2.0f, UiPanelBorderColor());
    DrawCenteredUiTextFitted(loc("Player Trade (T)"), button, 20, 14, 0.0f, isPlayable ? UiTextColor() : UiDisabledTextColor());
}

void DrawSettingsButton(void)
{
    const Rectangle button = GetSettingsButtonBounds();
    DrawRectangleRounded(button, 0.18f, 8, UiButtonFillColor());
    DrawRectangleLinesEx(button, 2.0f, UiPanelBorderColor());
    DrawCenteredUiTextFitted(loc("Settings (Esc)"), button, 20, 14, 0.0f, UiTextColor());
}

void DrawSettingsModal(void)
{
    const Rectangle targetMenu = GetSettingsModalBounds();
    const Rectangle targetSubmenu = GetSettingsSubmenuBounds();
    const Rectangle targetMultiplayer = GetSettingsMultiplayerPanelBounds();
    const float openAmount = uiGetSettingsMenuOpenAmount();
    const float eased = openAmount * openAmount * (3.0f - 2.0f * openAmount);
    const float scale = 0.9f + 0.1f * eased;
    const Rectangle menuPanel = ScaleRectangleFromCenter(targetMenu, scale);
    const Rectangle submenuPanel = ScaleRectangleFromCenter(targetSubmenu, scale);
    const Rectangle multiplayerPanel = ScaleRectangleFromCenter(targetMultiplayer, scale);
    const Rectangle closeButton = {menuPanel.x + menuPanel.width - 42.0f, menuPanel.y + 12.0f, 28.0f, 28.0f};
    const Rectangle settingsButton = {menuPanel.x + 24.0f, menuPanel.y + 82.0f, menuPanel.width - 48.0f, 42.0f};
    const Rectangle restartButton = {menuPanel.x + 24.0f, menuPanel.y + 136.0f, menuPanel.width - 48.0f, 42.0f};
    const Rectangle backToMenuButton = {menuPanel.x + 24.0f, menuPanel.y + 190.0f, menuPanel.width - 48.0f, 42.0f};
    const Rectangle quitButton = {menuPanel.x + 24.0f, menuPanel.y + 244.0f, menuPanel.width - 48.0f, 42.0f};
    const Rectangle confirmPanel = {menuPanel.x + 18.0f, menuPanel.y + menuPanel.height - 166.0f, menuPanel.width - 36.0f, 150.0f};
    const Rectangle confirmButton = {confirmPanel.x + 18.0f, confirmPanel.y + confirmPanel.height - 46.0f, 144.0f, 30.0f};
    const Rectangle cancelButton = {confirmPanel.x + confirmPanel.width - 110.0f, confirmPanel.y + confirmPanel.height - 46.0f, 92.0f, 30.0f};
    const bool submenuOpen = uiIsSettingsSubmenuOpen();
    const struct MatchSession *session = matchSessionGetActive();
    const bool showNetplayPanel = matchSessionIsNetplay(session);
    const Color panelColor = UiPanelColor();
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    const Color sectionFill = UiSectionFillColor();
    const Color sectionBorder = UiDisabledBorderColor();
    const Color accent = (Color){171, 82, 54, 255};
    const bool lightSelected = uiGetTheme() == UI_THEME_LIGHT;
    const bool darkSelected = uiGetTheme() == UI_THEME_DARK;
    const enum UiSettingsConfirmAction confirmAction = uiGetSettingsConfirmAction();
    const bool showingConfirm = confirmAction != UI_SETTINGS_CONFIRM_NONE;
    const enum UiWindowMode windowMode = uiGetWindowMode();
    const int aiSpeed = uiGetAiSpeedSetting();
    const float aiSpeedNormalized = (float)aiSpeed / 10.0f;
    const Vector2 mouse = GetMousePosition();
    const char *localIp = "";
    char portValue[16];
    char ipLine[96];
    char portLine[64];
    char themeLabel[48];
    char displayLabel[48];
    char languageLabel[64];
    const Rectangle submenuCloseButton = {submenuPanel.x + submenuPanel.width - 42.0f, submenuPanel.y + 12.0f, 28.0f, 28.0f};
    const Rectangle lightButton = {submenuPanel.x + 24.0f, submenuPanel.y + 82.0f, submenuPanel.width - 48.0f, 42.0f};
    const Rectangle darkButton = {submenuPanel.x + 24.0f, submenuPanel.y + 136.0f, submenuPanel.width - 48.0f, 42.0f};
    const Rectangle windowModeButton = {submenuPanel.x + 24.0f, submenuPanel.y + 190.0f, submenuPanel.width - 48.0f, 42.0f};
    const Rectangle languageButton = {submenuPanel.x + 24.0f, submenuPanel.y + 244.0f, submenuPanel.width - 48.0f, 42.0f};
    const Rectangle aiSpeedTrack = {submenuPanel.x + 36.0f, submenuPanel.y + 336.0f, submenuPanel.width - 72.0f, 10.0f};
    const float aiSpeedKnobX = aiSpeedTrack.x + aiSpeedTrack.width * aiSpeedNormalized;
    const Rectangle multiplayerIpRow = {multiplayerPanel.x + 18.0f, multiplayerPanel.y + 74.0f, multiplayerPanel.width - 36.0f, 34.0f};
    const Rectangle multiplayerPortRow = {multiplayerPanel.x + 18.0f, multiplayerPanel.y + 116.0f, multiplayerPanel.width - 36.0f, 34.0f};
    const bool revealLocalIp = CheckCollisionPointRec(mouse, multiplayerIpRow);
    const bool revealPort = CheckCollisionPointRec(mouse, multiplayerPortRow);
    char confirmTitleLines[2][96] = {{0}};
    char confirmBodyLines[3][96] = {{0}};
    const char *confirmTitle = "";
    const char *confirmBody = "";
    const char *confirmButtonLabel = "";
    Color confirmButtonColor = (Color){182, 141, 97, 255};
    int confirmTitleFont = 24;
    int confirmBodyFont = 17;
    int confirmTitleLineCount = 0;
    int confirmBodyLineCount = 0;
    int confirmButtonFont = 17;
    int cancelButtonFont = 17;

    if (session != NULL && session->netplay != NULL)
    {
        localIp = netplayGetLocalAddress(session->netplay);
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

    snprintf(ipLine,
             sizeof(ipLine),
             "%s: %s",
             loc("Your IP"),
             revealLocalIp ? (localIp[0] != '\0' ? localIp : "-") : "***.***.***.***");
    snprintf(portValue, sizeof(portValue), "%s", revealPort ? TextFormat("%u", (unsigned int)((session != NULL && session->netplay != NULL) ? netplayGetPort(session->netplay) : 0u)) : "*****");
    snprintf(portLine, sizeof(portLine), "%s: %s", loc("Port"), portValue);

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.24f * eased));
    DrawPanelFrame(menuPanel, 0.08f, 8.0f, 10.0f, 0.14f * eased, Fade(panelColor, eased), Fade(borderColor, eased));

    if (submenuOpen)
    {
        DrawPanelFrame(submenuPanel, 0.08f, 8.0f, 10.0f, 0.12f * eased, Fade(panelColor, eased), Fade(borderColor, eased));
    }

    if (showNetplayPanel)
    {
        DrawPanelFrame(multiplayerPanel, 0.08f, 8.0f, 10.0f, 0.12f * eased, Fade(panelColor, eased), Fade(borderColor, eased));
    }

    if (openAmount < 0.90f)
    {
        return;
    }

    DrawUiText(loc("Menu"), menuPanel.x + 22.0f, menuPanel.y + 18.0f, 28, textColor);
    DrawModalCloseButton(closeButton, 1.0f, UiCloseButtonFillColor(), borderColor, textColor);

    DrawRectangleRounded(settingsButton, 0.18f, 8, submenuOpen ? accent : sectionFill);
    DrawRectangleLinesEx(settingsButton, 2.0f, submenuOpen ? borderColor : sectionBorder);
    DrawLeftUiTextFitted(loc("Settings"), settingsButton, 18.0f, 20, 14, 0.0f, submenuOpen ? RAYWHITE : textColor);

    DrawRectangleRounded(restartButton, 0.18f, 8, sectionFill);
    DrawRectangleLinesEx(restartButton, 2.0f, sectionBorder);
    DrawLeftUiTextFitted(loc("Restart Game"), restartButton, 18.0f, 20, 14, 0.0f, textColor);

    DrawRectangleRounded(backToMenuButton, 0.18f, 8, sectionFill);
    DrawRectangleLinesEx(backToMenuButton, 2.0f, sectionBorder);
    DrawLeftUiTextFitted(loc("Back to Menu"), backToMenuButton, 18.0f, 20, 14, 0.0f, textColor);

    DrawRectangleRounded(quitButton, 0.18f, 8, accent);
    DrawRectangleLinesEx(quitButton, 2.0f, borderColor);
    DrawLeftUiTextFitted(loc("Quit Game"), quitButton, 18.0f, 20, 14, 0.0f, RAYWHITE);

    if (submenuOpen)
    {
        DrawUiText(loc("Settings"), submenuPanel.x + 22.0f, submenuPanel.y + 18.0f, 28, textColor);
        DrawModalCloseButton(submenuCloseButton, 1.0f, UiCloseButtonFillColor(), borderColor, textColor);

        DrawRectangleRounded(lightButton, 0.18f, 8, lightSelected ? accent : sectionFill);
        DrawRectangleLinesEx(lightButton, 2.0f, lightSelected ? borderColor : sectionBorder);
        DrawLeftUiTextFitted(loc("Light Mode"), lightButton, 18.0f, 20, 14, 0.0f, lightSelected ? RAYWHITE : textColor);

        DrawRectangleRounded(darkButton, 0.18f, 8, darkSelected ? accent : sectionFill);
        DrawRectangleLinesEx(darkButton, 2.0f, darkSelected ? borderColor : sectionBorder);
        DrawLeftUiTextFitted(loc("Dark Mode"), darkButton, 18.0f, 20, 14, 0.0f, darkSelected ? RAYWHITE : textColor);

        DrawRectangleRounded(windowModeButton, 0.18f, 8, windowMode == UI_WINDOW_MODE_FULLSCREEN ? accent : sectionFill);
        DrawRectangleLinesEx(windowModeButton, 2.0f, windowMode == UI_WINDOW_MODE_FULLSCREEN ? borderColor : sectionBorder);
        DrawLeftUiTextFitted(displayLabel, windowModeButton, 18.0f, 19, 13, 0.0f, windowMode == UI_WINDOW_MODE_FULLSCREEN ? RAYWHITE : textColor);

        DrawRectangleRounded(languageButton, 0.18f, 8, sectionFill);
        DrawRectangleLinesEx(languageButton, 2.0f, sectionBorder);
        DrawLeftUiTextFitted(languageLabel, languageButton, 18.0f, 20, 13, 0.0f, textColor);

        DrawUiText(loc("AI Speed"), submenuPanel.x + 24.0f, submenuPanel.y + 300.0f, 18, mutedText);
        DrawUiText(loc("0 slow"), aiSpeedTrack.x, aiSpeedTrack.y + 16.0f, 14, mutedText);
        DrawUiText(loc("10 instant"), aiSpeedTrack.x + aiSpeedTrack.width - MeasureUiText(loc("10 instant"), 14), aiSpeedTrack.y + 16.0f, 14, mutedText);
        DrawRectangleRounded(aiSpeedTrack, 0.45f, 8, UiTrackFillColor());
        DrawRectangleRounded((Rectangle){aiSpeedTrack.x, aiSpeedTrack.y, aiSpeedTrack.width * aiSpeedNormalized, aiSpeedTrack.height}, 0.45f, 8, accent);
        DrawCircleV((Vector2){aiSpeedKnobX, aiSpeedTrack.y + aiSpeedTrack.height * 0.5f}, 9.0f, UiPanelHighlightColor());
        DrawCircleLines((int)aiSpeedKnobX, (int)(aiSpeedTrack.y + aiSpeedTrack.height * 0.5f), 9.0f, borderColor);
        DrawUiText(TextFormat("%d", aiSpeed), submenuPanel.x + submenuPanel.width * 0.5f - 5.0f, aiSpeedTrack.y - 24.0f, 18, textColor);

        DrawUiText(themeLabel, submenuPanel.x + 24.0f, submenuPanel.y + 64.0f, 16, mutedText);
    }

    if (showNetplayPanel)
    {
        DrawUiText(loc("Multiplayer Options"), multiplayerPanel.x + 18.0f, multiplayerPanel.y + 18.0f, 24, textColor);
        DrawRectangleRounded(multiplayerIpRow, 0.16f, 8, revealLocalIp ? Fade(accent, 0.16f) : Fade(sectionFill, 0.94f));
        DrawRectangleLinesEx(multiplayerIpRow, 1.5f, revealLocalIp ? Fade(borderColor, 0.90f) : Fade(sectionBorder, 0.90f));
        DrawLeftUiTextFitted(ipLine, multiplayerIpRow, 12.0f, 17, 13, 0.0f, textColor);

        DrawRectangleRounded(multiplayerPortRow, 0.16f, 8, revealPort ? Fade(accent, 0.16f) : Fade(sectionFill, 0.94f));
        DrawRectangleLinesEx(multiplayerPortRow, 1.5f, revealPort ? Fade(borderColor, 0.90f) : Fade(sectionBorder, 0.90f));
        DrawLeftUiTextFitted(portLine, multiplayerPortRow, 12.0f, 17, 13, 0.0f, textColor);
    }

    if (!showingConfirm)
    {
        return;
    }

    if (confirmAction == UI_SETTINGS_CONFIRM_MAIN_MENU)
    {
        confirmTitle = loc("Back to main menu?");
        confirmBody = loc("Current progress will be lost.");
        confirmButtonLabel = loc("Return to Menu");
    }
    else if (confirmAction == UI_SETTINGS_CONFIRM_RESTART)
    {
        confirmTitle = loc("Restart game?");
        confirmBody = loc("Current progress will be lost.");
        confirmButtonLabel = loc("Confirm Restart");
    }
    else
    {
        confirmTitle = loc("Quit game?");
        confirmBody = loc("The application will close.");
        confirmButtonLabel = loc("Confirm Quit");
        confirmButtonColor = accent;
    }

    confirmTitleLineCount = BuildWrappedUiLinesFitted(confirmTitle, 24, 18, (int)confirmPanel.width - 36, confirmTitleLines, 2, &confirmTitleFont);
    confirmBodyLineCount = BuildWrappedUiLinesFitted(confirmBody, 17, 14, (int)confirmPanel.width - 36, confirmBodyLines, 3, &confirmBodyFont);

    DrawPanelFrame(confirmPanel, 0.08f, 4.0f, 6.0f, 0.08f, UiPanelHighlightColor(), borderColor);

    for (int i = 0; i < confirmTitleLineCount; i++)
    {
        DrawUiText(confirmTitleLines[i], confirmPanel.x + 18.0f, confirmPanel.y + 16.0f + (float)i * ((float)confirmTitleFont + 4.0f), confirmTitleFont, textColor);
    }
    for (int i = 0; i < confirmBodyLineCount; i++)
    {
        DrawUiText(
            confirmBodyLines[i],
            confirmPanel.x + 18.0f,
            confirmPanel.y + 24.0f + (float)confirmTitleLineCount * ((float)confirmTitleFont + 4.0f) + (float)i * ((float)confirmBodyFont + 3.0f),
            confirmBodyFont,
            mutedText);
    }

    {
        const int confirmLabelMaxWidth = (int)confirmButton.width - 20;
        confirmButtonFont = FitUiTextFontSize(confirmButtonLabel, 17, 14, confirmLabelMaxWidth);
        DrawRectangleRounded(confirmButton, 0.18f, 8, confirmButtonColor);
        DrawRectangleLinesEx(confirmButton, 1.5f, borderColor);
        DrawCenteredUiTextFitted(confirmButtonLabel, confirmButton, confirmButtonFont, 14, 0.0f, RAYWHITE);
    }

    {
        const char *cancelLabel = loc("Cancel");
        const int cancelLabelMaxWidth = (int)cancelButton.width - 20;
        cancelButtonFont = FitUiTextFontSize(cancelLabel, 17, 14, cancelLabelMaxWidth);
        DrawRectangleRounded(cancelButton, 0.18f, 8, sectionFill);
        DrawRectangleLinesEx(cancelButton, 1.5f, sectionBorder);
        DrawCenteredUiTextFitted(cancelLabel, cancelButton, cancelButtonFont, 14, 0.0f, textColor);
    }
}

void DrawTradeModal(const struct Map *map)
{
    const Rectangle targetPanel = GetTradeModalBounds();
    const float openAmount = uiGetTradeMenuOpenAmount();
    const float eased = openAmount * openAmount * (3.0f - 2.0f * openAmount);
    const float scale = 0.88f + 0.12f * eased;
    const Rectangle panel = {
        targetPanel.x + targetPanel.width * (1.0f - scale) * 0.5f,
        targetPanel.y + targetPanel.height * (1.0f - scale) * 0.5f,
        targetPanel.width * scale,
        targetPanel.height * scale};
    const Rectangle closeButton = {panel.x + panel.width - 42.0f, panel.y + 12.0f, 28.0f, 28.0f};
    const Rectangle confirmButton = {panel.x + 26.0f * scale, panel.y + panel.height - 48.0f * scale, 408.0f * scale, 34.0f * scale};
    const Color panelColor = UiPanelColor();
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    const Color sectionFill = UiSectionFillColor();
    const Color disabledFill = UiDisabledFillColor();
    const Color disabledBorder = UiDisabledBorderColor();
    const Color disabledText = UiDisabledTextColor();
    const bool isPlayable = map->phase == GAME_PHASE_PLAY;
    const bool canTrade = isPlayable && gameCanTradeMaritime(map, (enum ResourceType)gTradeGiveResource, gTradeAmount, (enum ResourceType)gTradeReceiveResource);
    const float alpha = eased;
    const int rate = gameGetMaritimeTradeRate(map, (enum ResourceType)gTradeGiveResource);
    const int maxTradeAmount = map->players[map->currentPlayer].resources[gTradeGiveResource] / rate;
    const float amountFill = maxTradeAmount > 0 ? (float)gTradeAmount / (float)maxTradeAmount : 0.0f;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.28f * alpha));
    DrawPanelFrame(panel, 0.06f, 8.0f, 10.0f, 0.14f * alpha, Fade(panelColor, alpha), Fade(borderColor, alpha));
    if (openAmount < 0.90f)
    {
        return;
    }

    DrawUiText(loc("Available Water Trades"), panel.x + 22.0f, panel.y + 16.0f, 26, Fade(textColor, alpha));
    DrawModalCloseButton(closeButton, alpha, UiCloseButtonFillColor(), borderColor, textColor);

    DrawUiText(loc("Give"), panel.x + 24.0f, panel.y + 48.0f, 18, Fade(mutedText, alpha));
    DrawUiText(loc("Receive"), panel.x + 232.0f, panel.y + 48.0f, 18, Fade(mutedText, alpha));
    for (int i = 0; i < 5; i++)
    {
        const int optionRate = gameGetMaritimeTradeRate(map, (enum ResourceType)i);
        const bool available = isPlayable && map->players[map->currentPlayer].resources[i] >= optionRate;
        const bool selected = i == gTradeGiveResource;
        const bool receiveSelected = i == gTradeReceiveResource;
        const bool receiveAllowed = i != gTradeGiveResource;
        const Rectangle giveOption = {panel.x + 24.0f, panel.y + 74.0f + i * 34.0f, 180.0f, 28.0f};
        const Rectangle receiveOption = {panel.x + 232.0f, panel.y + 74.0f + i * 34.0f, 180.0f, 28.0f};

        DrawRectangleRounded(giveOption, 0.18f, 6, selected ? (Color){171, 82, 54, 255} : (available ? sectionFill : disabledFill));
        DrawRectangleLinesEx(giveOption, 1.5f, selected ? borderColor : (available ? borderColor : disabledBorder));
        DrawLeftUiTextFitted(
            TextFormat("%s  %d:1", ResourceName((enum ResourceType)i), optionRate),
            giveOption,
            10.0f,
            16,
            12,
            0.0f,
            selected ? RAYWHITE : (available ? textColor : disabledText));

        DrawRectangleRounded(receiveOption, 0.18f, 6, receiveSelected ? (Color){171, 82, 54, 255} : (receiveAllowed ? sectionFill : disabledFill));
        DrawRectangleLinesEx(receiveOption, 1.5f, receiveSelected ? borderColor : (receiveAllowed ? borderColor : disabledBorder));
        DrawLeftUiTextFitted(
            ResourceName((enum ResourceType)i),
            receiveOption,
            10.0f,
            16,
            12,
            0.0f,
            receiveSelected ? RAYWHITE : (receiveAllowed ? textColor : disabledText));
    }

    DrawUiText(loc("Amount"), panel.x + panel.width * 0.5f - 32.0f, panel.y + 248.0f, 18, Fade(mutedText, alpha));
    const Rectangle tradeMinus = {panel.x + panel.width * 0.5f - 112.0f, panel.y + 276.0f, 28.0f, 28.0f};
    const Rectangle tradeTrack = {panel.x + panel.width * 0.5f - 68.0f, panel.y + 284.0f, 136.0f, 12.0f};
    const Rectangle tradePlus = {panel.x + panel.width * 0.5f + 84.0f, panel.y + 276.0f, 28.0f, 28.0f};
    DrawRectangleRounded(tradeMinus, 0.22f, 6, UiDisabledFillColor());
    DrawRectangleLinesEx(tradeMinus, 1.5f, borderColor);
    DrawCenteredUiTextFitted("-", tradeMinus, 24, 20, -1.0f, textColor);
    DrawRectangleRounded(tradeTrack, 0.45f, 8, UiTrackFillColor());
    DrawRectangleRounded((Rectangle){tradeTrack.x, tradeTrack.y, tradeTrack.width * amountFill, tradeTrack.height}, 0.45f, 8, (Color){171, 82, 54, 255});
    {
        const char *amountLabel = TextFormat("%d", gTradeAmount);
        const int amountWidth = MeasureUiText(amountLabel, 18);
        DrawUiText(amountLabel, tradeTrack.x + tradeTrack.width * 0.5f - amountWidth * 0.5f, tradeTrack.y - 20.0f, 18, textColor);
    }
    DrawRectangleRounded(tradePlus, 0.22f, 6, UiDisabledFillColor());
    DrawRectangleLinesEx(tradePlus, 1.5f, borderColor);
    DrawCenteredUiTextFitted("+", tradePlus, 24, 20, -1.0f, textColor);

    DrawRectangleRounded(confirmButton, 0.16f, 8, canTrade ? (Color){171, 82, 54, 255} : UiDisabledFillColor());
    DrawRectangleLinesEx(confirmButton, 2.0f, canTrade ? borderColor : UiDisabledBorderColor());
    {
        const char *confirmLabel = TextFormat(loc("Trade %d %s for %d %s"), rate * gTradeAmount, ResourceName((enum ResourceType)gTradeGiveResource), gTradeAmount, ResourceName((enum ResourceType)gTradeReceiveResource));
        DrawCenteredUiTextFitted(confirmLabel, confirmButton, 18, 12, 0.0f, canTrade ? RAYWHITE : disabledText);
    }
}

void DrawPlayerTradeModal(const struct Map *map)
{
    const Rectangle targetPanel = GetPlayerTradeModalBounds();
    const float openAmount = uiGetPlayerTradeMenuOpenAmount();
    const float eased = openAmount * openAmount * (3.0f - 2.0f * openAmount);
    const float scale = 0.88f + 0.12f * eased;
    const Rectangle panel = {
        targetPanel.x + targetPanel.width * (1.0f - scale) * 0.5f,
        targetPanel.y + targetPanel.height * (1.0f - scale) * 0.5f,
        targetPanel.width * scale,
        targetPanel.height * scale};
    const Rectangle closeButton = {panel.x + panel.width - 42.0f, panel.y + 12.0f, 28.0f, 28.0f};
    const Rectangle confirmButton = {panel.x + 54.0f, panel.y + panel.height - 48.0f, 332.0f, 34.0f};
    const Color panelColor = UiPanelColor();
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    const Color sectionFill = UiSectionFillColor();
    const Color disabledFill = UiDisabledFillColor();
    const Color disabledBorder = UiDisabledBorderColor();
    const Color disabledText = UiDisabledTextColor();
    const bool isPlayable = map->phase == GAME_PHASE_PLAY;
    const bool canTrade = isPlayable &&
                          gPlayerTradeGiveResource != gPlayerTradeReceiveResource &&
                          map->players[map->currentPlayer].resources[gPlayerTradeGiveResource] >= gPlayerTradeGiveAmount;
    const float alpha = eased;
    const int maxGiveAmount = map->players[map->currentPlayer].resources[gPlayerTradeGiveResource] > 0 ? map->players[map->currentPlayer].resources[gPlayerTradeGiveResource] : 1;
    const int maxReceiveAmount = 9;
    const float giveFill = (float)gPlayerTradeGiveAmount / (float)maxGiveAmount;
    const float receiveFill = (float)gPlayerTradeReceiveAmount / (float)maxReceiveAmount;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.28f * alpha));
    DrawPanelFrame(panel, 0.06f, 8.0f, 10.0f, 0.14f * alpha, Fade(panelColor, alpha), Fade(borderColor, alpha));
    if (openAmount < 0.90f)
    {
        return;
    }

    DrawUiText(loc("Player Trade"), panel.x + 22.0f, panel.y + 16.0f, 26, Fade(textColor, alpha));
    DrawModalCloseButton(closeButton, alpha, UiCloseButtonFillColor(), borderColor, textColor);

    DrawUiText(loc("Trade With"), panel.x + 24.0f, panel.y + 48.0f, 18, Fade(mutedText, alpha));
    int playerSlot = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        const enum PlayerType candidate = (enum PlayerType)i;
        if (candidate == map->currentPlayer ||
            !gameIsPlayerActive(map, candidate))
        {
            continue;
        }

        const bool selected = candidate == gPlayerTradeTarget;
        const Rectangle playerOption = {panel.x + 24.0f + playerSlot * 130.0f, panel.y + 74.0f, 116.0f, 32.0f};
        DrawRectangleRounded(playerOption, 0.18f, 6, selected ? Fade(UiReadablePlayerColor(candidate), 0.95f) : sectionFill);
        DrawRectangleLinesEx(playerOption, 1.5f, selected ? borderColor : UiDisabledBorderColor());
        {
            const char *label = PlayerName(candidate);
            DrawCenteredUiTextFitted(label, playerOption, 16, 12, 0.0f, selected ? RAYWHITE : textColor);
        }
        playerSlot++;
    }

    DrawUiText(loc("You Give"), panel.x + 24.0f, panel.y + 132.0f, 18, Fade(mutedText, alpha));
    DrawUiText(loc("You Receive"), panel.x + 232.0f, panel.y + 132.0f, 18, Fade(mutedText, alpha));
    for (int i = 0; i < 5; i++)
    {
        const bool giveAvailable = map->players[map->currentPlayer].resources[i] > 0;
        const bool giveSelected = i == gPlayerTradeGiveResource && giveAvailable;
        const bool receiveAllowed = i != gPlayerTradeGiveResource;
        const bool receiveSelected = i == gPlayerTradeReceiveResource && receiveAllowed;
        const Rectangle giveOption = {panel.x + 24.0f, panel.y + 160.0f + i * 28.0f, 180.0f, 22.0f};
        const Rectangle receiveOption = {panel.x + 232.0f, panel.y + 160.0f + i * 28.0f, 180.0f, 22.0f};
        DrawRectangleRounded(giveOption, 0.18f, 6, giveSelected ? (Color){171, 82, 54, 255} : (giveAvailable ? sectionFill : disabledFill));
        DrawRectangleLinesEx(giveOption, 1.5f, giveSelected ? borderColor : (giveAvailable ? borderColor : disabledBorder));
        DrawLeftUiTextFitted(
            TextFormat("%s  %d", ResourceName((enum ResourceType)i), map->players[map->currentPlayer].resources[i]),
            giveOption,
            10.0f,
            14,
            11,
            0.0f,
            giveSelected ? RAYWHITE : (giveAvailable ? textColor : disabledText));

        DrawRectangleRounded(receiveOption, 0.18f, 6, receiveSelected ? (Color){171, 82, 54, 255} : (receiveAllowed ? sectionFill : disabledFill));
        DrawRectangleLinesEx(receiveOption, 1.5f, receiveSelected ? borderColor : (receiveAllowed ? borderColor : disabledBorder));
        DrawLeftUiTextFitted(
            ResourceName((enum ResourceType)i),
            receiveOption,
            10.0f,
            14,
            11,
            0.0f,
            receiveSelected ? RAYWHITE : (receiveAllowed ? textColor : disabledText));
    }

    DrawUiText(loc("Your Amount"), panel.x + 24.0f, panel.y + 294.0f, 18, Fade(mutedText, alpha));
    const Rectangle giveMinus = {panel.x + 24.0f, panel.y + 326.0f, 28.0f, 28.0f};
    const Rectangle giveTrack = {panel.x + 68.0f, panel.y + 334.0f, 92.0f, 12.0f};
    const Rectangle givePlus = {panel.x + 176.0f, panel.y + 326.0f, 28.0f, 28.0f};
    DrawRectangleRounded(giveMinus, 0.22f, 6, UiDisabledFillColor());
    DrawRectangleLinesEx(giveMinus, 1.5f, borderColor);
    DrawCenteredUiTextFitted("-", giveMinus, 24, 20, -1.0f, textColor);
    DrawRectangleRounded(giveTrack, 0.45f, 8, UiTrackFillColor());
    DrawRectangleRounded((Rectangle){giveTrack.x, giveTrack.y, giveTrack.width * giveFill, giveTrack.height}, 0.45f, 8, (Color){171, 82, 54, 255});
    {
        const char *amountLabel = TextFormat("%d", gPlayerTradeGiveAmount);
        const int amountWidth = MeasureUiText(amountLabel, 18);
        DrawUiText(amountLabel, giveTrack.x + giveTrack.width * 0.5f - amountWidth * 0.5f, giveTrack.y - 20.0f, 18, textColor);
    }
    DrawRectangleRounded(givePlus, 0.22f, 6, UiDisabledFillColor());
    DrawRectangleLinesEx(givePlus, 1.5f, borderColor);
    DrawCenteredUiTextFitted("+", givePlus, 24, 20, -1.0f, textColor);

    DrawUiText(loc("Their Amount"), panel.x + 232.0f, panel.y + 294.0f, 18, Fade(mutedText, alpha));
    const Rectangle receiveMinus = {panel.x + 232.0f, panel.y + 326.0f, 28.0f, 28.0f};
    const Rectangle receiveTrack = {panel.x + 276.0f, panel.y + 334.0f, 92.0f, 12.0f};
    const Rectangle receivePlus = {panel.x + 384.0f, panel.y + 326.0f, 28.0f, 28.0f};
    DrawRectangleRounded(receiveMinus, 0.22f, 6, UiDisabledFillColor());
    DrawRectangleLinesEx(receiveMinus, 1.5f, borderColor);
    DrawCenteredUiTextFitted("-", receiveMinus, 24, 20, -1.0f, textColor);
    DrawRectangleRounded(receiveTrack, 0.45f, 8, UiTrackFillColor());
    DrawRectangleRounded((Rectangle){receiveTrack.x, receiveTrack.y, receiveTrack.width * receiveFill, receiveTrack.height}, 0.45f, 8, (Color){171, 82, 54, 255});
    {
        const char *amountLabel = TextFormat("%d", gPlayerTradeReceiveAmount);
        const int amountWidth = MeasureUiText(amountLabel, 18);
        DrawUiText(amountLabel, receiveTrack.x + receiveTrack.width * 0.5f - amountWidth * 0.5f, receiveTrack.y - 20.0f, 18, textColor);
    }
    DrawRectangleRounded(receivePlus, 0.22f, 6, UiDisabledFillColor());
    DrawRectangleLinesEx(receivePlus, 1.5f, borderColor);
    DrawCenteredUiTextFitted("+", receivePlus, 24, 20, -1.0f, textColor);

    DrawRectangleRounded(confirmButton, 0.16f, 8, canTrade ? (Color){171, 82, 54, 255} : UiDisabledFillColor());
    DrawRectangleLinesEx(confirmButton, 2.0f, canTrade ? borderColor : UiDisabledBorderColor());
    {
        const char *confirmLabel = TextFormat(loc("Trade %d %s for %d %s"), gPlayerTradeGiveAmount, ResourceName((enum ResourceType)gPlayerTradeGiveResource), gPlayerTradeReceiveAmount, ResourceName((enum ResourceType)gPlayerTradeReceiveResource));
        DrawCenteredUiTextFitted(confirmLabel, confirmButton, 18, 12, 0.0f, canTrade ? RAYWHITE : disabledText);
    }
}

void DrawIncomingTradeOfferModal(const struct Map *map)
{
    struct GameAction offer;
    const struct MatchSession *session = matchSessionGetActive();
    const Vector2 mouse = GetMousePosition();
    const Rectangle panel = GetIncomingTradeOfferModalBounds();
    const Rectangle acceptButton = GetIncomingTradeOfferAcceptButtonBounds();
    const Rectangle declineButton = GetIncomingTradeOfferDeclineButtonBounds();
    const Color borderColor = (Color){118, 88, 56, 255};
    const bool acceptHovered = CheckCollisionPointRec(mouse, acceptButton);
    const bool declineHovered = CheckCollisionPointRec(mouse, declineButton);
    const bool acceptPressed = acceptHovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool declinePressed = declineHovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const float acceptYOffset = acceptPressed ? 2.0f : (acceptHovered ? -2.0f : 0.0f);
    const float declineYOffset = declinePressed ? 2.0f : (declineHovered ? -2.0f : 0.0f);
    const Rectangle acceptBody = {acceptButton.x, acceptButton.y + acceptYOffset, acceptButton.width, acceptButton.height};
    const Rectangle declineBody = {declineButton.x, declineButton.y + declineYOffset, declineButton.width, declineButton.height};
    const Color acceptFill = acceptHovered ? ColorBrightness((Color){84, 146, 82, 255}, 0.08f) : (Color){84, 146, 82, 255};
    const Color acceptBorder = acceptHovered ? ColorBrightness((Color){60, 108, 58, 255}, 0.08f) : (Color){60, 108, 58, 255};
    const Color declineFill = declineHovered ? ColorBrightness(UiSectionFillColor(), 0.06f) : UiSectionFillColor();
    const Color declineBorder = declineHovered ? ColorBrightness(borderColor, 0.10f) : borderColor;
    const Color panelColor = UiPanelColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    const int titleWidth = MeasureUiText(loc("Incoming Trade Offer"), 28);

    if (map == NULL || session == NULL ||
        !matchSessionHasPendingTradeOfferForLocalResponse(session) ||
        !matchSessionGetPendingTradeOffer(session, &offer))
    {
        return;
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.34f));
    DrawPanelFrame(panel, 0.08f, 8.0f, 10.0f, 0.14f, panelColor, borderColor);

    DrawUiText(loc("Incoming Trade Offer"), panel.x + panel.width * 0.5f - titleWidth * 0.5f, panel.y + 20.0f, 28, textColor);
    DrawUiText(TextFormat(loc("From %s"), PlayerName(map->currentPlayer)), panel.x + 24.0f, panel.y + 66.0f, 18, mutedText);

    DrawUiText(loc("You Receive"), panel.x + 24.0f, panel.y + 104.0f, 18, mutedText);
    DrawRectangleRounded((Rectangle){panel.x + 24.0f, panel.y + 130.0f, panel.width - 48.0f, 36.0f}, 0.16f, 6, UiSectionFillColor());
    DrawRectangleLinesEx((Rectangle){panel.x + 24.0f, panel.y + 130.0f, panel.width - 48.0f, 36.0f}, 1.5f, UiDisabledBorderColor());
    DrawLeftUiTextFitted(TextFormat("%d %s", offer.amountA, ResourceName(offer.resourceA)), (Rectangle){panel.x + 24.0f, panel.y + 130.0f, panel.width - 48.0f, 36.0f}, 12.0f, 20, 14, 0.0f, textColor);

    DrawUiText(loc("You Give"), panel.x + 24.0f, panel.y + 182.0f, 18, mutedText);
    DrawRectangleRounded((Rectangle){panel.x + 24.0f, panel.y + 208.0f, panel.width - 48.0f, 36.0f}, 0.16f, 6, UiSectionFillColor());
    DrawRectangleLinesEx((Rectangle){panel.x + 24.0f, panel.y + 208.0f, panel.width - 48.0f, 36.0f}, 1.5f, UiDisabledBorderColor());
    DrawLeftUiTextFitted(TextFormat("%d %s", offer.amountB, ResourceName(offer.resourceB)), (Rectangle){panel.x + 24.0f, panel.y + 208.0f, panel.width - 48.0f, 36.0f}, 12.0f, 20, 14, 0.0f, textColor);

    DrawUiText(loc("Accept or decline this offer."), panel.x + 24.0f, panel.y + 260.0f, 17, mutedText);

    DrawRectangleRounded((Rectangle){acceptBody.x + 4.0f, acceptBody.y + 6.0f, acceptBody.width, acceptBody.height}, 0.20f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(acceptBody, 0.20f, 8, acceptFill);
    DrawRectangleLinesEx(acceptBody, 2.0f, acceptBorder);
    DrawCenteredUiTextFitted(loc("Accept"), acceptBody, 20, 14, 0.0f, RAYWHITE);

    DrawRectangleRounded((Rectangle){declineBody.x + 4.0f, declineBody.y + 6.0f, declineBody.width, declineBody.height}, 0.20f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(declineBody, 0.20f, 8, declineFill);
    DrawRectangleLinesEx(declineBody, 2.0f, declineBorder);
    DrawCenteredUiTextFitted(loc("Decline"), declineBody, 20, 14, 0.0f, textColor);
}

void DrawDiscardModal(const struct Map *map)
{
    const Rectangle panel = GetDiscardModalBounds();
    const enum PlayerType discardPlayer = gameGetCurrentDiscardPlayer(map);
    const int discardRequired = gameGetDiscardAmountForPlayer(map, discardPlayer);
    const bool aiDiscard = discardPlayer >= PLAYER_RED &&
                           discardPlayer <= PLAYER_BLACK &&
                           map->players[discardPlayer].controlMode == PLAYER_CONTROL_AI;
    const bool revealDiscard = gDiscardRevealPlayer == discardPlayer;
    const Color panelColor = UiPanelColor();
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    int discardSelected = 0;

    if (aiDiscard)
    {
        return;
    }

    for (int resource = 0; resource < 5; resource++)
    {
        discardSelected += gDiscardSelection[resource];
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.34f));
    DrawPanelFrame(panel, 0.06f, 8.0f, 10.0f, 0.14f, panelColor, borderColor);

    if (!revealDiscard)
    {
        const char *title = loc("Discard Half");
        const char *lineA = TextFormat(loc("%s needs to discard %d cards."), PlayerName(discardPlayer), discardRequired);
        const char *lineB = loc("Pass the screen, then click anywhere here.");
        const char *lineC = loc("Resource counts stay hidden until they continue.");
        DrawUiText(title, panel.x + 22.0f, panel.y + 24.0f, 28, textColor);
        DrawUiText(lineA, panel.x + 22.0f, panel.y + 88.0f, 20, mutedText);
        DrawUiText(lineB, panel.x + 22.0f, panel.y + 132.0f, 20, textColor);
        DrawUiText(lineC, panel.x + 22.0f, panel.y + 166.0f, 18, mutedText);
        return;
    }

    DrawUiText(loc("Discard Half"), panel.x + 22.0f, panel.y + 16.0f, 28, textColor);
    DrawUiText(TextFormat(loc("%s must discard %d cards"), PlayerName(discardPlayer), discardRequired), panel.x + 22.0f, panel.y + 50.0f, 18, mutedText);

    for (int resource = 0; resource < 5; resource++)
    {
        const float rowY = panel.y + 92.0f + resource * 38.0f;
        const Rectangle minusButton = {panel.x + 182.0f, rowY, 26.0f, 26.0f};
        const Rectangle amountBox = {panel.x + 218.0f, rowY, 94.0f, 26.0f};
        const Rectangle plusButton = {panel.x + 322.0f, rowY, 26.0f, 26.0f};

        DrawUiText(ResourceName((enum ResourceType)resource), panel.x + 26.0f, rowY + 4.0f, 18, textColor);
        DrawRectangleRounded(minusButton, 0.22f, 6, UiDisabledFillColor());
        DrawRectangleLinesEx(minusButton, 1.5f, borderColor);
        DrawCenteredUiTextFitted("-", minusButton, 24, 20, -1.0f, textColor);
        DrawRectangleRounded(amountBox, 0.16f, 6, UiSectionFillColor());
        DrawRectangleLinesEx(amountBox, 1.5f, borderColor);
        {
            const char *amountLabel = TextFormat("%d / %d", gDiscardSelection[resource], map->players[discardPlayer].resources[resource]);
            DrawCenteredUiTextFitted(amountLabel, amountBox, 16, 12, 0.0f, textColor);
        }
        DrawRectangleRounded(plusButton, 0.22f, 6, UiDisabledFillColor());
        DrawRectangleLinesEx(plusButton, 1.5f, borderColor);
        DrawCenteredUiTextFitted("+", plusButton, 24, 20, -1.0f, textColor);
    }

    DrawUiText(TextFormat(loc("Selected %d / %d"), discardSelected, discardRequired), panel.x + 22.0f, panel.y + 280.0f, 18, discardSelected == discardRequired ? (Color){54, 130, 72, 255} : mutedText);
    {
        const Rectangle confirmButton = {panel.x + 38.0f, panel.y + panel.height - 48.0f, panel.width - 76.0f, 36.0f};
        const bool ready = discardSelected == discardRequired;
        DrawRectangleRounded(confirmButton, 0.16f, 8, ready ? (Color){171, 82, 54, 255} : UiDisabledFillColor());
        DrawRectangleLinesEx(confirmButton, 2.0f, ready ? borderColor : UiDisabledBorderColor());
        {
            const char *label = loc("Confirm Discard");
            DrawCenteredUiTextFitted(label, confirmButton, 18, 13, 0.0f, ready ? RAYWHITE : UiDisabledTextColor());
        }
    }
}

void DrawThiefVictimModal(const struct Map *map)
{
    const Rectangle panel = GetThiefVictimModalBounds();
    const bool aiChooser = map->currentPlayer >= PLAYER_RED &&
                           map->currentPlayer <= PLAYER_BLACK &&
                           map->players[map->currentPlayer].controlMode == PLAYER_CONTROL_AI;
    const bool revealVictims = gThiefVictimRevealPlayer == map->currentPlayer;
    const Color panelColor = UiPanelColor();
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    int victimIndex = 0;

    if (aiChooser)
    {
        return;
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.34f));
    DrawPanelFrame(panel, 0.06f, 8.0f, 10.0f, 0.14f, panelColor, borderColor);

    if (!revealVictims)
    {
        DrawUiText(loc("Steal Resource"), panel.x + 22.0f, panel.y + 20.0f, 28, textColor);
        DrawUiText(TextFormat(loc("%s is choosing an enemy."), PlayerName(map->currentPlayer)), panel.x + 22.0f, panel.y + 96.0f, 20, mutedText);
        return;
    }

    DrawUiText(loc("Steal Resource"), panel.x + 22.0f, panel.y + 16.0f, 28, textColor);
    DrawUiText(loc("Choose one adjacent player to steal from"), panel.x + 22.0f, panel.y + 52.0f, 18, mutedText);

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        const enum PlayerType victim = (enum PlayerType)player;
        if (!gameCanStealFromPlayer(map, victim))
        {
            continue;
        }

        const Rectangle victimButton = {panel.x + 26.0f + victimIndex * 118.0f, panel.y + 102.0f, 102.0f, 42.0f};
        DrawRectangleRounded(victimButton, 0.18f, 8, Fade(UiReadablePlayerColor(victim), 0.88f));
        DrawRectangleLinesEx(victimButton, 2.0f, borderColor);
        DrawCenteredUiTextFitted(PlayerName(victim), victimButton, 18, 13, 0.0f, RAYWHITE);
        victimIndex++;
    }
}

void DrawAwardCards(const struct Map *map)
{
    const enum PlayerType longestRoadOwner = gameGetLongestRoadOwner(map);
    const enum PlayerType largestArmyOwner = gameGetLargestArmyOwner(map);
    const float cardWidth = UiScaled(224.0f);
    const float cardHeight = UiScaled(176.0f);
    const float gap = UiScaled(14.0f);
    const float cardX = UiScaled(12.0f);
    const float firstCardY = (float)GetScreenHeight() - UiScaled(12.0f) - gap - cardHeight * 2.0f;
    const Rectangle longestRoadCard = {cardX, firstCardY, cardWidth, cardHeight};
    const Rectangle largestArmyCard = {cardX, firstCardY + cardHeight + gap, cardWidth, cardHeight};
    const char *longestRoadDetail = gameGetLongestRoadLength(map) >= 5
                                        ? TextFormat(loc("Road length: %d"), gameGetLongestRoadLength(map))
                                        : loc("Need 5-road chain");
    const char *largestArmyDetail = loc("Need 3 knights");

    if (largestArmyOwner >= PLAYER_RED && largestArmyOwner <= PLAYER_BLACK)
    {
        largestArmyDetail = TextFormat(loc("Played knights: %d"), map->players[largestArmyOwner].playedKnightCount);
    }

    DrawAwardCard(longestRoadCard,
                  loc("Longest Road"),
                  loc("2 Victory Points"),
                  longestRoadDetail,
                  longestRoadOwner,
                  (Color){159, 103, 58, 255});
    DrawAwardCard(largestArmyCard,
                  loc("Largest Army"),
                  loc("2 Victory Points"),
                  largestArmyDetail,
                  largestArmyOwner,
                  (Color){118, 116, 70, 255});
}

void DrawPlayerPanel(const struct Map *map)
{
    const struct PlayerState *player = CurrentPlayerState(map);
    const bool showingPinnedLocalInfo = IsPrivateInfoPinnedToLocalHuman(map);
    if (player == NULL)
    {
        return;
    }

    const Rectangle panel = GetPlayerPanelBounds();
    const float panelX = panel.x;
    const float panelY = panel.y;
    const Color panelColor = UiPanelColor();
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();

    DrawPanelFrame(panel, 0.08f, UiScaled(6.0f), UiScaled(8.0f), 0.10f, panelColor, borderColor);

    DrawUiText(PlayerName(player->type), panelX + UiScaled(16.0f), panelY + UiScaled(14.0f), UiScaledFont(26), UiReadablePlayerColor(player->type));
    DrawUiText(showingPinnedLocalInfo ? loc("Your Hand") : loc("Current Player"), panelX + UiScaled(16.0f), panelY + UiScaled(44.0f), UiScaledFont(16), mutedText);

    const int totalVp = gameComputeVictoryPoints(map, player->type);
    const int visibleVp = gameComputeVisibleVictoryPoints(map, player->type);
    DrawUiText(loc("Victory Points"), panelX + UiScaled(16.0f), panelY + UiScaled(78.0f), UiScaledFont(18), textColor);
    {
        const char *vpLabel = TextFormat("%d (%d)", totalVp, visibleVp);
        const int vpFont = UiScaledFont(24);
        const int vpWidth = MeasureUiText(vpLabel, vpFont);
        DrawUiText(vpLabel, panelX + panel.width - UiScaled(18.0f) - vpWidth, panelY + UiScaled(76.0f), vpFont, textColor);
    }

    DrawUiText(loc("Development Cards"), panelX + UiScaled(16.0f), panelY + UiScaled(108.0f), UiScaledFont(14), mutedText);
    {
        const char *deckCountLabel = TextFormat(loc("%d left"), gameGetDevelopmentDeckCount(map));
        const int deckFont = UiScaledFont(14);
        const int deckCountWidth = MeasureUiText(deckCountLabel, deckFont);
        const float deckCountX = panelX + panel.width - UiScaled(32.0f) - deckCountWidth;
        const Color deckCountColor = textColor;
        DrawUiText(deckCountLabel, deckCountX, panelY + UiScaled(108.0f), deckFont, deckCountColor);
        DrawUiText(deckCountLabel, deckCountX + UiScaled(0.8f), panelY + UiScaled(108.0f), deckFont, deckCountColor);
    }

    {
        int totalResources = 0;
        const char *resourcesLabel = NULL;
        for (int resource = 0; resource < 5; resource++)
        {
            totalResources += player->resources[resource];
        }
        resourcesLabel = TextFormat("%s (%d)", loc("Resources"), totalResources);
        DrawUiText(resourcesLabel, panelX + UiScaled(16.0f), panelY + UiScaled(126.0f), UiScaledFont(18), textColor);
    }
    for (int resource = 0; resource < 5; resource++)
    {
        const float rowY = panelY + UiScaled(148.0f) + resource * UiScaled(14.0f);
        const char *resourceCountLabel = TextFormat("%d", player->resources[resource]);
        const int resourceFont = UiScaledFont(16);
        const int resourceCountWidth = MeasureUiText(resourceCountLabel, resourceFont);
        const int turnDelta = player->type == map->currentPlayer ? uiGetCurrentTurnResourceGain((enum ResourceType)resource) : 0;
        DrawUiText(ResourceName((enum ResourceType)resource), panelX + UiScaled(18.0f), rowY, resourceFont, mutedText);
        DrawUiText(resourceCountLabel, panelX + UiScaled(202.0f) - resourceCountWidth, rowY, resourceFont, textColor);
        if (turnDelta != 0)
        {
            const char *deltaLabel = TextFormat("%+d", turnDelta);
            const Color deltaColor = turnDelta > 0 ? (Color){70, 136, 78, 255} : (Color){176, 63, 52, 255};
            DrawUiText(deltaLabel, panelX + UiScaled(208.0f), rowY, UiScaledFont(15), deltaColor);
        }
    }
}

void DrawOpponentVictoryBar(const struct Map *map)
{
    enum PlayerType opponents[MAX_PLAYERS - 1];
    int opponentCount = 0;
    const struct PlayerState *player = CurrentPlayerState(map);
    Rectangle bar;
    const Color panelColor = UiPanelColor();
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    const float slotWidth = UiScaled(132.0f);
    const float slotHeight = UiScaled(34.0f);
    const float slotGap = UiScaled(10.0f);
    const float labelWidth = UiScaled(132.0f);
    const float padding = UiScaled(18.0f);
    float slotsWidth = 0.0f;
    float startX = 0.0f;

    if (player == NULL)
    {
        return;
    }

    for (int candidate = PLAYER_RED; candidate <= PLAYER_BLACK; candidate++)
    {
        if (candidate != player->type &&
            gameIsPlayerActive(map, (enum PlayerType)candidate))
        {
            opponents[opponentCount++] = (enum PlayerType)candidate;
        }
    }

    if (opponentCount <= 0)
    {
        return;
    }

    slotsWidth = opponentCount * slotWidth + (opponentCount - 1) * slotGap;
    bar = (Rectangle){
        (float)GetScreenWidth() * 0.5f - (labelWidth + slotsWidth + padding * 3.0f) * 0.5f,
        UiScaled(24.0f),
        labelWidth + slotsWidth + padding * 3.0f,
        UiScaled(56.0f)};
    startX = bar.x + bar.width - padding - slotsWidth;

    DrawPanelFrame(bar, 0.14f, UiScaled(6.0f), UiScaled(8.0f), 0.10f, panelColor, borderColor);
    DrawUiText(loc("Opponents"), bar.x + padding, bar.y + UiScaled(8.0f), UiScaledFont(18), textColor);
    DrawUiText(loc("Visible VP"), bar.x + padding, bar.y + UiScaled(30.0f), UiScaledFont(13), mutedText);

    for (int i = 0; i < opponentCount; i++)
    {
        const int notificationCount = uiGetPlayerNotificationCount(opponents[i]);
        const Rectangle slot = {
            startX + i * (slotWidth + slotGap),
            bar.y + UiScaled(10.0f),
            slotWidth,
            slotHeight};
        const char *name = PlayerName(opponents[i]);
        const char *score = TextFormat("%d", gameComputeVisibleVictoryPoints(map, opponents[i]));
        const int nameFont = UiScaledFont(16);
        const int scoreFont = UiScaledFont(22);
        const int scoreWidth = MeasureUiText(score, scoreFont);

        DrawRectangleRounded(slot, 0.24f, 8, Fade(UiReadablePlayerColor(opponents[i]), IsDarkUiTheme() ? 0.26f : 0.18f));
        DrawRectangleLinesEx(slot, 1.5f, Fade(UiReadablePlayerColor(opponents[i]), 0.85f));
        DrawUiText(name, slot.x + UiScaled(12.0f), slot.y + UiScaled(8.0f), nameFont, textColor);
        DrawUiText(score, slot.x + slot.width - UiScaled(14.0f) - scoreWidth, slot.y + UiScaled(5.0f), scoreFont, UiReadablePlayerColor(opponents[i]));

        for (int notificationIndex = 0; notificationIndex < notificationCount; notificationIndex++)
        {
            const char *notificationText = uiGetPlayerNotificationText(opponents[i], notificationIndex);
            const enum UiNotificationTone tone = uiGetPlayerNotificationTone(opponents[i], notificationIndex);
            const float dismissProgress = uiGetPlayerNotificationDismissProgress(opponents[i], notificationIndex);
            const int notificationFont = UiScaledFont(16);
            const int notificationWidth = MeasureUiText(notificationText, notificationFont);
            const float chipWidth = (float)(notificationWidth + UiScaled(20.0f));
            const float chipHeight = UiScaled(22.0f);
            const float chipX = slot.x + slot.width * 0.5f - chipWidth * 0.5f;
            const float textX = slot.x + slot.width * 0.5f - notificationWidth * 0.5f;
            Color notificationColor = mutedText;
            Color chipFill = UiPanelHighlightColor();
            Color chipBorder = UiDisabledBorderColor();
            float alpha = 1.0f;
            float y = bar.y + bar.height + UiScaled(10.0f) + notificationIndex * UiScaled(28.0f);

            if (tone == UI_NOTIFICATION_POSITIVE)
            {
                notificationColor = (Color){70, 136, 78, 255};
                chipFill = (Color){230, 242, 228, 248};
                chipBorder = (Color){96, 150, 92, 255};
            }
            else if (tone == UI_NOTIFICATION_NEGATIVE)
            {
                notificationColor = (Color){176, 63, 52, 255};
                chipFill = (Color){247, 229, 225, 248};
                chipBorder = (Color){186, 88, 74, 255};
            }
            else if (tone == UI_NOTIFICATION_VICTORY)
            {
                notificationColor = (Color){184, 140, 43, 255};
                chipFill = (Color){247, 239, 213, 248};
                chipBorder = (Color){190, 150, 58, 255};
            }

            if (uiIsPlayerNotificationDismissing(opponents[i], notificationIndex))
            {
                alpha = 1.0f - dismissProgress;
                y -= dismissProgress * UiScaled(22.0f);
            }

            DrawRectangleRounded(
                (Rectangle){chipX, y, chipWidth, chipHeight},
                0.45f,
                8,
                Fade(chipFill, alpha));
            DrawRectangleLinesEx(
                (Rectangle){chipX, y, chipWidth, chipHeight},
                1.5f,
                Fade(chipBorder, alpha));
            DrawUiText(
                notificationText,
                textX,
                y + UiScaled(3.0f),
                notificationFont,
                Fade(notificationColor, alpha));
        }
    }
}

void DrawTurnBanner(const struct Map *map)
{
    const struct MatchSession *session = matchSessionGetActive();
    const enum PlayerType currentPlayer = map != NULL ? map->currentPlayer : PLAYER_NONE;
    enum PlayerType bannerPlayer = currentPlayer;
    const enum PlayerType localHuman = LocalHumanPlayer(map);
    const float emphasis = uiGetTurnAnnouncementEmphasis();
    const float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 3.8f);
    bool currentPlayerValid = currentPlayer >= PLAYER_RED && currentPlayer <= PLAYER_BLACK;
    bool currentHuman = false;
    bool localTurn = false;
    bool showHandoffHint = false;
    int humanCount = 0;
    const char *eyebrow = NULL;
    const char *title = NULL;
    const char *subtitle = "";
    char subtitleLines[2][96] = {{0}};
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    const float maxPanelWidth = (float)GetScreenWidth() - 20.0f;
    float width = 336.0f + emphasis * 20.0f;
    float height = 74.0f + emphasis * 6.0f;
    float subtitleLineHeight = 0.0f;
    float titleY = 0.0f;
    float subtitleY = 0.0f;
    Rectangle panel = {0};
    Rectangle accentStrip = {0};
    Color accent = {171, 82, 54, 255};
    Color border = {118, 88, 56, 255};
    Color panelColor = {247, 240, 226, 246};
    int contentWidth = 0;
    int eyebrowFont = 14;
    int titleFont = 26 + (int)(emphasis * 2.0f);
    int subtitleFont = 14;
    int subtitleLineCount = 0;

    if (map == NULL || gameHasWinner(map) || !currentPlayerValid)
    {
        return;
    }

    if (gameHasPendingDiscards(map))
    {
        bannerPlayer = gameGetCurrentDiscardPlayer(map);
        currentPlayerValid = bannerPlayer >= PLAYER_RED && bannerPlayer <= PLAYER_BLACK;
        if (!currentPlayerValid)
        {
            return;
        }
    }

    currentHuman = playerControlModeIsHuman(map->players[bannerPlayer].controlMode);
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (playerControlModeIsHuman(map->players[player].controlMode))
        {
            humanCount++;
        }
    }

    localTurn = localHuman != PLAYER_NONE && matchSessionLocalControlsPlayer(session, bannerPlayer);
    if (!currentHuman && !localTurn)
    {
        return;
    }

    if (emphasis <= 0.02f)
    {
        return;
    }

    accent = UiReadablePlayerColor(bannerPlayer);
    border = ColorBrightness(accent, -0.36f);
    panelColor = localTurn ? UiPanelHighlightColor() : UiPanelColor();

    eyebrow = localTurn ? loc("Your turn.") : loc("Current Player");
    title = PlayerName(bannerPlayer);

    if (humanCount > 1 && localHuman == PLAYER_NONE && emphasis > 0.08f)
    {
        subtitle = loc("Pass the screen to continue");
        showHandoffHint = true;
    }
    else if (map->phase == GAME_PHASE_SETUP)
    {
        subtitle = map->setupNeedsRoad ? loc("Place 1 road") : loc("Place 1 settlement");
    }
    else if (gameHasPendingDiscards(map))
    {
        subtitle = loc("Discard Cards");
    }
    else if (gameNeedsThiefPlacement(map))
    {
        subtitle = loc("Move Thief");
    }
    else if (gameNeedsThiefVictimSelection(map))
    {
        subtitle = loc("Steal Resource");
    }
    else if (localTurn && !map->rolledThisTurn)
    {
        subtitle = loc("Roll Dice (Enter)");
    }
    else if (localTurn && gameCanEndTurn(map))
    {
        subtitle = loc("End Turn");
    }

    width = 320.0f + emphasis * 18.0f;
    height = 74.0f + emphasis * 4.0f;
    eyebrowFont = 14;
    titleFont = 24 + (int)(emphasis * 2.0f);

    if (width > maxPanelWidth)
    {
        width = maxPanelWidth;
    }

    contentWidth = (int)(width - 50.0f);
    if (contentWidth < 210)
    {
        contentWidth = (int)(width - 38.0f);
    }

    eyebrowFont = FitUiTextFontSize(eyebrow, eyebrowFont, 13, contentWidth);
    titleFont = FitUiTextFontSize(title, titleFont, 20, contentWidth);

    if (subtitle[0] != '\0')
    {
        subtitleFont = showHandoffHint ? 16 : 15;
        subtitleLineCount = BuildWrappedUiLinesFitted(subtitle, subtitleFont, 12, contentWidth, subtitleLines, 2, &subtitleFont);
    }

    subtitleLineHeight = (float)subtitleFont + 3.0f;
    if (subtitleLineCount > 0)
    {
        height = 28.0f + (float)eyebrowFont + (float)titleFont + 6.0f + subtitleLineHeight * (float)subtitleLineCount + 12.0f;
    }
    else
    {
        height = 28.0f + (float)eyebrowFont + (float)titleFont + 12.0f;
    }

    if (height < 64.0f + emphasis * 4.0f)
    {
        height = 64.0f + emphasis * 4.0f;
    }

    panel = (Rectangle){
        (float)GetScreenWidth() * 0.5f - width * 0.5f,
        10.0f - emphasis * 2.0f,
        width,
        height};
    accentStrip = (Rectangle){panel.x + 10.0f, panel.y + 8.0f, panel.width - 20.0f, 5.0f};
    titleY = panel.y + 14.0f + (float)eyebrowFont + 2.0f;
    subtitleY = titleY + (float)titleFont + 6.0f;

    DrawRectangleRounded(
        (Rectangle){panel.x + 6.0f, panel.y + 8.0f, panel.width, panel.height},
        0.22f,
        8,
        Fade(BLACK, 0.10f + emphasis * 0.05f));
    DrawRectangleRounded(
        (Rectangle){panel.x - 4.0f - emphasis * 6.0f, panel.y - 2.0f - emphasis * 3.0f, panel.width + 8.0f + emphasis * 12.0f, panel.height + 4.0f + emphasis * 6.0f},
        0.24f,
        8,
        Fade(accent, 0.06f + 0.06f * pulse + emphasis * 0.10f));
    DrawRectangleRounded(panel, 0.22f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.2f, Fade(border, 0.95f));
    DrawRectangleRounded(accentStrip, 0.45f, 8, Fade(accent, 0.86f));
    DrawCircleGradient((int)(panel.x + panel.width - 28.0f), (int)(panel.y + 16.0f), 14.0f + emphasis * 4.0f, Fade(accent, 0.18f + emphasis * 0.08f), BLANK);

    DrawUiText(eyebrow, panel.x + 18.0f, panel.y + 16.0f, eyebrowFont, mutedText);
    DrawUiText(title, panel.x + 18.0f, titleY, titleFont, accent);
    for (int i = 0; i < subtitleLineCount; i++)
    {
        DrawUiText(
            subtitleLines[i],
            panel.x + 18.0f,
            subtitleY + (float)i * subtitleLineHeight,
            subtitleFont,
            showHandoffHint ? textColor : mutedText);
    }
}

bool GetHoveredDevelopmentHandCard(const struct Map *map, enum DevelopmentCardType *type)
{
    struct DevelopmentHandCard cards[DEVELOPMENT_CARD_COUNT];
    const Vector2 mouse = GetMousePosition();
    const int cardCount = BuildDevelopmentHandLayout(map, cards);

    for (int i = cardCount - 1; i >= 0; i--)
    {
        if (PointInRotatedRectangle(mouse, DevelopmentHandHitBounds(cards[i].bounds), cards[i].rotation))
        {
            if (type != NULL)
            {
                *type = cards[i].type;
            }
            return true;
        }
    }

    return false;
}

void DrawDevelopmentHand(const struct Map *map)
{
    struct DevelopmentHandCard cards[DEVELOPMENT_CARD_COUNT];
    const int cardCount = BuildDevelopmentHandLayout(map, cards);
    const Font font = RendererGetUiFont();
    const float centerX = (float)GetScreenWidth() * 0.5f;
    const Vector2 mouse = GetMousePosition();
    int hoveredIndex = -1;
    float totalWidth = 0.0f;

    if (cardCount <= 0)
    {
        return;
    }

    totalWidth = cards[cardCount - 1].bounds.x + cards[cardCount - 1].bounds.width - cards[0].bounds.x;
    DrawEllipse((int)centerX, GetScreenHeight() + 52, (totalWidth + 84.0f) * 0.5f, 30.0f, Fade(BLACK, 0.10f));

    for (int i = cardCount - 1; i >= 0; i--)
    {
        if (PointInRotatedRectangle(mouse, DevelopmentHandHitBounds(cards[i].bounds), cards[i].rotation))
        {
            hoveredIndex = i;
            break;
        }
    }

    for (int pass = 0; pass < 2; pass++)
    {
        for (int i = 0; i < cardCount; i++)
        {
            const bool hovered = i == hoveredIndex;
            const float alpha = cards[i].playable ? 1.0f : 0.82f;
            Rectangle card = cards[i].bounds;
            if ((pass == 0 && hovered) || (pass == 1 && !hovered))
            {
                continue;
            }

            if (hovered)
            {
                card.y -= 34.0f;
            }
            DrawDevelopmentCardVisual(font, card, cards[i].rotation, cards[i].type, cards[i].count, alpha, hovered);
        }
    }
}

void DrawDevelopmentCardDrawAnimation(const struct Map *map)
{
    const Font font = RendererGetUiFont();
    const float progress = uiGetDevelopmentCardDrawAnimationProgress();
    struct DevelopmentHandCard cards[DEVELOPMENT_CARD_COUNT];
    const int cardCount = BuildDevelopmentHandLayout(map, cards);
    Rectangle startCard = GetBuildPanelDevelopmentCardBounds();
    Rectangle focusCard;
    Rectangle settleCard;
    Rectangle currentCard;
    float phase = 0.0f;
    float rotation = 0.0f;
    float alpha = 1.0f;

    if (!uiIsDevelopmentCardDrawAnimating() || IsPrivateInfoPinnedToLocalHuman(map))
    {
        return;
    }

    startCard = (Rectangle){
        startCard.x + startCard.width * 0.5f - 38.0f,
        startCard.y + startCard.height * 0.5f - 52.0f,
        76.0f,
        104.0f};
    focusCard = (Rectangle){
        (float)GetScreenWidth() * 0.5f - 102.0f,
        (float)GetScreenHeight() * 0.5f - 188.0f,
        204.0f,
        266.0f};
    settleCard = (Rectangle){
        (float)GetScreenWidth() * 0.5f - 78.0f,
        (float)GetScreenHeight() - 170.0f,
        156.0f,
        202.0f};
    for (int i = 0; i < cardCount; i++)
    {
        if (cards[i].type == uiGetAnimatedDevelopmentCardType())
        {
            settleCard = cards[i].bounds;
            break;
        }
    }

    if (progress < 0.42f)
    {
        phase = EaseOutCubic(progress / 0.42f);
        currentCard = LerpRectangle(startCard, focusCard, phase);
        rotation = LerpFloat(-12.0f, -2.0f, phase);
    }
    else
    {
        phase = EaseInOutCubic((progress - 0.42f) / 0.58f);
        currentCard = LerpRectangle(focusCard, settleCard, phase);
        rotation = LerpFloat(-2.0f, 0.0f, phase);
    }

    alpha = progress < 0.08f ? progress / 0.08f : 1.0f;
    if (progress > 0.88f)
    {
        alpha = 1.0f - (progress - 0.88f) / 0.12f;
    }
    alpha = Clamp01(alpha);

    DrawEllipse(
        (int)(currentCard.x + currentCard.width * 0.5f),
        (int)(currentCard.y + currentCard.height * 0.88f),
        currentCard.width * 0.52f,
        currentCard.height * 0.12f,
        Fade((Color){196, 167, 99, 255}, 0.16f * alpha));
    DrawCircleGradient(
        (int)(currentCard.x + currentCard.width * 0.5f),
        (int)(currentCard.y + currentCard.height * 0.40f),
        currentCard.width * 0.48f,
        Fade((Color){255, 244, 206, 255}, 0.18f * alpha),
        BLANK);
    DrawDevelopmentCardVisual(font, currentCard, rotation, uiGetAnimatedDevelopmentCardType(), 0, alpha, true);
}

void DrawDevelopmentPurchaseOverlay(const struct Map *map)
{
    const Rectangle panel = GetDevelopmentPurchaseOverlayBounds();
    const Rectangle confirmButton = GetDevelopmentPurchaseConfirmButtonBounds();
    const Rectangle cancelButton = GetDevelopmentPurchaseCancelButtonBounds();
    const float alpha = uiGetDevelopmentPurchaseConfirmOpenAmount();
    const bool canBuyDevelopment = gameCanBuyDevelopment(map);
    const Color borderColor = UiPanelBorderColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();

    if (alpha <= 0.01f)
    {
        return;
    }

    DrawPanelFrame(panel, 0.10f, 6.0f, 8.0f, 0.12f * alpha, Fade(UiPanelHighlightColor(), alpha), Fade(borderColor, alpha));
    DrawUiText(loc("Buy Development Card?"), panel.x + 18.0f, panel.y + 16.0f, 22, Fade(textColor, alpha));
    DrawUiText(loc("Cost: Wheat + Sheep + Stone"), panel.x + 18.0f, panel.y + 50.0f, 16, Fade(mutedText, alpha));
    DrawUiText(TextFormat(loc("%d cards left in deck"), gameGetDevelopmentDeckCount(map)), panel.x + 18.0f, panel.y + 72.0f, 14, Fade(mutedText, alpha));

    DrawRectangleRounded(confirmButton, 0.18f, 8, Fade(canBuyDevelopment ? (Color){171, 82, 54, 255} : UiDisabledFillColor(), alpha));
    DrawRectangleLinesEx(confirmButton, 2.0f, Fade(canBuyDevelopment ? borderColor : UiDisabledBorderColor(), alpha));
    {
        const char *confirmLabel = loc("Confirm Buy");
        DrawCenteredUiTextFitted(confirmLabel, confirmButton, 18, 13, 0.0f, Fade(canBuyDevelopment ? RAYWHITE : UiDisabledTextColor(), alpha));
    }

    DrawRectangleRounded(cancelButton, 0.18f, 8, Fade(UiDisabledFillColor(), alpha));
    DrawRectangleLinesEx(cancelButton, 2.0f, Fade(UiDisabledBorderColor(), alpha));
    {
        const char *cancelLabel = loc("Cancel");
        DrawCenteredUiTextFitted(cancelLabel, cancelButton, 18, 13, 0.0f, Fade(mutedText, alpha));
    }
}

void DrawDevelopmentPlayOverlay(const struct Map *map)
{
    const Rectangle panel = GetDevelopmentPlayOverlayBounds();
    const Rectangle confirmButton = GetDevelopmentPlayConfirmButtonBounds();
    const Rectangle cancelButton = GetDevelopmentPlayCancelButtonBounds();
    const enum DevelopmentCardType type = uiGetDevelopmentPlayCardType();
    const float alpha = uiGetDevelopmentPlayConfirmOpenAmount();
    const bool canPlay = gameCanPlayDevelopmentCard(map, type);
    const float buttonWidth = 76.0f;
    const float buttonHeight = 36.0f;
    const float buttonGap = 8.0f;
    const float rowWidth = buttonWidth * 5.0f + buttonGap * 4.0f;
    const float rowX = panel.x + panel.width * 0.5f - rowWidth * 0.5f;
    const int detailMaxWidth = (int)(panel.width - 44.0f);
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();

    if (alpha <= 0.01f || type < DEVELOPMENT_CARD_KNIGHT || type >= DEVELOPMENT_CARD_COUNT)
    {
        return;
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.18f * alpha));
    DrawPanelFrame(panel, 0.10f, 8.0f, 10.0f, 0.14f * alpha, Fade(UiPanelHighlightColor(), alpha), Fade(UiPanelBorderColor(), alpha));
    DrawUiText(TextFormat(loc("Use %s?"), DevelopmentCardTitle(type)), panel.x + 22.0f, panel.y + 18.0f, 26, Fade(textColor, alpha));

    switch (type)
    {
    case DEVELOPMENT_CARD_KNIGHT:
        DrawWrappedUiTextBlock(loc("Move the thief and steal 1 random card."), panel.x + 22.0f, panel.y + 60.0f, 18, detailMaxWidth, 22.0f, Fade(mutedText, alpha));
        DrawUiText(loc("You can still roll this turn afterward."), panel.x + 22.0f, panel.y + 88.0f, 16, Fade((Color){122, 100, 78, 255}, alpha));
        break;
    case DEVELOPMENT_CARD_ROAD_BUILDING:
        DrawWrappedUiTextBlock(loc("Place up to 2 roads for free."), panel.x + 22.0f, panel.y + 60.0f, 18, detailMaxWidth, 22.0f, Fade(mutedText, alpha));
        DrawUiText(loc("The board will switch into road placement mode."), panel.x + 22.0f, panel.y + 88.0f, 16, Fade((Color){122, 100, 78, 255}, alpha));
        break;
    case DEVELOPMENT_CARD_YEAR_OF_PLENTY:
        DrawWrappedUiTextBlock(loc("Choose 2 resources to take from the bank."), panel.x + 22.0f, panel.y + 60.0f, 18, detailMaxWidth, 22.0f, Fade(mutedText, alpha));
        DrawUiText(loc("First Resource"), panel.x + 24.0f, panel.y + 108.0f, 15, Fade(textColor, alpha));
        DrawUiText(loc("Second Resource"), panel.x + 24.0f, panel.y + 176.0f, 15, Fade(textColor, alpha));
        for (int resource = 0; resource < 5; resource++)
        {
            const Rectangle firstButton = {rowX + resource * (buttonWidth + buttonGap), panel.y + 132.0f, buttonWidth, buttonHeight};
            const Rectangle secondButton = {rowX + resource * (buttonWidth + buttonGap), panel.y + 200.0f, buttonWidth, buttonHeight};
            const bool firstSelected = gDevelopmentPlayPrimaryResource == (enum ResourceType)resource;
            const bool secondSelected = gDevelopmentPlaySecondaryResource == (enum ResourceType)resource;
            const Color fill = UiSectionFillColor();
            const Color selectedFill = (Color){182, 141, 97, 255};
            const Color border = UiDisabledBorderColor();
            const Color selectedText = RAYWHITE;
            const char *label = ResourceName((enum ResourceType)resource);

            DrawRectangleRounded(firstButton, 0.18f, 8, Fade(firstSelected ? selectedFill : fill, alpha));
            DrawRectangleLinesEx(firstButton, 1.6f, Fade(border, alpha));
            DrawCenteredUiTextFitted(label, firstButton, 15, 11, 0.0f, Fade(firstSelected ? selectedText : textColor, alpha));

            DrawRectangleRounded(secondButton, 0.18f, 8, Fade(secondSelected ? selectedFill : fill, alpha));
            DrawRectangleLinesEx(secondButton, 1.6f, Fade(border, alpha));
            DrawCenteredUiTextFitted(label, secondButton, 15, 11, 0.0f, Fade(secondSelected ? selectedText : textColor, alpha));
        }
        break;
    case DEVELOPMENT_CARD_MONOPOLY:
        DrawWrappedUiTextBlock(loc("Choose 1 resource. Every opponent gives you that type."), panel.x + 22.0f, panel.y + 60.0f, 18, detailMaxWidth, 22.0f, Fade(mutedText, alpha));
        DrawUiText(loc("Target Resource"), panel.x + 24.0f, panel.y + 116.0f, 15, Fade(textColor, alpha));
        for (int resource = 0; resource < 5; resource++)
        {
            const Rectangle button = {rowX + resource * (buttonWidth + buttonGap), panel.y + 144.0f, buttonWidth, buttonHeight};
            const bool selected = gDevelopmentPlayPrimaryResource == (enum ResourceType)resource;
            const char *label = ResourceName((enum ResourceType)resource);

            DrawRectangleRounded(button, 0.18f, 8, Fade(selected ? (Color){182, 141, 97, 255} : UiSectionFillColor(), alpha));
            DrawRectangleLinesEx(button, 1.6f, Fade(UiDisabledBorderColor(), alpha));
            DrawCenteredUiTextFitted(label, button, 15, 11, 0.0f, Fade(selected ? RAYWHITE : textColor, alpha));
        }
        break;
    case DEVELOPMENT_CARD_VICTORY_POINT:
    default:
        DrawWrappedUiTextBlock(loc("Victory point cards are counted automatically."), panel.x + 22.0f, panel.y + 60.0f, 18, detailMaxWidth, 22.0f, Fade(mutedText, alpha));
        break;
    }

    DrawRectangleRounded(confirmButton, 0.18f, 8, Fade(canPlay ? (Color){171, 82, 54, 255} : UiDisabledFillColor(), alpha));
    DrawRectangleLinesEx(confirmButton, 2.0f, Fade(canPlay ? UiPanelBorderColor() : UiDisabledBorderColor(), alpha));
    {
        const char *confirmLabel = loc("Confirm Use");
        if (type == DEVELOPMENT_CARD_YEAR_OF_PLENTY)
        {
            confirmLabel = loc("Gain Resources");
        }
        else if (type == DEVELOPMENT_CARD_MONOPOLY)
        {
            confirmLabel = loc("Claim Resource");
        }
        else if (type == DEVELOPMENT_CARD_ROAD_BUILDING)
        {
            confirmLabel = loc("Place Roads");
        }
        DrawCenteredUiTextFitted(confirmLabel, confirmButton, 18, 13, 0.0f, Fade(canPlay ? RAYWHITE : UiDisabledTextColor(), alpha));
    }

    DrawRectangleRounded(cancelButton, 0.18f, 8, Fade(UiDisabledFillColor(), alpha));
    DrawRectangleLinesEx(cancelButton, 2.0f, Fade(UiDisabledBorderColor(), alpha));
    {
        const char *cancelLabel = loc("Cancel");
        DrawCenteredUiTextFitted(cancelLabel, cancelButton, 18, 13, 0.0f, Fade(mutedText, alpha));
    }
}

void DrawCenteredWarning(void)
{
    char textBuffer[192];
    char *lines[3] = {0};
    int lineCount = 0;
    const char *message = uiGetCenteredWarningText();
    const float alpha = uiGetCenteredWarningAlpha();
    const float yOffset = uiGetCenteredWarningVerticalOffset();
    const float panelWidth = 500.0f;
    const float panelHeight = 82.0f;
    const Rectangle panel = {
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - 162.0f + yOffset,
        panelWidth,
        panelHeight};
    const Color panelColor = IsDarkUiTheme() ? (Color){74, 45, 48, 248} : (Color){249, 233, 230, 248};
    const Color borderColor = IsDarkUiTheme() ? (Color){214, 105, 96, 255} : (Color){176, 63, 52, 255};
    const Color textColor = IsDarkUiTheme() ? (Color){247, 204, 198, 255} : (Color){176, 41, 33, 255};

    if (message == NULL || message[0] == '\0' || alpha <= 0.01f)
    {
        return;
    }

    strncpy(textBuffer, message, sizeof(textBuffer) - 1);
    textBuffer[sizeof(textBuffer) - 1] = '\0';

    {
        char *line = textBuffer;
        while (line != NULL && lineCount < 3)
        {
            char *next = strchr(line, '\n');
            if (next != NULL)
            {
                *next = '\0';
                next++;
            }
            lines[lineCount++] = line;
            line = next;
        }
    }

    DrawPanelFrame(panel, 0.18f, 7.0f, 9.0f, 0.11f * alpha, Fade(panelColor, alpha), Fade(borderColor, alpha));

    for (int i = 0; i < lineCount; i++)
    {
        const int fontSize = 17;
        const int lineWidth = MeasureUiText(lines[i], fontSize);
        const float lineX = panel.x + panel.width * 0.5f - lineWidth * 0.5f;
        const float lineY = panel.y + 18.0f + i * 24.0f;
        DrawUiText(lines[i], lineX + 1.0f, lineY + 1.0f, fontSize, Fade((Color){92, 21, 17, 255}, alpha * 0.26f));
        DrawUiText(lines[i], lineX, lineY, fontSize, Fade(textColor, alpha));
    }
}

void DrawCenteredStatus(void)
{
    char lines[4][96] = {{0}};
    int lineCount = 0;
    const char *message = uiGetCenteredStatusText();
    const enum UiNotificationTone tone = uiGetCenteredStatusTone();
    const enum PlayerType accentPlayer = uiGetCenteredStatusPlayer();
    const float alpha = uiGetCenteredStatusAlpha();
    const float yOffset = uiGetCenteredStatusVerticalOffset();
    const float emphasis = uiGetCenteredStatusEmphasis();
    const int fontSize = 18;
    const float lineStep = 22.0f;
    float widestLine = 0.0f;
    float panelWidth = 320.0f;
    float panelHeight = 72.0f;
    Rectangle panel = {0};
    Rectangle scaledPanel = {0};
    Color panelColor = UiPanelHighlightColor();
    Color borderColor = UiPanelBorderColor();
    Color textColor = UiTextColor();
    Color glowColor = (Color){196, 158, 112, 255};
    Color accentColor = (Color){196, 158, 112, 255};

    if (message == NULL || message[0] == '\0' || alpha <= 0.01f)
    {
        return;
    }

    if (tone == UI_NOTIFICATION_POSITIVE)
    {
        panelColor = (Color){230, 242, 228, 248};
        borderColor = (Color){78, 136, 76, 255};
        textColor = (Color){47, 92, 48, 255};
        glowColor = (Color){102, 178, 94, 255};
    }
    else if (tone == UI_NOTIFICATION_NEGATIVE)
    {
        panelColor = (Color){247, 229, 225, 248};
        borderColor = (Color){176, 63, 52, 255};
        textColor = (Color){140, 43, 34, 255};
        glowColor = (Color){208, 92, 82, 255};
    }
    else if (tone == UI_NOTIFICATION_VICTORY)
    {
        panelColor = (Color){247, 239, 213, 248};
        borderColor = (Color){184, 140, 43, 255};
        textColor = (Color){120, 84, 19, 255};
        glowColor = (Color){212, 175, 72, 255};
    }

    if (accentPlayer >= PLAYER_RED && accentPlayer <= PLAYER_BLACK)
    {
        accentColor = UiReadablePlayerColor(accentPlayer);
        borderColor = ColorBrightness(accentColor, -0.28f);
        textColor = accentColor;
        glowColor = accentColor;
    }

    lineCount = BuildWrappedUiLines(message, fontSize, 338, lines, 4);
    if (lineCount <= 0)
    {
        return;
    }

    for (int i = 0; i < lineCount; i++)
    {
        const int lineFontSize = (lineCount == 1 ? 20 : fontSize) + (int)roundf(2.0f * emphasis);
        const float lineWidth = (float)MeasureUiText(lines[i], lineFontSize);
        if (lineWidth > widestLine)
        {
            widestLine = lineWidth;
        }
    }

    panelWidth = widestLine + 50.0f + emphasis * 16.0f;
    if (panelWidth < 290.0f)
    {
        panelWidth = 290.0f;
    }
    if (panelWidth > 430.0f)
    {
        panelWidth = 430.0f;
    }

    panelHeight = 30.0f + (float)lineCount * lineStep;
    panel = (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - 122.0f + yOffset,
        panelWidth,
        panelHeight};
    scaledPanel = ScaleRectangleFromCenter(panel, 1.0f + emphasis * 0.07f);

    DrawRectangleRounded((Rectangle){scaledPanel.x - 10.0f, scaledPanel.y - 8.0f, scaledPanel.width + 20.0f, scaledPanel.height + 16.0f}, 0.22f, 8, Fade(glowColor, alpha * (0.05f + 0.17f * emphasis)));
    DrawRectangleRounded((Rectangle){scaledPanel.x + 8.0f, scaledPanel.y + 12.0f, scaledPanel.width, scaledPanel.height}, 0.18f, 8, Fade(BLACK, 0.13f * alpha));
    DrawRectangleRounded(scaledPanel, 0.18f, 8, Fade(panelColor, alpha));
    DrawRectangleLinesEx(scaledPanel, 2.0f + emphasis * 1.2f, Fade(borderColor, alpha));
    DrawRectangleRounded((Rectangle){scaledPanel.x + 14.0f, scaledPanel.y + 10.0f, scaledPanel.width - 28.0f, 6.0f + emphasis * 3.0f}, 0.5f, 8, Fade(glowColor, alpha * (0.50f + 0.24f * emphasis)));
    for (int i = 0; i < lineCount; i++)
    {
        const int lineFontSize = (lineCount == 1 ? 20 : fontSize) + (int)roundf(2.0f * emphasis);
        const int width = MeasureUiText(lines[i], lineFontSize);
        const float textY = scaledPanel.y + scaledPanel.height * 0.5f - (lineCount - 1) * lineStep * 0.5f - lineFontSize * 0.5f + i * lineStep + 2.0f;
        const float textX = scaledPanel.x + scaledPanel.width * 0.5f - width * 0.5f;
        DrawUiText(lines[i], textX + 1.2f, textY + 1.4f, lineFontSize, Fade(BLACK, alpha * 0.18f));
        DrawUiText(lines[i], textX, textY, lineFontSize, Fade(textColor, alpha));
    }
}

void DrawTurnPanel(const struct Map *map)
{
    const Rectangle panel = GetTurnPanelBounds();
    const Rectangle rollDiceButton = GetRollDiceButtonBounds();
    const Rectangle endTurnButton = GetEndTurnButtonBounds();
    const struct MatchSession *session = matchSessionGetActive();
    const Color turnAccent = map->currentPlayer >= PLAYER_RED && map->currentPlayer <= PLAYER_BLACK
                                 ? UiReadablePlayerColor(map->currentPlayer)
                                 : (Color){171, 82, 54, 255};
    const Color panelColor = UiPanelColor();
    const Color borderColor = ColorBrightness(turnAccent, -0.34f);
    const Color actionColor = ColorBrightness(turnAccent, -0.08f);
    const Color mutedButton = UiButtonFillColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    const bool humanControlledTurn = matchSessionLocalControlsPlayer(session, map->currentPlayer);
    const bool canEndTurn = gameCanEndTurn(map);
    const bool diceLocked = map->rolledThisTurn || uiIsDiceRolling();
    const bool rollButtonInteractive = humanControlledTurn && !diceLocked;
    const bool endTurnButtonInteractive = humanControlledTurn && canEndTurn;
    const Rectangle dieA = {panel.x + UiScaled(20.0f), panel.y + UiScaled(76.0f), UiScaled(54.0f), UiScaled(54.0f)};
    const Rectangle dieB = {panel.x + UiScaled(84.0f), panel.y + UiScaled(76.0f), UiScaled(54.0f), UiScaled(54.0f)};
    const int shownTotal = uiIsDiceRolling() ? (uiGetDisplayedDieA() + uiGetDisplayedDieB()) : map->lastDiceRoll;
    const char *turnTitle = TextFormat("%s: %s", loc("Turn"), PlayerName(map->currentPlayer));
    const float headerTextY = panel.y + UiScaled(28.0f);

    DrawPanelFrame(panel, 0.08f, UiScaled(6.0f), UiScaled(8.0f), 0.10f, panelColor, borderColor);
    DrawRectangleRounded((Rectangle){panel.x + UiScaled(12.0f), panel.y + UiScaled(12.0f), panel.width - UiScaled(24.0f), UiScaled(8.0f)}, 0.4f, 8, Fade(turnAccent, 0.82f));

    DrawUiText(turnTitle, panel.x + UiScaled(16.0f), headerTextY, UiScaledFont(22), turnAccent);
    DrawTurnPanelPlaytime(map, panel, mutedText);
    if (matchSessionIsNetplay(session))
    {
        const char *statusLabel = NetplayStatusLabel(session);
        const char *errorLabel = matchSessionGetConnectionError(session);
        if (statusLabel[0] != '\0')
        {
            DrawUiText(statusLabel, panel.x + UiScaled(16.0f), panel.y + panel.height - UiScaled(22.0f), UiScaledFont(14), mutedText);
        }
        if (errorLabel[0] != '\0')
        {
            DrawUiText(errorLabel, panel.x + UiScaled(16.0f), panel.y + panel.height - UiScaled(40.0f), UiScaledFont(13), (Color){146, 54, 46, 255});
        }
    }
    if (gameHasWinner(map))
    {
        const enum PlayerType winner = gameGetWinner(map);
        const int winnerPoints = gameComputeVictoryPoints(map, winner);
        char headline[64];
        char subheadline[64];
        char matchDuration[24];
        char totalDuration[24];
        char matchLength[48];
        char totalPlaytime[52];
        BuildVictoryHeadline(map, winner, headline, sizeof(headline));
        BuildVictorySubheadline(map, winner, subheadline, sizeof(subheadline));
        FormatElapsedDuration(uiGetCurrentMatchPlaytimeSeconds(), matchDuration, sizeof(matchDuration));
        FormatElapsedDuration(uiGetTotalPlaytimeSeconds(), totalDuration, sizeof(totalDuration));
        snprintf(matchLength, sizeof(matchLength), loc("Match length %s"), matchDuration);
        snprintf(totalPlaytime, sizeof(totalPlaytime), loc("Total playtime %s"), totalDuration);
        DrawUiText(loc("Game Over"), panel.x + UiScaled(16.0f), panel.y + UiScaled(52.0f), UiScaledFont(24), (Color){171, 82, 54, 255});
        DrawUiText(headline, panel.x + UiScaled(16.0f), panel.y + UiScaled(88.0f), UiScaledFont(28), winner == LocalHumanPlayer(map) ? (Color){54, 130, 72, 255} : UiReadablePlayerColor(winner));
        DrawUiText(subheadline, panel.x + UiScaled(16.0f), panel.y + UiScaled(124.0f), UiScaledFont(18), UiReadablePlayerColor(winner));
        DrawUiText(TextFormat(loc("%d victory points"), winnerPoints), panel.x + UiScaled(16.0f), panel.y + UiScaled(150.0f), UiScaledFont(18), mutedText);
        DrawUiText(loc("Board is locked"), panel.x + UiScaled(16.0f), panel.y + UiScaled(174.0f), UiScaledFont(20), textColor);
        DrawUiText(loc("Use the overlay buttons"), panel.x + UiScaled(16.0f), panel.y + UiScaled(202.0f), UiScaledFont(18), mutedText);
        DrawUiText(loc("to restart or return"), panel.x + UiScaled(16.0f), panel.y + UiScaled(224.0f), UiScaledFont(18), mutedText);
        DrawUiText(matchLength, panel.x + UiScaled(16.0f), panel.y + UiScaled(254.0f), UiScaledFont(18), textColor);
        DrawUiText(totalPlaytime, panel.x + UiScaled(16.0f), panel.y + UiScaled(278.0f), UiScaledFont(18), mutedText);
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        return;
    }

    if (map->phase == GAME_PHASE_SETUP)
    {
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        DrawUiText(loc("Setup Phase"), panel.x + UiScaled(16.0f), panel.y + UiScaled(48.0f), UiScaledFont(18), mutedText);
        DrawUiText(map->setupNeedsRoad ? loc("Place 1 road") : loc("Place 1 settlement"), panel.x + UiScaled(16.0f), panel.y + UiScaled(76.0f), UiScaledFont(24), textColor);
        DrawUiText(TextFormat(loc("Round %d / 8"), map->setupStep + 1), panel.x + UiScaled(16.0f), panel.y + UiScaled(110.0f), UiScaledFont(18), mutedText);
        return;
    }

    if (gameHasPendingDiscards(map))
    {
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        const enum PlayerType discardPlayer = gameGetCurrentDiscardPlayer(map);
        const int discardAmount = gameGetDiscardAmountForPlayer(map, discardPlayer);
        const bool aiDiscard = discardPlayer >= PLAYER_RED &&
                               discardPlayer <= PLAYER_BLACK &&
                               map->players[discardPlayer].controlMode == PLAYER_CONTROL_AI;
        const bool revealDiscard = gDiscardRevealPlayer == discardPlayer;
        DrawUiText(loc("Dice"), panel.x + UiScaled(16.0f), panel.y + UiScaled(48.0f), UiScaledFont(18), mutedText);
        DrawDie(dieA, uiGetDisplayedDieA(), uiIsDiceRolling() ? -8.0f : -2.0f, 1.0f);
        DrawDie(dieB, uiGetDisplayedDieB(), uiIsDiceRolling() ? 7.0f : 2.0f, 1.0f);
        DrawUiText(shownTotal > 0 ? TextFormat("%d", shownTotal) : "-", panel.x + UiScaled(178.0f), panel.y + UiScaled(72.0f), UiScaledFont(30), textColor);
        if (uiIsDiceRolling())
        {
            DrawUiText(loc("Rolling..."), panel.x + UiScaled(148.0f), panel.y + UiScaled(106.0f), UiScaledFont(16), mutedText);
        }
        DrawUiText(loc("Discard Cards"), panel.x + UiScaled(16.0f), panel.y + UiScaled(148.0f), UiScaledFont(24), (Color){171, 82, 54, 255});
        if (aiDiscard)
        {
            DrawUiText(loc("Waiting for AI discard"), panel.x + UiScaled(16.0f), panel.y + UiScaled(178.0f), UiScaledFont(18), mutedText);
        }
        else if (!revealDiscard)
        {
            DrawUiText(TextFormat(loc("%s is up next"), PlayerName(discardPlayer)), panel.x + UiScaled(16.0f), panel.y + UiScaled(178.0f), UiScaledFont(18), mutedText);
            DrawUiText(loc("Pass the screen to continue"), panel.x + UiScaled(16.0f), panel.y + UiScaled(200.0f), UiScaledFont(18), mutedText);
        }
        else
        {
            DrawUiText(TextFormat(loc("Choose %d cards to discard"), discardAmount), panel.x + UiScaled(16.0f), panel.y + UiScaled(178.0f), UiScaledFont(18), mutedText);
        }
        return;
    }

    if (gameNeedsThiefPlacement(map))
    {
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        DrawUiText(loc("Dice"), panel.x + UiScaled(16.0f), panel.y + UiScaled(48.0f), UiScaledFont(18), mutedText);
        DrawDie(dieA, uiGetDisplayedDieA(), uiIsDiceRolling() ? -8.0f : -2.0f, 1.0f);
        DrawDie(dieB, uiGetDisplayedDieB(), uiIsDiceRolling() ? 7.0f : 2.0f, 1.0f);
        DrawUiText(shownTotal > 0 ? TextFormat("%d", shownTotal) : "-", panel.x + UiScaled(178.0f), panel.y + UiScaled(72.0f), UiScaledFont(30), textColor);
        if (uiIsDiceRolling())
        {
            DrawUiText(loc("Rolling..."), panel.x + UiScaled(148.0f), panel.y + UiScaled(106.0f), UiScaledFont(16), mutedText);
        }
        DrawUiText(loc("Move Thief"), panel.x + UiScaled(16.0f), panel.y + UiScaled(148.0f), UiScaledFont(24), (Color){171, 82, 54, 255});
        DrawUiText(loc("Select a land tile"), panel.x + UiScaled(16.0f), panel.y + UiScaled(178.0f), UiScaledFont(18), mutedText);
        DrawUiText(loc("before ending turn"), panel.x + UiScaled(16.0f), panel.y + UiScaled(200.0f), UiScaledFont(18), mutedText);
        DrawTurnActionButton(rollDiceButton, loc("Roll Dice (Enter)"), 22, mutedButton, borderColor, UiDisabledTextColor(), actionColor, &gRollDiceButtonAnimation);
        DrawTurnActionButton(endTurnButton, loc("End Turn"), 22, UiDisabledFillColor(), UiDisabledBorderColor(), UiDisabledTextColor(), mutedButton, &gEndTurnButtonAnimation);
        return;
    }

    if (gameNeedsThiefVictimSelection(map))
    {
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        DrawUiText(loc("Dice"), panel.x + UiScaled(16.0f), panel.y + UiScaled(48.0f), UiScaledFont(18), mutedText);
        DrawDie(dieA, uiGetDisplayedDieA(), uiIsDiceRolling() ? -8.0f : -2.0f, 1.0f);
        DrawDie(dieB, uiGetDisplayedDieB(), uiIsDiceRolling() ? 7.0f : 2.0f, 1.0f);
        DrawUiText(shownTotal > 0 ? TextFormat("%d", shownTotal) : "-", panel.x + UiScaled(178.0f), panel.y + UiScaled(72.0f), UiScaledFont(30), textColor);
        if (uiIsDiceRolling())
        {
            DrawUiText(loc("Rolling..."), panel.x + UiScaled(148.0f), panel.y + UiScaled(106.0f), UiScaledFont(16), mutedText);
        }
        DrawUiText(loc("Steal Resource"), panel.x + UiScaled(16.0f), panel.y + UiScaled(148.0f), UiScaledFont(24), (Color){171, 82, 54, 255});
        if (map->players[map->currentPlayer].controlMode == PLAYER_CONTROL_AI)
        {
            DrawUiText(TextFormat(loc("%s is choosing"), PlayerName(map->currentPlayer)), panel.x + UiScaled(16.0f), panel.y + UiScaled(178.0f), UiScaledFont(18), mutedText);
            DrawUiText(loc("an enemy"), panel.x + UiScaled(16.0f), panel.y + UiScaled(200.0f), UiScaledFont(18), mutedText);
        }
        else
        {
            DrawUiText(loc("Choose one adjacent"), panel.x + UiScaled(16.0f), panel.y + UiScaled(178.0f), UiScaledFont(18), mutedText);
            DrawUiText(loc("player to rob"), panel.x + UiScaled(16.0f), panel.y + UiScaled(200.0f), UiScaledFont(18), mutedText);
        }
        return;
    }

    DrawUiText(loc("Dice"), panel.x + UiScaled(16.0f), panel.y + UiScaled(48.0f), UiScaledFont(18), mutedText);
    DrawDie(dieA, uiGetDisplayedDieA(), uiIsDiceRolling() ? -8.0f : -2.0f, 1.0f);
    DrawDie(dieB, uiGetDisplayedDieB(), uiIsDiceRolling() ? 7.0f : 2.0f, 1.0f);
    DrawUiText(shownTotal > 0 ? TextFormat("%d", shownTotal) : "-", panel.x + UiScaled(178.0f), panel.y + UiScaled(72.0f), UiScaledFont(30), textColor);
    if (uiIsDiceRolling())
    {
        DrawUiText(loc("Rolling..."), panel.x + UiScaled(148.0f), panel.y + UiScaled(106.0f), UiScaledFont(16), mutedText);
    }

    UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, rollButtonInteractive);
    UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, endTurnButtonInteractive);
    DrawTurnActionButton(rollDiceButton, loc("Roll Dice (Enter)"), 22, rollButtonInteractive ? actionColor : mutedButton, rollButtonInteractive ? ColorBrightness(actionColor, -0.18f) : borderColor, rollButtonInteractive ? RAYWHITE : UiDisabledTextColor(), actionColor, &gRollDiceButtonAnimation);
    DrawTurnActionButton(endTurnButton, loc("End Turn"), 22, endTurnButtonInteractive ? mutedButton : UiDisabledFillColor(), endTurnButtonInteractive ? borderColor : UiDisabledBorderColor(), endTurnButtonInteractive ? textColor : UiDisabledTextColor(), (Color){182, 141, 97, 255}, &gEndTurnButtonAnimation);
}

void DrawVictoryOverlay(const struct Map *map)
{
    if (!gameHasWinner(map))
    {
        return;
    }

    const enum PlayerType winner = gameGetWinner(map);
    const int winnerPoints = gameComputeVictoryPoints(map, winner);
    const Rectangle panel = GetVictoryOverlayBounds();
    const Rectangle restartButton = GetVictoryOverlayRestartButtonBounds();
    const Rectangle menuButton = GetVictoryOverlayMenuButtonBounds();
    const Color borderColor = UiPanelBorderColor();
    const Color panelColor = UiPanelColor();
    const Color textColor = UiTextColor();
    const Color mutedText = UiMutedTextColor();
    char headline[64];
    char winnerLabel[64];
    char subtitle[48];
    char matchTimeValue[24];
    char totalTimeValue[24];
    char matchTimeLine[48];
    char totalTimeLine[52];
    BuildVictoryHeadline(map, winner, headline, sizeof(headline));
    BuildVictorySubheadline(map, winner, winnerLabel, sizeof(winnerLabel));
    snprintf(subtitle, sizeof(subtitle), loc("%d victory points"), winnerPoints);
    FormatElapsedDuration(uiGetCurrentMatchPlaytimeSeconds(), matchTimeValue, sizeof(matchTimeValue));
    FormatElapsedDuration(uiGetTotalPlaytimeSeconds(), totalTimeValue, sizeof(totalTimeValue));
    snprintf(matchTimeLine, sizeof(matchTimeLine), loc("Match length %s"), matchTimeValue);
    snprintf(totalTimeLine, sizeof(totalTimeLine), loc("Total playtime %s"), totalTimeValue);
    const int headlineWidth = MeasureUiText(headline, 34);
    const int winnerWidth = MeasureUiText(winnerLabel, 24);
    const int subtitleWidth = MeasureUiText(subtitle, 18);
    const int matchTimeWidth = MeasureUiText(matchTimeLine, 18);
    const int totalTimeWidth = MeasureUiText(totalTimeLine, 18);
    const char *hint = loc("Choose what to do next.");
    const int hintWidth = MeasureUiText(hint, 18);
    const char *restartLabel = loc("Restart Game");
    const char *menuLabel = loc("Back to Menu");
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.36f));
    DrawPanelFrame(panel, 0.08f, 8.0f, 10.0f, 0.14f, panelColor, borderColor);

    DrawUiText(loc("Game Over"), panel.x + panel.width * 0.5f - MeasureUiText(loc("Game Over"), 20) * 0.5f, panel.y + 20.0f, 20, mutedText);
    DrawUiText(headline, panel.x + panel.width * 0.5f - headlineWidth * 0.5f, panel.y + 54.0f, 34, winner == LocalHumanPlayer(map) ? (Color){54, 130, 72, 255} : (Color){171, 82, 54, 255});
    DrawUiText(winnerLabel, panel.x + panel.width * 0.5f - winnerWidth * 0.5f, panel.y + 98.0f, 24, UiReadablePlayerColor(winner));
    DrawUiText(subtitle, panel.x + panel.width * 0.5f - subtitleWidth * 0.5f, panel.y + 130.0f, 18, textColor);
    DrawUiText(matchTimeLine, panel.x + panel.width * 0.5f - matchTimeWidth * 0.5f, panel.y + 156.0f, 18, textColor);
    DrawUiText(totalTimeLine, panel.x + panel.width * 0.5f - totalTimeWidth * 0.5f, panel.y + 180.0f, 18, mutedText);
    DrawUiText(hint, panel.x + panel.width * 0.5f - hintWidth * 0.5f, panel.y + 208.0f, 18, mutedText);

    DrawRectangleRounded((Rectangle){restartButton.x + 4.0f, restartButton.y + 6.0f, restartButton.width, restartButton.height}, 0.22f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(restartButton, 0.22f, 8, (Color){171, 82, 54, 255});
    DrawRectangleLinesEx(restartButton, 2.0f, (Color){120, 58, 37, 255});
    DrawCenteredUiTextFitted(restartLabel, restartButton, 19, 14, 0.0f, RAYWHITE);

    DrawRectangleRounded((Rectangle){menuButton.x + 4.0f, menuButton.y + 6.0f, menuButton.width, menuButton.height}, 0.22f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(menuButton, 0.22f, 8, UiSectionFillColor());
    DrawRectangleLinesEx(menuButton, 2.0f, borderColor);
    DrawCenteredUiTextFitted(menuLabel, menuButton, 19, 14, 0.0f, textColor);
}

static bool IsDarkUiTheme(void)
{
    return uiGetTheme() == UI_THEME_DARK;
}

static float GameplayUiScale(void)
{
    const float widthScale = (float)GetScreenWidth() / 1280.0f;
    const float heightScale = (float)GetScreenHeight() / 720.0f;
    const float rawScale = widthScale < heightScale ? widthScale : heightScale;
    float scale = 1.0f;

    if (rawScale > 1.0f)
    {
        scale += (rawScale - 1.0f) * 0.55f;
    }

    if (scale > 1.36f)
    {
        scale = 1.36f;
    }

    return scale;
}

static float UiScaled(float value)
{
    return roundf(value * GameplayUiScale());
}

static int UiScaledFont(int fontSize)
{
    return (int)roundf((float)fontSize * GameplayUiScale());
}

static Color UiPanelColor(void)
{
    return IsDarkUiTheme() ? (Color){38, 47, 60, 248} : (Color){244, 236, 217, 250};
}

static Color UiPanelHighlightColor(void)
{
    return IsDarkUiTheme() ? (Color){47, 58, 73, 252} : (Color){247, 240, 226, 252};
}

static Color UiSectionFillColor(void)
{
    return IsDarkUiTheme() ? (Color){59, 72, 90, 255} : (Color){236, 228, 208, 255};
}

static Color UiButtonFillColor(void)
{
    return IsDarkUiTheme() ? (Color){66, 80, 99, 255} : (Color){214, 202, 181, 255};
}

static Color UiTrackFillColor(void)
{
    return IsDarkUiTheme() ? (Color){48, 60, 76, 255} : (Color){224, 216, 198, 255};
}

static Color UiPanelBorderColor(void)
{
    return IsDarkUiTheme() ? (Color){132, 151, 176, 255} : (Color){118, 88, 56, 255};
}

static Color UiTextColor(void)
{
    return IsDarkUiTheme() ? (Color){236, 241, 246, 255} : (Color){54, 39, 29, 255};
}

static Color UiMutedTextColor(void)
{
    return IsDarkUiTheme() ? (Color){194, 205, 216, 255} : (Color){92, 70, 50, 255};
}

static Color UiDisabledFillColor(void)
{
    return IsDarkUiTheme() ? (Color){49, 60, 74, 255} : (Color){224, 216, 198, 255};
}

static Color UiDisabledBorderColor(void)
{
    return IsDarkUiTheme() ? (Color){102, 118, 138, 255} : (Color){154, 132, 108, 255};
}

static Color UiDisabledTextColor(void)
{
    return IsDarkUiTheme() ? (Color){146, 158, 172, 255} : (Color){132, 112, 91, 255};
}

static Color UiCloseButtonFillColor(void)
{
    return IsDarkUiTheme() ? (Color){70, 82, 100, 255} : (Color){224, 216, 198, 255};
}

static Color UiReadableAccent(Color color)
{
    if (!IsDarkUiTheme())
    {
        return color;
    }

    if (color.r < 80 && color.g < 80 && color.b < 80)
    {
        return (Color){170, 179, 190, color.a};
    }

    return ColorBrightness(color, 0.08f);
}

static Color UiReadablePlayerColor(enum PlayerType player)
{
    return UiReadableAccent(PlayerColor(player));
}

static void DrawPanelFrame(Rectangle panel, float roundness, float shadowOffsetX, float shadowOffsetY, float shadowAlpha, Color fill, Color border)
{
    DrawRectangleRounded(
        (Rectangle){panel.x + shadowOffsetX, panel.y + shadowOffsetY, panel.width, panel.height},
        roundness,
        8,
        Fade(BLACK, shadowAlpha));
    DrawRectangleRounded(panel, roundness, 8, fill);
    DrawRectangleLinesEx(panel, 2.0f, border);
}

static void DrawCenteredUiTextFitted(const char *text, Rectangle bounds, int preferredFontSize, int minFontSize, float yOffset, Color color)
{
    const int scaledPreferredFontSize = UiScaledFont(preferredFontSize);
    const int scaledMinFontSize = UiScaledFont(minFontSize);
    const int fontSize = FitUiTextFontSize(text, scaledPreferredFontSize, scaledMinFontSize, (int)bounds.width - (int)UiScaled(18.0f));
    const int textWidth = MeasureUiText(text, fontSize);
    DrawUiText(
        text,
        bounds.x + bounds.width * 0.5f - textWidth * 0.5f,
        bounds.y + bounds.height * 0.5f - (float)fontSize * 0.48f + yOffset,
        fontSize,
        color);
}

static void DrawLeftUiTextFitted(const char *text, Rectangle bounds, float padding, int preferredFontSize, int minFontSize, float yOffset, Color color)
{
    const int scaledPreferredFontSize = UiScaledFont(preferredFontSize);
    const int scaledMinFontSize = UiScaledFont(minFontSize);
    const int fontSize = FitUiTextFontSize(text, scaledPreferredFontSize, scaledMinFontSize, (int)(bounds.width - padding * 2.0f));
    DrawUiText(
        text,
        bounds.x + padding,
        bounds.y + bounds.height * 0.5f - (float)fontSize * 0.48f + yOffset,
        fontSize,
        color);
}

static void DrawModalCloseButton(Rectangle bounds, float alpha, Color fill, Color border, Color textColor)
{
    DrawRectangleRounded(bounds, 0.22f, 6, Fade(fill, alpha));
    DrawRectangleLinesEx(bounds, 1.5f, Fade(border, alpha));
    DrawCenteredUiTextFitted("X", bounds, 20, 18, 0.0f, Fade(textColor, alpha));
}

static float EaseAnimationValue(float current, float target, float speed)
{
    const float amount = speed * GetFrameTime();
    const float step = amount > 1.0f ? 1.0f : amount;
    const float next = current + (target - current) * step;
    return fabsf(next - target) < 0.01f ? target : next;
}

static Rectangle ScaleRectangleFromCenter(Rectangle bounds, float scale)
{
    const float extraWidth = bounds.width * (scale - 1.0f);
    const float extraHeight = bounds.height * (scale - 1.0f);
    return (Rectangle){
        bounds.x - extraWidth * 0.5f,
        bounds.y - extraHeight * 0.5f,
        bounds.width + extraWidth,
        bounds.height + extraHeight
    };
}

static void UpdateTurnButtonAnimation(struct ButtonAnimationState *state, Rectangle bounds, bool interactive)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = interactive && CheckCollisionPointRec(mouse, bounds);
    const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    state->hoverAmount = EaseAnimationValue(state->hoverAmount, hovered ? 1.0f : 0.0f, 11.0f);
    state->pressAmount = EaseAnimationValue(state->pressAmount, pressed ? 1.0f : 0.0f, 18.0f);
    state->activeAmount = EaseAnimationValue(state->activeAmount, interactive ? 1.0f : 0.0f, 8.0f);
}

static void DrawTurnActionButton(Rectangle bounds, const char *label, int fontSize, Color fillColor, Color borderColor, Color textColor, Color glowColor, const struct ButtonAnimationState *state)
{
    const float hoverAmount = state->hoverAmount;
    const float pressAmount = state->pressAmount;
    const float activeAmount = state->activeAmount;
    const float pulseWave = 0.5f + 0.5f * sinf((float)GetTime() * 5.4f);
    const float pulseAmount = activeAmount * (0.25f + 0.75f * pulseWave);
    const float scale = 1.0f + hoverAmount * 0.028f + pulseAmount * 0.024f - pressAmount * 0.015f;
    const float lift = hoverAmount * UiScaled(3.0f) + pulseAmount * UiScaled(2.2f) - pressAmount * UiScaled(1.6f);
    const Rectangle body = ScaleRectangleFromCenter((Rectangle){bounds.x, bounds.y - lift, bounds.width, bounds.height}, scale);
    const Rectangle shadow = {body.x + UiScaled(4.0f), body.y + UiScaled(6.0f) + hoverAmount * UiScaled(1.4f), body.width, body.height};
    const float glowExpand = UiScaled(3.0f) + UiScaled(4.0f) * pulseAmount;
    const Rectangle glow = {body.x - glowExpand, body.y - glowExpand, body.width + glowExpand * 2.0f, body.height + glowExpand * 2.0f};
    const int scaledFontSize = UiScaledFont(fontSize);
    const int minFontSize = UiScaledFont(fontSize - 5 > 13 ? fontSize - 5 : 13);
    const int fittedFontSize = FitUiTextFontSize(label, scaledFontSize, minFontSize, (int)body.width - (int)UiScaled(18.0f));
    const int labelWidth = MeasureUiText(label, fittedFontSize);
    const float labelX = body.x + body.width * 0.5f - labelWidth * 0.5f;
    const float labelY = body.y + body.height * 0.5f - (float)fittedFontSize * 0.48f + pressAmount * UiScaled(1.0f);

    DrawRectangleRounded(shadow, 0.22f, 8, Fade(BLACK, 0.10f + hoverAmount * 0.08f - pressAmount * 0.03f));
    if (hoverAmount > 0.01f || pressAmount > 0.01f || pulseAmount > 0.01f)
    {
        DrawRectangleRounded(glow, 0.24f, 8, Fade(glowColor, hoverAmount * 0.10f + pressAmount * 0.16f + pulseAmount * 0.20f));
    }
    DrawRectangleRounded(body, 0.18f, 8, ColorBrightness(fillColor, hoverAmount * 0.10f + pulseAmount * 0.07f - pressAmount * 0.08f));
    DrawRectangleLinesEx(body, 2.0f, ColorBrightness(borderColor, hoverAmount * 0.10f + pulseAmount * 0.12f));
    DrawUiText(label, labelX, labelY, fittedFontSize, textColor);
}

/* Bounds helpers are grouped separately because both rendering and input depend on them. */
#include "renderer_ui_bounds.inc"

/* Development-card, award-card, and private-info helpers stay renderer-ui-local here. */
#include "renderer_ui_helpers.inc"
