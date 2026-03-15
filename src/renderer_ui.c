#include "renderer_ui.h"

#include "game_logic.h"
#include "renderer.h"
#include "ui_state.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct ButtonAnimationState
{
    float hoverAmount;
    float pressAmount;
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
static Rectangle DevelopmentHandHitBounds(Rectangle bounds);
static void DrawAwardCard(Rectangle bounds, const char *title, const char *subtitle, const char *detail, enum PlayerType owner, Color accent);
static void BuildVictoryHeadline(const struct Map *map, enum PlayerType winner, char *buffer, size_t bufferSize);
static void BuildVictorySubheadline(const struct Map *map, enum PlayerType winner, char *buffer, size_t bufferSize);

static int BuildWrappedUiLines(const char *message, int fontSize, int maxWidth, char lines[][96], int maxLines)
{
    char textBuffer[192];
    int lineCount = 0;
    char *paragraph = NULL;

    if (message == NULL || message[0] == '\0' || lines == NULL || maxLines <= 0)
    {
        return 0;
    }

    strncpy(textBuffer, message, sizeof(textBuffer) - 1);
    textBuffer[sizeof(textBuffer) - 1] = '\0';
    paragraph = textBuffer;
    while (paragraph != NULL && lineCount < maxLines)
    {
        char currentLine[96] = {0};
        char *nextParagraph = strchr(paragraph, '\n');
        char *word = NULL;

        if (nextParagraph != NULL)
        {
            *nextParagraph = '\0';
            nextParagraph++;
        }

        word = strtok(paragraph, " ");
        while (word != NULL && lineCount < maxLines)
        {
            char candidate[96] = {0};

            if (currentLine[0] != '\0')
            {
                strncpy(candidate, currentLine, sizeof(candidate) - 1);
                strncat(candidate, " ", sizeof(candidate) - strlen(candidate) - 1);
                strncat(candidate, word, sizeof(candidate) - strlen(candidate) - 1);
            }
            else
            {
                strncpy(candidate, word, sizeof(candidate) - 1);
            }

            if (currentLine[0] != '\0' && MeasureUiText(candidate, fontSize) > maxWidth)
            {
                strncpy(lines[lineCount], currentLine, 95);
                lines[lineCount][95] = '\0';
                lineCount++;
                currentLine[0] = '\0';
                if (lineCount >= maxLines)
                {
                    break;
                }
                strncpy(currentLine, word, sizeof(currentLine) - 1);
            }
            else
            {
                strncpy(currentLine, candidate, sizeof(currentLine) - 1);
            }

            word = strtok(NULL, " ");
        }

        if (lineCount < maxLines && currentLine[0] != '\0')
        {
            strncpy(lines[lineCount], currentLine, 95);
            lines[lineCount][95] = '\0';
            lineCount++;
        }

        paragraph = nextParagraph;
    }

    return lineCount;
}

static void BuildVictoryHeadline(const struct Map *map, enum PlayerType winner, char *buffer, size_t bufferSize)
{
    const enum PlayerType localHuman = LocalHumanPlayer(map);

    if (buffer == NULL || bufferSize == 0)
    {
        return;
    }

    if (localHuman != PLAYER_NONE)
    {
        snprintf(buffer, bufferSize, "%s", winner == localHuman ? "You won" : "You lost");
        return;
    }

    snprintf(buffer, bufferSize, "%s won", PlayerName(winner));
}

static void BuildVictorySubheadline(const struct Map *map, enum PlayerType winner, char *buffer, size_t bufferSize)
{
    const enum PlayerType localHuman = LocalHumanPlayer(map);

    if (buffer == NULL || bufferSize == 0)
    {
        return;
    }

    if (localHuman != PLAYER_NONE)
    {
        if (winner == localHuman)
        {
            snprintf(buffer, bufferSize, "%s", "You reached 10 points");
        }
        else
        {
            snprintf(buffer, bufferSize, "%s won", PlayerName(winner));
        }
        return;
    }

    snprintf(buffer, bufferSize, "%s reaches 10 points", PlayerName(winner));
}

void DrawBuildPanel(const struct Map *map)
{
    const Rectangle panel = GetBuildPanelBounds();
    const float panelX = panel.x;
    const float panelY = panel.y;
    const float openAmount = uiGetBuildPanelOpenAmount();
    const Color panelColor = (Color){232, 220, 196, 245};
    const Color borderColor = (Color){118, 88, 56, 255};
    const float cardWidth = 170.0f;
    const float cardHeight = 56.0f;
    const Rectangle roadCard = {panelX + 18.0f, panelY + 52.0f, cardWidth, cardHeight};
    const Rectangle settlementCard = {panelX + 204.0f, panelY + 52.0f, cardWidth, cardHeight};
    const Rectangle cityCard = {panelX + 18.0f, panelY + 118.0f, cardWidth, cardHeight};
    const Rectangle devCard = GetBuildPanelDevelopmentCardBounds();
    const Color activeCard = (Color){244, 236, 217, 255};
    const Color armedAccent = (Color){171, 82, 54, 255};
    const Color inactiveCard = (Color){236, 228, 208, 255};
    const Color disabledCard = (Color){224, 216, 198, 255};
    const Color disabledBorder = (Color){154, 132, 108, 255};
    const Color disabledText = (Color){132, 112, 91, 255};
    const bool setupSettlementMode = gameIsSetupSettlementTurn(map);
    const bool setupRoadMode = gameIsSetupRoadTurn(map);
    const bool canBuyRoad = setupRoadMode || gameCanAffordRoad(map);
    const bool canBuySettlement = setupSettlementMode || gameCanAffordSettlement(map);
    const bool canBuyCity = map->phase == GAME_PHASE_PLAY && gameCanAffordCity(map);
    const bool canBuyDevelopment = gameCanBuyDevelopment(map);
    const unsigned char contentAlpha = (unsigned char)(255.0f * ((openAmount - 0.55f) / 0.45f < 0.0f ? 0.0f : ((openAmount - 0.55f) / 0.45f > 1.0f ? 1.0f : (openAmount - 0.55f) / 0.45f)));

    if (openAmount < 0.98f)
    {
        DrawRectangleRounded((Rectangle){panelX + 6.0f, panelY + 8.0f, panel.width, panel.height}, 0.12f, 8, Fade(BLACK, 0.10f));
        DrawRectangleRounded(panel, 0.12f, 8, panelColor);
        DrawRectangleLinesEx(panel, 2.0f, borderColor);
        DrawUiText("Build (B)", panelX + 18.0f, panelY + 14.0f, 24, (Color){54, 39, 29, 255});
        if (!uiIsBuildPanelOpen())
        {
            return;
        }
    }

    if (openAmount < 0.98f)
    {
        return;
    }

    DrawRectangleRounded((Rectangle){panelX + 6.0f, panelY + 8.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);
    DrawUiText("Build (B)", panelX + 18.0f, panelY + 14.0f, 24, (Color){54, 39, 29, 255});

    DrawRectangleRounded(roadCard, 0.14f, 8, Fade(canBuyRoad ? activeCard : disabledCard, contentAlpha / 255.0f));
    DrawRectangleLinesEx(roadCard, 2.0f, Fade(gBuildMode == BUILD_MODE_ROAD ? armedAccent : (canBuyRoad ? borderColor : disabledBorder), contentAlpha / 255.0f));
    DrawUiText("Road", roadCard.x + 12.0f, roadCard.y + 8.0f, 20, Fade(canBuyRoad ? (Color){54, 39, 29, 255} : disabledText, contentAlpha / 255.0f));
    DrawUiText("Wood + Clay", roadCard.x + 12.0f, roadCard.y + 31.0f, 14, Fade(canBuyRoad ? (Color){92, 70, 50, 255} : disabledText, contentAlpha / 255.0f));
    DrawLineEx((Vector2){roadCard.x + 112.0f, roadCard.y + 18.0f}, (Vector2){roadCard.x + 152.0f, roadCard.y + 38.0f}, 8.0f, Fade(canBuyRoad ? (Color){230, 41, 55, 255} : Fade((Color){230, 41, 55, 255}, 0.35f), contentAlpha / 255.0f));
    DrawLineEx((Vector2){roadCard.x + 112.0f, roadCard.y + 18.0f}, (Vector2){roadCard.x + 152.0f, roadCard.y + 38.0f}, 2.0f, Fade(RAYWHITE, 0.24f * (contentAlpha / 255.0f)));
    DrawRectangleRounded(settlementCard, 0.14f, 8, Fade(canBuySettlement ? inactiveCard : disabledCard, contentAlpha / 255.0f));
    DrawRectangleLinesEx(settlementCard, 2.0f, Fade(gBuildMode == BUILD_MODE_SETTLEMENT ? armedAccent : (canBuySettlement ? borderColor : disabledBorder), contentAlpha / 255.0f));
    DrawUiText("Settlement", settlementCard.x + 12.0f, settlementCard.y + 8.0f, 20, Fade(canBuySettlement ? (Color){54, 39, 29, 255} : disabledText, contentAlpha / 255.0f));
    DrawUiText("Wood Clay Wheat Sheep", settlementCard.x + 12.0f, settlementCard.y + 31.0f, 11, Fade(canBuySettlement ? (Color){92, 70, 50, 255} : disabledText, contentAlpha / 255.0f));
    DrawRectangle((int)(settlementCard.x + 126), (int)(settlementCard.y + 22), 24, 18, Fade(canBuySettlement ? (Color){170, 118, 72, 255} : Fade((Color){170, 118, 72, 255}, 0.45f), contentAlpha / 255.0f));
    DrawTriangle((Vector2){settlementCard.x + 138, settlementCard.y + 10}, (Vector2){settlementCard.x + 124, settlementCard.y + 24}, (Vector2){settlementCard.x + 152, settlementCard.y + 24}, Fade(canBuySettlement ? (Color){132, 80, 49, 255} : Fade((Color){132, 80, 49, 255}, 0.45f), contentAlpha / 255.0f));
    DrawRectangleRounded(cityCard, 0.14f, 8, Fade(canBuyCity ? inactiveCard : disabledCard, contentAlpha / 255.0f));
    DrawRectangleLinesEx(cityCard, 2.0f, Fade(gBuildMode == BUILD_MODE_CITY ? armedAccent : (canBuyCity ? borderColor : disabledBorder), contentAlpha / 255.0f));
    DrawUiText("City", cityCard.x + 12.0f, cityCard.y + 8.0f, 20, Fade(canBuyCity ? (Color){54, 39, 29, 255} : disabledText, contentAlpha / 255.0f));
    DrawUiText("2 Wheat + 3 Stone", cityCard.x + 12.0f, cityCard.y + 31.0f, 13, Fade(canBuyCity ? (Color){92, 70, 50, 255} : disabledText, contentAlpha / 255.0f));
    DrawRectangle((int)(cityCard.x + 122), (int)(cityCard.y + 24), 30, 18, Fade(canBuyCity ? (Color){170, 118, 72, 255} : Fade((Color){170, 118, 72, 255}, 0.45f), contentAlpha / 255.0f));
    DrawRectangle((int)(cityCard.x + 127), (int)(cityCard.y + 15), 20, 12, Fade(canBuyCity ? (Color){170, 118, 72, 255} : Fade((Color){170, 118, 72, 255}, 0.45f), contentAlpha / 255.0f));
    DrawTriangle((Vector2){cityCard.x + 137, cityCard.y + 6}, (Vector2){cityCard.x + 125, cityCard.y + 16}, (Vector2){cityCard.x + 149, cityCard.y + 16}, Fade(canBuyCity ? (Color){132, 80, 49, 255} : Fade((Color){132, 80, 49, 255}, 0.45f), contentAlpha / 255.0f));
    DrawRectangleRounded(devCard, 0.14f, 8, Fade(canBuyDevelopment ? inactiveCard : disabledCard, contentAlpha / 255.0f));
    DrawRectangleLinesEx(devCard, 2.0f, Fade(canBuyDevelopment ? borderColor : disabledBorder, contentAlpha / 255.0f));
    DrawUiText("Development", devCard.x + 12.0f, devCard.y + 8.0f, 18, Fade(canBuyDevelopment ? (Color){54, 39, 29, 255} : disabledText, contentAlpha / 255.0f));
    DrawUiText("Wheat Sheep Stone", devCard.x + 12.0f, devCard.y + 31.0f, 13, Fade(canBuyDevelopment ? (Color){92, 70, 50, 255} : disabledText, contentAlpha / 255.0f));
    DrawRectangleRounded((Rectangle){devCard.x + 126.0f, devCard.y + 10.0f, 22.0f, 32.0f}, 0.12f, 6, Fade(canBuyDevelopment ? (Color){191, 171, 111, 255} : Fade((Color){191, 171, 111, 255}, 0.45f), contentAlpha / 255.0f));
    DrawRectangleLinesEx((Rectangle){devCard.x + 126.0f, devCard.y + 10.0f, 22.0f, 32.0f}, 1.5f, Fade(canBuyDevelopment ? (Color){120, 96, 53, 255} : disabledBorder, contentAlpha / 255.0f));
}

void DrawTradeButton(const struct Map *map)
{
    const Rectangle button = GetTradeButtonBounds();
    const bool isPlayable = map->phase == GAME_PHASE_PLAY;
    DrawRectangleRounded(button, 0.18f, 8, isPlayable ? (Color){214, 202, 181, 255} : (Color){228, 220, 202, 255});
    DrawRectangleLinesEx(button, 2.0f, (Color){118, 88, 56, 255});
    DrawUiText("Water Trade (W)", button.x + 14.0f, button.y + 11.0f, 20, isPlayable ? (Color){54, 39, 29, 255} : (Color){132, 112, 91, 255});
}

void DrawPlayerTradeButton(const struct Map *map)
{
    const Rectangle button = GetPlayerTradeButtonBounds();
    const bool isPlayable = map->phase == GAME_PHASE_PLAY;
    DrawRectangleRounded(button, 0.18f, 8, isPlayable ? (Color){214, 202, 181, 255} : (Color){228, 220, 202, 255});
    DrawRectangleLinesEx(button, 2.0f, (Color){118, 88, 56, 255});
    DrawUiText("Player Trade (T)", button.x + 10.0f, button.y + 11.0f, 20, isPlayable ? (Color){54, 39, 29, 255} : (Color){132, 112, 91, 255});
}

void DrawSettingsButton(void)
{
    const Rectangle button = GetSettingsButtonBounds();
    DrawRectangleRounded(button, 0.18f, 8, (Color){214, 202, 181, 255});
    DrawRectangleLinesEx(button, 2.0f, (Color){118, 88, 56, 255});
    DrawUiText("Settings (Esc)", button.x + 14.0f, button.y + 10.0f, 20, (Color){54, 39, 29, 255});
}

void DrawSettingsModal(void)
{
    const Rectangle targetPanel = GetSettingsModalBounds();
    const float openAmount = uiGetSettingsMenuOpenAmount();
    const float eased = openAmount * openAmount * (3.0f - 2.0f * openAmount);
    const float scale = 0.9f + 0.1f * eased;
    const Rectangle panel = {
        targetPanel.x + targetPanel.width * (1.0f - scale) * 0.5f,
        targetPanel.y + targetPanel.height * (1.0f - scale) * 0.5f,
        targetPanel.width * scale,
        targetPanel.height * scale};
    const Rectangle closeButton = {panel.x + panel.width - 42.0f, panel.y + 12.0f, 28.0f, 28.0f};
    const Rectangle lightButton = {panel.x + 24.0f, panel.y + 82.0f, panel.width - 48.0f, 42.0f};
    const Rectangle darkButton = {panel.x + 24.0f, panel.y + 136.0f, panel.width - 48.0f, 42.0f};
    const Rectangle aiSpeedTrack = {panel.x + 36.0f, panel.y + 220.0f, panel.width - 72.0f, 10.0f};
    const Rectangle restartButton = {panel.x + 24.0f, panel.y + 292.0f, panel.width - 48.0f, 42.0f};
    const Rectangle backToMenuButton = {panel.x + 24.0f, panel.y + 346.0f, panel.width - 48.0f, 42.0f};
    const Rectangle quitButton = {panel.x + 24.0f, panel.y + 400.0f, panel.width - 48.0f, 42.0f};
    const Rectangle confirmPanel = {panel.x + 26.0f, panel.y + 274.0f, panel.width - 52.0f, 140.0f};
    const Rectangle confirmButton = {confirmPanel.x + 18.0f, confirmPanel.y + confirmPanel.height - 46.0f, 132.0f, 30.0f};
    const Rectangle cancelButton = {confirmPanel.x + confirmPanel.width - 110.0f, confirmPanel.y + confirmPanel.height - 46.0f, 92.0f, 30.0f};
    const Color panelColor = (Color){244, 236, 217, 250};
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){92, 70, 50, 255};
    const bool lightSelected = uiGetTheme() == UI_THEME_LIGHT;
    const bool darkSelected = uiGetTheme() == UI_THEME_DARK;
    const enum UiSettingsConfirmAction confirmAction = uiGetSettingsConfirmAction();
    const bool showingConfirm = confirmAction != UI_SETTINGS_CONFIRM_NONE;
    const int aiSpeed = uiGetAiSpeedSetting();
    const float aiSpeedNormalized = (float)aiSpeed / 10.0f;
    const float aiSpeedKnobX = aiSpeedTrack.x + aiSpeedTrack.width * aiSpeedNormalized;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.24f * eased));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.06f, 8, Fade(BLACK, 0.14f * eased));
    DrawRectangleRounded(panel, 0.06f, 8, Fade(panelColor, eased));
    DrawRectangleLinesEx(panel, 2.0f, Fade(borderColor, eased));
    if (openAmount < 0.90f)
    {
        return;
    }

    DrawUiText("Settings", panel.x + 22.0f, panel.y + 16.0f, 28, textColor);
    DrawUiText("Theme", panel.x + 22.0f, panel.y + 52.0f, 18, mutedText);
    DrawRectangleRounded(closeButton, 0.22f, 6, (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(closeButton, 1.5f, borderColor);
    DrawUiText("X", closeButton.x + 9.0f, closeButton.y + 4.0f, 20, textColor);

    DrawRectangleRounded(lightButton, 0.18f, 8, lightSelected ? (Color){171, 82, 54, 255} : (Color){236, 228, 208, 255});
    DrawRectangleLinesEx(lightButton, 2.0f, lightSelected ? borderColor : (Color){154, 132, 108, 255});
    DrawUiText("Light Mode", lightButton.x + 18.0f, lightButton.y + 10.0f, 20, lightSelected ? RAYWHITE : textColor);

    DrawRectangleRounded(darkButton, 0.18f, 8, darkSelected ? (Color){171, 82, 54, 255} : (Color){236, 228, 208, 255});
    DrawRectangleLinesEx(darkButton, 2.0f, darkSelected ? borderColor : (Color){154, 132, 108, 255});
    DrawUiText("Dark Mode", darkButton.x + 18.0f, darkButton.y + 10.0f, 20, darkSelected ? RAYWHITE : textColor);

    DrawUiText("AI Speed", panel.x + 22.0f, panel.y + 192.0f, 18, mutedText);
    DrawUiText("0 slow", aiSpeedTrack.x, aiSpeedTrack.y + 16.0f, 14, mutedText);
    DrawUiText("10 instant", aiSpeedTrack.x + aiSpeedTrack.width - MeasureUiText("10 instant", 14), aiSpeedTrack.y + 16.0f, 14, mutedText);
    DrawRectangleRounded(aiSpeedTrack, 0.45f, 8, (Color){224, 216, 198, 255});
    DrawRectangleRounded((Rectangle){aiSpeedTrack.x, aiSpeedTrack.y, aiSpeedTrack.width * aiSpeedNormalized, aiSpeedTrack.height}, 0.45f, 8, (Color){171, 82, 54, 255});
    DrawCircleV((Vector2){aiSpeedKnobX, aiSpeedTrack.y + aiSpeedTrack.height * 0.5f}, 9.0f, (Color){247, 240, 226, 255});
    DrawCircleLines((int)aiSpeedKnobX, (int)(aiSpeedTrack.y + aiSpeedTrack.height * 0.5f), 9.0f, borderColor);
    DrawUiText(TextFormat("%d", aiSpeed), panel.x + panel.width * 0.5f - 5.0f, aiSpeedTrack.y - 24.0f, 18, textColor);

    DrawUiText("Game", panel.x + 22.0f, panel.y + 262.0f, 18, mutedText);

    DrawRectangleRounded(restartButton, 0.18f, 8, (Color){236, 228, 208, 255});
    DrawRectangleLinesEx(restartButton, 2.0f, (Color){154, 132, 108, 255});
    DrawUiText("Restart Game", restartButton.x + 18.0f, restartButton.y + 10.0f, 20, textColor);

    DrawRectangleRounded(backToMenuButton, 0.18f, 8, (Color){236, 228, 208, 255});
    DrawRectangleLinesEx(backToMenuButton, 2.0f, (Color){154, 132, 108, 255});
    DrawUiText("Back to Menu", backToMenuButton.x + 18.0f, backToMenuButton.y + 10.0f, 20, textColor);

    DrawRectangleRounded(quitButton, 0.18f, 8, (Color){171, 82, 54, 255});
    DrawRectangleLinesEx(quitButton, 2.0f, borderColor);
    DrawUiText("Quit Game", quitButton.x + 18.0f, quitButton.y + 10.0f, 20, RAYWHITE);

    if (!showingConfirm)
    {
        return;
    }

    DrawRectangleRounded((Rectangle){confirmPanel.x + 4.0f, confirmPanel.y + 6.0f, confirmPanel.width, confirmPanel.height}, 0.08f, 8, Fade(BLACK, 0.06f));
    DrawRectangleRounded(confirmPanel, 0.08f, 8, (Color){248, 241, 225, 252});
    DrawRectangleLinesEx(confirmPanel, 2.0f, borderColor);

    if (confirmAction == UI_SETTINGS_CONFIRM_MAIN_MENU)
    {
        const char *confirmLabel = "Return to Menu";
        const int confirmLabelWidth = MeasureUiText(confirmLabel, 17);
        DrawUiText("Back to main menu?", confirmPanel.x + 18.0f, confirmPanel.y + 16.0f, 24, textColor);
        DrawUiText("Current progress will be lost.", confirmPanel.x + 18.0f, confirmPanel.y + 48.0f, 17, mutedText);
        DrawRectangleRounded(confirmButton, 0.18f, 8, (Color){182, 141, 97, 255});
        DrawRectangleLinesEx(confirmButton, 1.5f, borderColor);
        DrawUiText(confirmLabel, confirmButton.x + confirmButton.width * 0.5f - confirmLabelWidth * 0.5f, confirmButton.y + 6.0f, 17, RAYWHITE);
    }
    else if (confirmAction == UI_SETTINGS_CONFIRM_RESTART)
    {
        const char *confirmLabel = "Confirm Restart";
        const int confirmLabelWidth = MeasureUiText(confirmLabel, 17);
        DrawUiText("Restart game?", confirmPanel.x + 18.0f, confirmPanel.y + 16.0f, 24, textColor);
        DrawUiText("Current progress will be lost.", confirmPanel.x + 18.0f, confirmPanel.y + 48.0f, 17, mutedText);
        DrawRectangleRounded(confirmButton, 0.18f, 8, (Color){182, 141, 97, 255});
        DrawRectangleLinesEx(confirmButton, 1.5f, borderColor);
        DrawUiText(confirmLabel, confirmButton.x + confirmButton.width * 0.5f - confirmLabelWidth * 0.5f, confirmButton.y + 6.0f, 17, RAYWHITE);
    }
    else
    {
        const char *confirmLabel = "Confirm Quit";
        const int confirmLabelWidth = MeasureUiText(confirmLabel, 17);
        DrawUiText("Quit game?", confirmPanel.x + 18.0f, confirmPanel.y + 16.0f, 24, textColor);
        DrawUiText("The application will close.", confirmPanel.x + 18.0f, confirmPanel.y + 48.0f, 17, mutedText);
        DrawRectangleRounded(confirmButton, 0.18f, 8, (Color){171, 82, 54, 255});
        DrawRectangleLinesEx(confirmButton, 1.5f, borderColor);
        DrawUiText(confirmLabel, confirmButton.x + confirmButton.width * 0.5f - confirmLabelWidth * 0.5f, confirmButton.y + 6.0f, 17, RAYWHITE);
    }

    {
        const char *cancelLabel = "Cancel";
        const int cancelLabelWidth = MeasureUiText(cancelLabel, 17);
    DrawRectangleRounded(cancelButton, 0.18f, 8, (Color){236, 228, 208, 255});
    DrawRectangleLinesEx(cancelButton, 1.5f, (Color){154, 132, 108, 255});
        DrawUiText(cancelLabel, cancelButton.x + cancelButton.width * 0.5f - cancelLabelWidth * 0.5f, cancelButton.y + 6.0f, 17, textColor);
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
    const Color panelColor = (Color){244, 236, 217, 250};
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){92, 70, 50, 255};
    const bool isPlayable = map->phase == GAME_PHASE_PLAY;
    const bool canTrade = isPlayable && gameCanTradeMaritime(map, (enum ResourceType)gTradeGiveResource, gTradeAmount, (enum ResourceType)gTradeReceiveResource);
    const float alpha = eased;
    const int rate = gameGetMaritimeTradeRate(map, (enum ResourceType)gTradeGiveResource);
    const int maxTradeAmount = map->players[map->currentPlayer].resources[gTradeGiveResource] / rate;
    const float amountFill = maxTradeAmount > 0 ? (float)gTradeAmount / (float)maxTradeAmount : 0.0f;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.28f * alpha));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.06f, 8, Fade(BLACK, 0.14f * alpha));
    DrawRectangleRounded(panel, 0.06f, 8, Fade(panelColor, alpha));
    DrawRectangleLinesEx(panel, 2.0f, Fade(borderColor, alpha));
    if (openAmount < 0.90f)
    {
        return;
    }

    DrawUiText("Available Water Trades", panel.x + 22.0f, panel.y + 16.0f, 26, Fade(textColor, alpha));
    DrawRectangleRounded(closeButton, 0.22f, 6, Fade((Color){224, 216, 198, 255}, alpha));
    DrawRectangleLinesEx(closeButton, 1.5f, Fade(borderColor, alpha));
    DrawUiText("X", closeButton.x + 9.0f, closeButton.y + 4.0f, 20, Fade(textColor, alpha));

    DrawUiText("Give", panel.x + 24.0f, panel.y + 48.0f, 18, Fade(mutedText, alpha));
    DrawUiText("Receive", panel.x + 232.0f, panel.y + 48.0f, 18, Fade(mutedText, alpha));
    for (int i = 0; i < 5; i++)
    {
        const int optionRate = gameGetMaritimeTradeRate(map, (enum ResourceType)i);
        const bool available = isPlayable && map->players[map->currentPlayer].resources[i] >= optionRate;
        const bool selected = i == gTradeGiveResource;
        const bool receiveSelected = i == gTradeReceiveResource;
        const bool receiveAllowed = i != gTradeGiveResource;
        const Rectangle giveOption = {panel.x + 24.0f, panel.y + 74.0f + i * 34.0f, 180.0f, 28.0f};
        const Rectangle receiveOption = {panel.x + 232.0f, panel.y + 74.0f + i * 34.0f, 180.0f, 28.0f};

        DrawRectangleRounded(giveOption, 0.18f, 6, selected ? (Color){171, 82, 54, 255} : (available ? (Color){236, 228, 208, 255} : (Color){224, 216, 198, 255}));
        DrawRectangleLinesEx(giveOption, 1.5f, selected ? (Color){118, 88, 56, 255} : borderColor);
        DrawUiText(TextFormat("%s  %d:1", ResourceName((enum ResourceType)i), optionRate), giveOption.x + 10.0f, giveOption.y + 6.0f, 16, selected ? RAYWHITE : (available ? textColor : (Color){132, 112, 91, 255}));

        DrawRectangleRounded(receiveOption, 0.18f, 6, receiveSelected ? (Color){171, 82, 54, 255} : (receiveAllowed ? (Color){236, 228, 208, 255} : (Color){224, 216, 198, 255}));
        DrawRectangleLinesEx(receiveOption, 1.5f, receiveSelected ? (Color){118, 88, 56, 255} : borderColor);
        DrawUiText(ResourceName((enum ResourceType)i), receiveOption.x + 10.0f, receiveOption.y + 6.0f, 16, receiveSelected ? RAYWHITE : (receiveAllowed ? textColor : (Color){132, 112, 91, 255}));
    }

    DrawUiText("Amount", panel.x + panel.width * 0.5f - 32.0f, panel.y + 248.0f, 18, Fade(mutedText, alpha));
    const Rectangle tradeMinus = {panel.x + panel.width * 0.5f - 112.0f, panel.y + 276.0f, 28.0f, 28.0f};
    const Rectangle tradeTrack = {panel.x + panel.width * 0.5f - 68.0f, panel.y + 284.0f, 136.0f, 12.0f};
    const Rectangle tradePlus = {panel.x + panel.width * 0.5f + 84.0f, panel.y + 276.0f, 28.0f, 28.0f};
    DrawRectangleRounded(tradeMinus, 0.22f, 6, (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(tradeMinus, 1.5f, borderColor);
    DrawUiText("-", tradeMinus.x + 10.0f, tradeMinus.y + 2.0f, 24, textColor);
    DrawRectangleRounded(tradeTrack, 0.45f, 8, (Color){224, 216, 198, 255});
    DrawRectangleRounded((Rectangle){tradeTrack.x, tradeTrack.y, tradeTrack.width * amountFill, tradeTrack.height}, 0.45f, 8, (Color){171, 82, 54, 255});
    {
        const char *amountLabel = TextFormat("%d", gTradeAmount);
        const int amountWidth = MeasureUiText(amountLabel, 18);
        DrawUiText(amountLabel, tradeTrack.x + tradeTrack.width * 0.5f - amountWidth * 0.5f, tradeTrack.y - 20.0f, 18, textColor);
    }
    DrawRectangleRounded(tradePlus, 0.22f, 6, (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(tradePlus, 1.5f, borderColor);
    DrawUiText("+", tradePlus.x + 8.0f, tradePlus.y + 2.0f, 24, textColor);

    DrawRectangleRounded(confirmButton, 0.16f, 8, canTrade ? (Color){171, 82, 54, 255} : (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(confirmButton, 2.0f, canTrade ? borderColor : (Color){154, 132, 108, 255});
    {
        const char *confirmLabel = TextFormat("Trade %d %s for %d %s", rate * gTradeAmount, ResourceName((enum ResourceType)gTradeGiveResource), gTradeAmount, ResourceName((enum ResourceType)gTradeReceiveResource));
        const int labelWidth = MeasureUiText(confirmLabel, 18);
        DrawUiText(confirmLabel, confirmButton.x + confirmButton.width * 0.5f - labelWidth * 0.5f, confirmButton.y + confirmButton.height * 0.5f - 9.0f, 18, canTrade ? RAYWHITE : (Color){132, 112, 91, 255});
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
    const Color panelColor = (Color){244, 236, 217, 250};
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){92, 70, 50, 255};
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
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.06f, 8, Fade(BLACK, 0.14f * alpha));
    DrawRectangleRounded(panel, 0.06f, 8, Fade(panelColor, alpha));
    DrawRectangleLinesEx(panel, 2.0f, Fade(borderColor, alpha));
    if (openAmount < 0.90f)
    {
        return;
    }

    DrawUiText("Player Trade", panel.x + 22.0f, panel.y + 16.0f, 26, Fade(textColor, alpha));
    DrawRectangleRounded(closeButton, 0.22f, 6, Fade((Color){224, 216, 198, 255}, alpha));
    DrawRectangleLinesEx(closeButton, 1.5f, Fade(borderColor, alpha));
    DrawUiText("X", closeButton.x + 9.0f, closeButton.y + 4.0f, 20, Fade(textColor, alpha));

    DrawUiText("Trade With", panel.x + 24.0f, panel.y + 48.0f, 18, Fade(mutedText, alpha));
    int playerSlot = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        const enum PlayerType candidate = (enum PlayerType)i;
        if (candidate == map->currentPlayer)
        {
            continue;
        }

        const bool selected = candidate == gPlayerTradeTarget;
        const Rectangle playerOption = {panel.x + 24.0f + playerSlot * 130.0f, panel.y + 74.0f, 116.0f, 32.0f};
        DrawRectangleRounded(playerOption, 0.18f, 6, selected ? Fade(PlayerColor(candidate), 0.95f) : (Color){236, 228, 208, 255});
        DrawRectangleLinesEx(playerOption, 1.5f, selected ? borderColor : (Color){154, 132, 108, 255});
        {
            const char *label = PlayerName(candidate);
            const int labelWidth = MeasureUiText(label, 16);
            DrawUiText(label, playerOption.x + playerOption.width * 0.5f - labelWidth * 0.5f, playerOption.y + 7.0f, 16, selected ? RAYWHITE : textColor);
        }
        playerSlot++;
    }

    DrawUiText("You Give", panel.x + 24.0f, panel.y + 132.0f, 18, Fade(mutedText, alpha));
    DrawUiText("You Receive", panel.x + 232.0f, panel.y + 132.0f, 18, Fade(mutedText, alpha));
    for (int i = 0; i < 5; i++)
    {
        const bool giveAvailable = map->players[map->currentPlayer].resources[i] > 0;
        const bool giveSelected = i == gPlayerTradeGiveResource && giveAvailable;
        const bool receiveAllowed = i != gPlayerTradeGiveResource;
        const bool receiveSelected = i == gPlayerTradeReceiveResource && receiveAllowed;
        const Rectangle giveOption = {panel.x + 24.0f, panel.y + 160.0f + i * 28.0f, 180.0f, 22.0f};
        const Rectangle receiveOption = {panel.x + 232.0f, panel.y + 160.0f + i * 28.0f, 180.0f, 22.0f};
        const Color disabledFill = (Color){224, 216, 198, 255};
        const Color disabledBorder = (Color){154, 132, 108, 255};
        const Color disabledText = (Color){132, 112, 91, 255};

        DrawRectangleRounded(giveOption, 0.18f, 6, giveSelected ? (Color){171, 82, 54, 255} : (giveAvailable ? (Color){236, 228, 208, 255} : disabledFill));
        DrawRectangleLinesEx(giveOption, 1.5f, giveSelected ? borderColor : (giveAvailable ? (Color){154, 132, 108, 255} : disabledBorder));
        DrawUiText(TextFormat("%s  %d", ResourceName((enum ResourceType)i), map->players[map->currentPlayer].resources[i]), giveOption.x + 10.0f, giveOption.y + 3.0f, 14, giveSelected ? RAYWHITE : (giveAvailable ? textColor : disabledText));

        DrawRectangleRounded(receiveOption, 0.18f, 6, receiveSelected ? (Color){171, 82, 54, 255} : (receiveAllowed ? (Color){236, 228, 208, 255} : disabledFill));
        DrawRectangleLinesEx(receiveOption, 1.5f, receiveSelected ? borderColor : (receiveAllowed ? (Color){154, 132, 108, 255} : disabledBorder));
        DrawUiText(ResourceName((enum ResourceType)i), receiveOption.x + 10.0f, receiveOption.y + 3.0f, 14, receiveSelected ? RAYWHITE : (receiveAllowed ? textColor : disabledText));
    }

    DrawUiText("Your Amount", panel.x + 24.0f, panel.y + 294.0f, 18, Fade(mutedText, alpha));
    const Rectangle giveMinus = {panel.x + 24.0f, panel.y + 326.0f, 28.0f, 28.0f};
    const Rectangle giveTrack = {panel.x + 68.0f, panel.y + 334.0f, 92.0f, 12.0f};
    const Rectangle givePlus = {panel.x + 176.0f, panel.y + 326.0f, 28.0f, 28.0f};
    DrawRectangleRounded(giveMinus, 0.22f, 6, (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(giveMinus, 1.5f, borderColor);
    DrawUiText("-", giveMinus.x + 10.0f, giveMinus.y + 2.0f, 24, textColor);
    DrawRectangleRounded(giveTrack, 0.45f, 8, (Color){224, 216, 198, 255});
    DrawRectangleRounded((Rectangle){giveTrack.x, giveTrack.y, giveTrack.width * giveFill, giveTrack.height}, 0.45f, 8, (Color){171, 82, 54, 255});
    {
        const char *amountLabel = TextFormat("%d", gPlayerTradeGiveAmount);
        const int amountWidth = MeasureUiText(amountLabel, 18);
        DrawUiText(amountLabel, giveTrack.x + giveTrack.width * 0.5f - amountWidth * 0.5f, giveTrack.y - 20.0f, 18, textColor);
    }
    DrawRectangleRounded(givePlus, 0.22f, 6, (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(givePlus, 1.5f, borderColor);
    DrawUiText("+", givePlus.x + 8.0f, givePlus.y + 2.0f, 24, textColor);

    DrawUiText("Their Amount", panel.x + 232.0f, panel.y + 294.0f, 18, Fade(mutedText, alpha));
    const Rectangle receiveMinus = {panel.x + 232.0f, panel.y + 326.0f, 28.0f, 28.0f};
    const Rectangle receiveTrack = {panel.x + 276.0f, panel.y + 334.0f, 92.0f, 12.0f};
    const Rectangle receivePlus = {panel.x + 384.0f, panel.y + 326.0f, 28.0f, 28.0f};
    DrawRectangleRounded(receiveMinus, 0.22f, 6, (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(receiveMinus, 1.5f, borderColor);
    DrawUiText("-", receiveMinus.x + 10.0f, receiveMinus.y + 2.0f, 24, textColor);
    DrawRectangleRounded(receiveTrack, 0.45f, 8, (Color){224, 216, 198, 255});
    DrawRectangleRounded((Rectangle){receiveTrack.x, receiveTrack.y, receiveTrack.width * receiveFill, receiveTrack.height}, 0.45f, 8, (Color){171, 82, 54, 255});
    {
        const char *amountLabel = TextFormat("%d", gPlayerTradeReceiveAmount);
        const int amountWidth = MeasureUiText(amountLabel, 18);
        DrawUiText(amountLabel, receiveTrack.x + receiveTrack.width * 0.5f - amountWidth * 0.5f, receiveTrack.y - 20.0f, 18, textColor);
    }
    DrawRectangleRounded(receivePlus, 0.22f, 6, (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(receivePlus, 1.5f, borderColor);
    DrawUiText("+", receivePlus.x + 8.0f, receivePlus.y + 2.0f, 24, textColor);

    DrawRectangleRounded(confirmButton, 0.16f, 8, canTrade ? (Color){171, 82, 54, 255} : (Color){224, 216, 198, 255});
    DrawRectangleLinesEx(confirmButton, 2.0f, canTrade ? borderColor : (Color){154, 132, 108, 255});
    {
        const char *confirmLabel = TextFormat("Trade %d %s for %d %s", gPlayerTradeGiveAmount, ResourceName((enum ResourceType)gPlayerTradeGiveResource), gPlayerTradeReceiveAmount, ResourceName((enum ResourceType)gPlayerTradeReceiveResource));
        const int labelWidth = MeasureUiText(confirmLabel, 18);
        DrawUiText(confirmLabel, confirmButton.x + confirmButton.width * 0.5f - labelWidth * 0.5f, confirmButton.y + confirmButton.height * 0.5f - 9.0f, 18, canTrade ? RAYWHITE : (Color){132, 112, 91, 255});
    }
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
    const Color panelColor = (Color){244, 236, 217, 250};
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){92, 70, 50, 255};
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
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.06f, 8, Fade(BLACK, 0.14f));
    DrawRectangleRounded(panel, 0.06f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    if (!revealDiscard)
    {
        const char *title = "Discard Half";
        const char *lineA = TextFormat("%s needs to discard %d cards.", PlayerName(discardPlayer), discardRequired);
        const char *lineB = "Pass the screen, then click anywhere here.";
        const char *lineC = "Resource counts stay hidden until they continue.";
        DrawUiText(title, panel.x + 22.0f, panel.y + 24.0f, 28, textColor);
        DrawUiText(lineA, panel.x + 22.0f, panel.y + 88.0f, 20, mutedText);
        DrawUiText(lineB, panel.x + 22.0f, panel.y + 132.0f, 20, textColor);
        DrawUiText(lineC, panel.x + 22.0f, panel.y + 166.0f, 18, mutedText);
        return;
    }

    DrawUiText("Discard Half", panel.x + 22.0f, panel.y + 16.0f, 28, textColor);
    DrawUiText(TextFormat("%s must discard %d cards", PlayerName(discardPlayer), discardRequired), panel.x + 22.0f, panel.y + 50.0f, 18, mutedText);

    for (int resource = 0; resource < 5; resource++)
    {
        const float rowY = panel.y + 92.0f + resource * 38.0f;
        const Rectangle minusButton = {panel.x + 182.0f, rowY, 26.0f, 26.0f};
        const Rectangle amountBox = {panel.x + 218.0f, rowY, 94.0f, 26.0f};
        const Rectangle plusButton = {panel.x + 322.0f, rowY, 26.0f, 26.0f};

        DrawUiText(ResourceName((enum ResourceType)resource), panel.x + 26.0f, rowY + 4.0f, 18, textColor);
        DrawRectangleRounded(minusButton, 0.22f, 6, (Color){224, 216, 198, 255});
        DrawRectangleLinesEx(minusButton, 1.5f, borderColor);
        DrawUiText("-", minusButton.x + 8.0f, minusButton.y + 1.0f, 24, textColor);
        DrawRectangleRounded(amountBox, 0.16f, 6, (Color){236, 228, 208, 255});
        DrawRectangleLinesEx(amountBox, 1.5f, borderColor);
        {
            const char *amountLabel = TextFormat("%d / %d", gDiscardSelection[resource], map->players[discardPlayer].resources[resource]);
            const int amountWidth = MeasureUiText(amountLabel, 16);
            DrawUiText(amountLabel, amountBox.x + amountBox.width * 0.5f - amountWidth * 0.5f, amountBox.y + 4.0f, 16, textColor);
        }
        DrawRectangleRounded(plusButton, 0.22f, 6, (Color){224, 216, 198, 255});
        DrawRectangleLinesEx(plusButton, 1.5f, borderColor);
        DrawUiText("+", plusButton.x + 7.0f, plusButton.y + 1.0f, 24, textColor);
    }

    DrawUiText(TextFormat("Selected %d / %d", discardSelected, discardRequired), panel.x + 22.0f, panel.y + 280.0f, 18, discardSelected == discardRequired ? (Color){54, 130, 72, 255} : mutedText);
    {
        const Rectangle confirmButton = {panel.x + 38.0f, panel.y + panel.height - 48.0f, panel.width - 76.0f, 36.0f};
        const bool ready = discardSelected == discardRequired;
        DrawRectangleRounded(confirmButton, 0.16f, 8, ready ? (Color){171, 82, 54, 255} : (Color){224, 216, 198, 255});
        DrawRectangleLinesEx(confirmButton, 2.0f, ready ? borderColor : (Color){154, 132, 108, 255});
        {
            const char *label = "Confirm Discard";
            const int width = MeasureUiText(label, 18);
            DrawUiText(label, confirmButton.x + confirmButton.width * 0.5f - width * 0.5f, confirmButton.y + 8.0f, 18, ready ? RAYWHITE : (Color){132, 112, 91, 255});
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
    const Color panelColor = (Color){244, 236, 217, 250};
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){92, 70, 50, 255};
    int victimIndex = 0;

    if (aiChooser)
    {
        return;
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.34f));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.06f, 8, Fade(BLACK, 0.14f));
    DrawRectangleRounded(panel, 0.06f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    if (!revealVictims)
    {
        DrawUiText("Steal Resource", panel.x + 22.0f, panel.y + 20.0f, 28, textColor);
        DrawUiText(TextFormat("%s is choosing an enemy.", PlayerName(map->currentPlayer)), panel.x + 22.0f, panel.y + 96.0f, 20, mutedText);
        return;
    }

    DrawUiText("Steal Resource", panel.x + 22.0f, panel.y + 16.0f, 28, textColor);
    DrawUiText("Choose one adjacent player to steal from", panel.x + 22.0f, panel.y + 52.0f, 18, mutedText);

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        const enum PlayerType victim = (enum PlayerType)player;
        if (!gameCanStealFromPlayer(map, victim))
        {
            continue;
        }

        const Rectangle victimButton = {panel.x + 26.0f + victimIndex * 118.0f, panel.y + 102.0f, 102.0f, 42.0f};
        DrawRectangleRounded(victimButton, 0.18f, 8, Fade(PlayerColor(victim), 0.88f));
        DrawRectangleLinesEx(victimButton, 2.0f, borderColor);
        {
            const char *label = PlayerName(victim);
            const int width = MeasureUiText(label, 18);
            DrawUiText(label, victimButton.x + victimButton.width * 0.5f - width * 0.5f, victimButton.y + 11.0f, 18, RAYWHITE);
        }
        victimIndex++;
    }
}

void DrawAwardCards(const struct Map *map)
{
    const enum PlayerType longestRoadOwner = gameGetLongestRoadOwner(map);
    const enum PlayerType largestArmyOwner = gameGetLargestArmyOwner(map);
    const float cardWidth = 224.0f;
    const float cardHeight = 176.0f;
    const float gap = 14.0f;
    const float cardX = 12.0f;
    const float firstCardY = (float)GetScreenHeight() - 12.0f - gap - cardHeight * 2.0f;
    const Rectangle longestRoadCard = {cardX, firstCardY, cardWidth, cardHeight};
    const Rectangle largestArmyCard = {cardX, firstCardY + cardHeight + gap, cardWidth, cardHeight};
    const char *longestRoadDetail = gameGetLongestRoadLength(map) >= 5
                                        ? TextFormat("Road length: %d", gameGetLongestRoadLength(map))
                                        : "Need 5-road chain";
    const char *largestArmyDetail = "Need 3 knights";

    if (largestArmyOwner >= PLAYER_RED && largestArmyOwner <= PLAYER_BLACK)
    {
        largestArmyDetail = TextFormat("Played knights: %d", map->players[largestArmyOwner].playedKnightCount);
    }

    DrawAwardCard(longestRoadCard,
                  "Longest Road",
                  "2 Victory Points",
                  longestRoadDetail,
                  longestRoadOwner,
                  (Color){159, 103, 58, 255});
    DrawAwardCard(largestArmyCard,
                  "Largest Army",
                  "2 Victory Points",
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
    const Color panelColor = (Color){244, 236, 217, 245};
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color buttonText = (Color){54, 39, 29, 255};

    DrawRectangleRounded((Rectangle){panelX + 6.0f, panelY + 8.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText(PlayerName(player->type), panelX + 16.0f, panelY + 14.0f, 26, PlayerColor(player->type));
    DrawUiText(showingPinnedLocalInfo ? "Your Hand" : "Current Player", panelX + 16.0f, panelY + 44.0f, 16, (Color){92, 70, 50, 255});

    const int totalVp = gameComputeVictoryPoints(map, player->type);
    const int visibleVp = gameComputeVisibleVictoryPoints(map, player->type);
    DrawUiText("Victory Points", panelX + 16.0f, panelY + 78.0f, 18, (Color){54, 39, 29, 255});
    {
        const char *vpLabel = TextFormat("%d (%d)", totalVp, visibleVp);
        const int vpWidth = MeasureUiText(vpLabel, 24);
        DrawUiText(vpLabel, panelX + panel.width - 18.0f - vpWidth, panelY + 76.0f, 24, (Color){54, 39, 29, 255});
    }

    DrawUiText("Developement Cards", panelX + 16.0f, panelY + 108.0f, 14, (Color){92, 70, 50, 255});
    {
        const char *deckCountLabel = TextFormat("%d left", gameGetDevelopmentDeckCount(map));
        const int deckCountWidth = MeasureUiText(deckCountLabel, 14);
        const float deckCountX = panelX + panel.width - 32.0f - deckCountWidth;
        const Color deckCountColor = (Color){46, 33, 25, 255};
        DrawUiText(deckCountLabel, deckCountX, panelY + 108.0f, 14, deckCountColor);
        DrawUiText(deckCountLabel, deckCountX + 0.8f, panelY + 108.0f, 14, deckCountColor);
    }

    DrawUiText("Resources", panelX + 16.0f, panelY + 126.0f, 18, buttonText);
    for (int resource = 0; resource < 5; resource++)
    {
        const float rowY = panelY + 148.0f + resource * 14.0f;
        const char *resourceCountLabel = TextFormat("%d", player->resources[resource]);
        const int resourceCountWidth = MeasureUiText(resourceCountLabel, 16);
        const int turnDelta = player->type == map->currentPlayer ? uiGetCurrentTurnResourceGain((enum ResourceType)resource) : 0;
        DrawUiText(ResourceName((enum ResourceType)resource), panelX + 18.0f, rowY, 16, (Color){92, 70, 50, 255});
        DrawUiText(resourceCountLabel, panelX + 202.0f - resourceCountWidth, rowY, 16, (Color){54, 39, 29, 255});
        if (turnDelta != 0)
        {
            const char *deltaLabel = TextFormat("%+d", turnDelta);
            const Color deltaColor = turnDelta > 0 ? (Color){70, 136, 78, 255} : (Color){176, 63, 52, 255};
            DrawUiText(deltaLabel, panelX + 208.0f, rowY, 15, deltaColor);
        }
    }
}

void DrawOpponentVictoryBar(const struct Map *map)
{
    enum PlayerType opponents[MAX_PLAYERS - 1];
    int opponentCount = 0;
    const struct PlayerState *player = CurrentPlayerState(map);
    Rectangle bar;
    const Color panelColor = (Color){244, 236, 217, 245};
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){92, 70, 50, 255};
    const float slotWidth = 132.0f;
    const float slotHeight = 34.0f;
    const float slotGap = 10.0f;
    const float labelWidth = 132.0f;
    const float padding = 18.0f;
    float slotsWidth = 0.0f;
    float startX = 0.0f;

    if (player == NULL)
    {
        return;
    }

    for (int candidate = PLAYER_RED; candidate <= PLAYER_BLACK; candidate++)
    {
        if (candidate != player->type)
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
        24.0f,
        labelWidth + slotsWidth + padding * 3.0f,
        56.0f};
    startX = bar.x + bar.width - padding - slotsWidth;

    DrawRectangleRounded((Rectangle){bar.x + 6.0f, bar.y + 8.0f, bar.width, bar.height}, 0.14f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(bar, 0.14f, 8, panelColor);
    DrawRectangleLinesEx(bar, 2.0f, borderColor);
    DrawUiText("Opponents", bar.x + padding, bar.y + 8.0f, 18, textColor);
    DrawUiText("Visible VP", bar.x + padding, bar.y + 30.0f, 13, mutedText);

    for (int i = 0; i < opponentCount; i++)
    {
        const int notificationCount = uiGetPlayerNotificationCount(opponents[i]);
        const Rectangle slot = {
            startX + i * (slotWidth + slotGap),
            bar.y + 10.0f,
            slotWidth,
            slotHeight};
        const char *name = PlayerName(opponents[i]);
        const char *score = TextFormat("%d", gameComputeVisibleVictoryPoints(map, opponents[i]));
        const int scoreWidth = MeasureUiText(score, 22);

        DrawRectangleRounded(slot, 0.24f, 8, Fade(PlayerColor(opponents[i]), 0.18f));
        DrawRectangleLinesEx(slot, 1.5f, Fade(PlayerColor(opponents[i]), 0.85f));
        DrawUiText(name, slot.x + 12.0f, slot.y + 8.0f, 16, textColor);
        DrawUiText(score, slot.x + slot.width - 14.0f - scoreWidth, slot.y + 5.0f, 22, PlayerColor(opponents[i]));

        for (int notificationIndex = 0; notificationIndex < notificationCount; notificationIndex++)
        {
            const char *notificationText = uiGetPlayerNotificationText(opponents[i], notificationIndex);
            const enum UiNotificationTone tone = uiGetPlayerNotificationTone(opponents[i], notificationIndex);
            const float dismissProgress = uiGetPlayerNotificationDismissProgress(opponents[i], notificationIndex);
            const int notificationFont = 16;
            const int notificationWidth = MeasureUiText(notificationText, notificationFont);
            const float chipWidth = (float)(notificationWidth + 20);
            const float chipHeight = 22.0f;
            const float chipX = slot.x + slot.width * 0.5f - chipWidth * 0.5f;
            const float textX = slot.x + slot.width * 0.5f - notificationWidth * 0.5f;
            Color notificationColor = mutedText;
            Color chipFill = (Color){244, 236, 217, 242};
            Color chipBorder = (Color){154, 132, 108, 255};
            float alpha = 1.0f;
            float y = bar.y + bar.height + 10.0f + notificationIndex * 28.0f;

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
                y -= dismissProgress * 22.0f;
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
                y + 3.0f,
                notificationFont,
                Fade(notificationColor, alpha));
        }
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

    if (alpha <= 0.01f)
    {
        return;
    }

    DrawRectangleRounded((Rectangle){panel.x + 6.0f, panel.y + 8.0f, panel.width, panel.height}, 0.10f, 8, Fade(BLACK, 0.12f * alpha));
    DrawRectangleRounded(panel, 0.10f, 8, Fade((Color){247, 240, 226, 255}, alpha));
    DrawRectangleLinesEx(panel, 2.0f, Fade((Color){118, 88, 56, 255}, alpha));
    DrawUiText("Buy Development Card?", panel.x + 18.0f, panel.y + 16.0f, 22, Fade((Color){54, 39, 29, 255}, alpha));
    DrawUiText("Cost: Wheat + Sheep + Stone", panel.x + 18.0f, panel.y + 50.0f, 16, Fade((Color){92, 70, 50, 255}, alpha));
    DrawUiText(TextFormat("%d cards left in deck", gameGetDevelopmentDeckCount(map)), panel.x + 18.0f, panel.y + 72.0f, 14, Fade((Color){92, 70, 50, 255}, alpha));

    DrawRectangleRounded(confirmButton, 0.18f, 8, Fade(canBuyDevelopment ? (Color){171, 82, 54, 255} : (Color){224, 216, 198, 255}, alpha));
    DrawRectangleLinesEx(confirmButton, 2.0f, Fade(canBuyDevelopment ? (Color){118, 88, 56, 255} : (Color){154, 132, 108, 255}, alpha));
    {
        const char *confirmLabel = "Confirm Buy";
        const int confirmWidth = MeasureUiText(confirmLabel, 18);
        DrawUiText(confirmLabel, confirmButton.x + confirmButton.width * 0.5f - confirmWidth * 0.5f, confirmButton.y + 8.0f, 18, Fade(canBuyDevelopment ? RAYWHITE : (Color){132, 112, 91, 255}, alpha));
    }

    DrawRectangleRounded(cancelButton, 0.18f, 8, Fade((Color){224, 216, 198, 255}, alpha));
    DrawRectangleLinesEx(cancelButton, 2.0f, Fade((Color){154, 132, 108, 255}, alpha));
    {
        const char *cancelLabel = "Cancel";
        const int cancelWidth = MeasureUiText(cancelLabel, 18);
        DrawUiText(cancelLabel, cancelButton.x + cancelButton.width * 0.5f - cancelWidth * 0.5f, cancelButton.y + 8.0f, 18, Fade((Color){92, 70, 50, 255}, alpha));
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
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){92, 70, 50, 255};

    if (alpha <= 0.01f || type < DEVELOPMENT_CARD_KNIGHT || type >= DEVELOPMENT_CARD_COUNT)
    {
        return;
    }

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.18f * alpha));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.10f, 8, Fade(BLACK, 0.14f * alpha));
    DrawRectangleRounded(panel, 0.10f, 8, Fade((Color){247, 240, 226, 252}, alpha));
    DrawRectangleLinesEx(panel, 2.0f, Fade((Color){118, 88, 56, 255}, alpha));
    DrawUiText(TextFormat("Use %s?", DevelopmentCardTitle(type)), panel.x + 22.0f, panel.y + 18.0f, 26, Fade(textColor, alpha));

    switch (type)
    {
    case DEVELOPMENT_CARD_KNIGHT:
        DrawUiText("Move the thief and steal 1 random card.", panel.x + 22.0f, panel.y + 60.0f, 18, Fade(mutedText, alpha));
        DrawUiText("You can still roll this turn afterward.", panel.x + 22.0f, panel.y + 88.0f, 16, Fade((Color){122, 100, 78, 255}, alpha));
        break;
    case DEVELOPMENT_CARD_ROAD_BUILDING:
        DrawUiText("Place up to 2 roads for free.", panel.x + 22.0f, panel.y + 60.0f, 18, Fade(mutedText, alpha));
        DrawUiText("The board will switch into road placement mode.", panel.x + 22.0f, panel.y + 88.0f, 16, Fade((Color){122, 100, 78, 255}, alpha));
        break;
    case DEVELOPMENT_CARD_YEAR_OF_PLENTY:
        DrawUiText("Choose 2 resources to take from the bank.", panel.x + 22.0f, panel.y + 60.0f, 18, Fade(mutedText, alpha));
        DrawUiText("First Resource", panel.x + 24.0f, panel.y + 108.0f, 15, Fade(textColor, alpha));
        DrawUiText("Second Resource", panel.x + 24.0f, panel.y + 160.0f, 15, Fade(textColor, alpha));
        for (int resource = 0; resource < 5; resource++)
        {
            const Rectangle firstButton = {rowX + resource * (buttonWidth + buttonGap), panel.y + 132.0f, buttonWidth, buttonHeight};
            const Rectangle secondButton = {rowX + resource * (buttonWidth + buttonGap), panel.y + 184.0f, buttonWidth, buttonHeight};
            const bool firstSelected = gDevelopmentPlayPrimaryResource == (enum ResourceType)resource;
            const bool secondSelected = gDevelopmentPlaySecondaryResource == (enum ResourceType)resource;
            const Color fill = (Color){236, 228, 208, 255};
            const Color selectedFill = (Color){182, 141, 97, 255};
            const Color border = (Color){154, 132, 108, 255};
            const Color selectedText = RAYWHITE;
            const char *label = ResourceName((enum ResourceType)resource);
            const int labelWidth = MeasureUiText(label, 15);

            DrawRectangleRounded(firstButton, 0.18f, 8, Fade(firstSelected ? selectedFill : fill, alpha));
            DrawRectangleLinesEx(firstButton, 1.6f, Fade(border, alpha));
            DrawUiText(label, firstButton.x + firstButton.width * 0.5f - labelWidth * 0.5f, firstButton.y + 9.0f, 15, Fade(firstSelected ? selectedText : textColor, alpha));

            DrawRectangleRounded(secondButton, 0.18f, 8, Fade(secondSelected ? selectedFill : fill, alpha));
            DrawRectangleLinesEx(secondButton, 1.6f, Fade(border, alpha));
            DrawUiText(label, secondButton.x + secondButton.width * 0.5f - labelWidth * 0.5f, secondButton.y + 9.0f, 15, Fade(secondSelected ? selectedText : textColor, alpha));
        }
        break;
    case DEVELOPMENT_CARD_MONOPOLY:
        DrawUiText("Choose 1 resource. Every opponent gives you that type.", panel.x + 22.0f, panel.y + 60.0f, 18, Fade(mutedText, alpha));
        DrawUiText("Target Resource", panel.x + 24.0f, panel.y + 116.0f, 15, Fade(textColor, alpha));
        for (int resource = 0; resource < 5; resource++)
        {
            const Rectangle button = {rowX + resource * (buttonWidth + buttonGap), panel.y + 144.0f, buttonWidth, buttonHeight};
            const bool selected = gDevelopmentPlayPrimaryResource == (enum ResourceType)resource;
            const char *label = ResourceName((enum ResourceType)resource);
            const int labelWidth = MeasureUiText(label, 15);

            DrawRectangleRounded(button, 0.18f, 8, Fade(selected ? (Color){182, 141, 97, 255} : (Color){236, 228, 208, 255}, alpha));
            DrawRectangleLinesEx(button, 1.6f, Fade((Color){154, 132, 108, 255}, alpha));
            DrawUiText(label, button.x + button.width * 0.5f - labelWidth * 0.5f, button.y + 9.0f, 15, Fade(selected ? RAYWHITE : textColor, alpha));
        }
        break;
    case DEVELOPMENT_CARD_VICTORY_POINT:
    default:
        DrawUiText("Victory point cards are counted automatically.", panel.x + 22.0f, panel.y + 60.0f, 18, Fade(mutedText, alpha));
        break;
    }

    DrawRectangleRounded(confirmButton, 0.18f, 8, Fade(canPlay ? (Color){171, 82, 54, 255} : (Color){224, 216, 198, 255}, alpha));
    DrawRectangleLinesEx(confirmButton, 2.0f, Fade(canPlay ? (Color){118, 88, 56, 255} : (Color){154, 132, 108, 255}, alpha));
    {
        const char *confirmLabel = "Confirm Use";
        if (type == DEVELOPMENT_CARD_YEAR_OF_PLENTY)
        {
            confirmLabel = "Gain Resources";
        }
        else if (type == DEVELOPMENT_CARD_MONOPOLY)
        {
            confirmLabel = "Claim Resource";
        }
        else if (type == DEVELOPMENT_CARD_ROAD_BUILDING)
        {
            confirmLabel = "Place Roads";
        }
        const int confirmWidth = MeasureUiText(confirmLabel, 18);
        DrawUiText(confirmLabel, confirmButton.x + confirmButton.width * 0.5f - confirmWidth * 0.5f, confirmButton.y + 8.0f, 18, Fade(canPlay ? RAYWHITE : (Color){132, 112, 91, 255}, alpha));
    }

    DrawRectangleRounded(cancelButton, 0.18f, 8, Fade((Color){224, 216, 198, 255}, alpha));
    DrawRectangleLinesEx(cancelButton, 2.0f, Fade((Color){154, 132, 108, 255}, alpha));
    {
        const char *cancelLabel = "Cancel";
        const int cancelWidth = MeasureUiText(cancelLabel, 18);
        DrawUiText(cancelLabel, cancelButton.x + cancelButton.width * 0.5f - cancelWidth * 0.5f, cancelButton.y + 8.0f, 18, Fade((Color){92, 70, 50, 255}, alpha));
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
    const Color panelColor = (Color){249, 233, 230, 248};
    const Color borderColor = (Color){176, 63, 52, 255};
    const Color textColor = (Color){176, 41, 33, 255};

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

    DrawRectangleRounded((Rectangle){panel.x + 7.0f, panel.y + 9.0f, panel.width, panel.height}, 0.18f, 8, Fade(BLACK, 0.11f * alpha));
    DrawRectangleRounded(panel, 0.18f, 8, Fade(panelColor, alpha));
    DrawRectangleLinesEx(panel, 2.2f, Fade(borderColor, alpha));

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
    const float alpha = uiGetCenteredStatusAlpha();
    const float yOffset = uiGetCenteredStatusVerticalOffset();
    const int fontSize = 18;
    const float lineStep = 22.0f;
    float widestLine = 0.0f;
    float panelWidth = 320.0f;
    float panelHeight = 72.0f;
    Rectangle panel = {0};
    Color panelColor = (Color){241, 235, 219, 248};
    Color borderColor = (Color){118, 88, 56, 255};
    Color textColor = (Color){54, 39, 29, 255};

    if (message == NULL || message[0] == '\0' || alpha <= 0.01f)
    {
        return;
    }

    if (tone == UI_NOTIFICATION_POSITIVE)
    {
        panelColor = (Color){230, 242, 228, 248};
        borderColor = (Color){78, 136, 76, 255};
        textColor = (Color){47, 92, 48, 255};
    }
    else if (tone == UI_NOTIFICATION_NEGATIVE)
    {
        panelColor = (Color){247, 229, 225, 248};
        borderColor = (Color){176, 63, 52, 255};
        textColor = (Color){140, 43, 34, 255};
    }
    else if (tone == UI_NOTIFICATION_VICTORY)
    {
        panelColor = (Color){247, 239, 213, 248};
        borderColor = (Color){184, 140, 43, 255};
        textColor = (Color){120, 84, 19, 255};
    }

    lineCount = BuildWrappedUiLines(message, fontSize, 338, lines, 4);
    if (lineCount <= 0)
    {
        return;
    }

    for (int i = 0; i < lineCount; i++)
    {
        const float lineWidth = (float)MeasureUiText(lines[i], lineCount == 1 ? 20 : fontSize);
        if (lineWidth > widestLine)
        {
            widestLine = lineWidth;
        }
    }

    panelWidth = widestLine + 42.0f;
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

    DrawRectangleRounded((Rectangle){panel.x + 7.0f, panel.y + 9.0f, panel.width, panel.height}, 0.18f, 8, Fade(BLACK, 0.11f * alpha));
    DrawRectangleRounded(panel, 0.18f, 8, Fade(panelColor, alpha));
    DrawRectangleLinesEx(panel, 2.0f, Fade(borderColor, alpha));
    for (int i = 0; i < lineCount; i++)
    {
        const int lineFontSize = lineCount == 1 ? 20 : fontSize;
        const int width = MeasureUiText(lines[i], lineFontSize);
        const float textY = panel.y + panel.height * 0.5f - (lineCount - 1) * lineStep * 0.5f - lineFontSize * 0.5f + i * lineStep;
        DrawUiText(lines[i], panel.x + panel.width * 0.5f - width * 0.5f, textY, lineFontSize, Fade(textColor, alpha));
    }
}

void DrawTurnPanel(const struct Map *map)
{
    const Rectangle panel = GetTurnPanelBounds();
    const Rectangle rollDiceButton = GetRollDiceButtonBounds();
    const Rectangle endTurnButton = GetEndTurnButtonBounds();
    const Color panelColor = (Color){244, 236, 217, 245};
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color actionColor = (Color){171, 82, 54, 255};
    const Color mutedButton = (Color){214, 202, 181, 255};
    const Color textColor = (Color){54, 39, 29, 255};
    const bool canEndTurn = gameCanEndTurn(map);
    const bool diceLocked = map->rolledThisTurn || uiIsDiceRolling();
    const Rectangle dieA = {panel.x + 20.0f, panel.y + 76.0f, 54.0f, 54.0f};
    const Rectangle dieB = {panel.x + 84.0f, panel.y + 76.0f, 54.0f, 54.0f};
    const int shownTotal = uiIsDiceRolling() ? (uiGetDisplayedDieA() + uiGetDisplayedDieB()) : map->lastDiceRoll;

    DrawRectangleRounded((Rectangle){panel.x + 6.0f, panel.y + 8.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText("Turn", panel.x + 16.0f, panel.y + 14.0f, 24, textColor);
    if (gameHasWinner(map))
    {
        const enum PlayerType winner = gameGetWinner(map);
        const int winnerPoints = gameComputeVictoryPoints(map, winner);
        char headline[64];
        char subheadline[64];
        BuildVictoryHeadline(map, winner, headline, sizeof(headline));
        BuildVictorySubheadline(map, winner, subheadline, sizeof(subheadline));
        DrawUiText("Game Over", panel.x + 16.0f, panel.y + 52.0f, 24, (Color){171, 82, 54, 255});
        DrawUiText(headline, panel.x + 16.0f, panel.y + 88.0f, 28, winner == LocalHumanPlayer(map) ? (Color){54, 130, 72, 255} : PlayerColor(winner));
        DrawUiText(subheadline, panel.x + 16.0f, panel.y + 124.0f, 18, PlayerColor(winner));
        DrawUiText(TextFormat("%d victory points", winnerPoints), panel.x + 16.0f, panel.y + 150.0f, 18, (Color){92, 70, 50, 255});
        DrawUiText("Board is locked", panel.x + 16.0f, panel.y + 174.0f, 20, textColor);
        DrawUiText("Use the overlay buttons", panel.x + 16.0f, panel.y + 202.0f, 18, (Color){92, 70, 50, 255});
        DrawUiText("to restart or return", panel.x + 16.0f, panel.y + 224.0f, 18, (Color){92, 70, 50, 255});
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        return;
    }

    if (map->phase == GAME_PHASE_SETUP)
    {
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        DrawUiText("Setup Phase", panel.x + 16.0f, panel.y + 48.0f, 18, (Color){92, 70, 50, 255});
        DrawUiText(map->setupNeedsRoad ? "Place 1 road" : "Place 1 settlement", panel.x + 16.0f, panel.y + 76.0f, 24, textColor);
        DrawUiText(TextFormat("Round %d / 8", map->setupStep + 1), panel.x + 16.0f, panel.y + 110.0f, 18, (Color){92, 70, 50, 255});
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
        DrawUiText("Dice", panel.x + 16.0f, panel.y + 48.0f, 18, (Color){92, 70, 50, 255});
        DrawUiText("7", panel.x + 178.0f, panel.y + 72.0f, 30, textColor);
        DrawUiText("Discard Cards", panel.x + 16.0f, panel.y + 148.0f, 24, (Color){171, 82, 54, 255});
        if (aiDiscard)
        {
            DrawUiText("Waiting for AI discard", panel.x + 16.0f, panel.y + 178.0f, 18, (Color){92, 70, 50, 255});
        }
        else if (!revealDiscard)
        {
            DrawUiText(TextFormat("%s is up next", PlayerName(discardPlayer)), panel.x + 16.0f, panel.y + 178.0f, 18, (Color){92, 70, 50, 255});
            DrawUiText("Pass the screen to continue", panel.x + 16.0f, panel.y + 200.0f, 18, (Color){92, 70, 50, 255});
        }
        else
        {
            DrawUiText(TextFormat("Choose %d cards to discard", discardAmount), panel.x + 16.0f, panel.y + 178.0f, 18, (Color){92, 70, 50, 255});
        }
        return;
    }

    if (gameNeedsThiefPlacement(map))
    {
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        DrawUiText("Dice", panel.x + 16.0f, panel.y + 48.0f, 18, (Color){92, 70, 50, 255});
        DrawUiText("7", panel.x + 178.0f, panel.y + 72.0f, 30, textColor);
        DrawUiText("Move Thief", panel.x + 16.0f, panel.y + 148.0f, 24, (Color){171, 82, 54, 255});
        DrawUiText("Select a land tile", panel.x + 16.0f, panel.y + 178.0f, 18, (Color){92, 70, 50, 255});
        DrawUiText("before ending turn", panel.x + 16.0f, panel.y + 200.0f, 18, (Color){92, 70, 50, 255});
        DrawTurnActionButton(rollDiceButton, "Roll Dice (Enter)", 22, mutedButton, borderColor, (Color){108, 86, 67, 255}, actionColor, &gRollDiceButtonAnimation);
        DrawTurnActionButton(endTurnButton, "End Turn", 22, (Color){228, 220, 202, 255}, (Color){154, 132, 108, 255}, (Color){132, 112, 91, 255}, mutedButton, &gEndTurnButtonAnimation);
        return;
    }

    if (gameNeedsThiefVictimSelection(map))
    {
        UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, false);
        UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, false);
        DrawUiText("Dice", panel.x + 16.0f, panel.y + 48.0f, 18, (Color){92, 70, 50, 255});
        DrawUiText("7", panel.x + 178.0f, panel.y + 72.0f, 30, textColor);
        DrawUiText("Steal Resource", panel.x + 16.0f, panel.y + 148.0f, 24, (Color){171, 82, 54, 255});
        if (map->players[map->currentPlayer].controlMode == PLAYER_CONTROL_AI)
        {
            DrawUiText(TextFormat("%s is choosing", PlayerName(map->currentPlayer)), panel.x + 16.0f, panel.y + 178.0f, 18, (Color){92, 70, 50, 255});
            DrawUiText("an enemy", panel.x + 16.0f, panel.y + 200.0f, 18, (Color){92, 70, 50, 255});
        }
        else
        {
            DrawUiText("Choose one adjacent", panel.x + 16.0f, panel.y + 178.0f, 18, (Color){92, 70, 50, 255});
            DrawUiText("player to rob", panel.x + 16.0f, panel.y + 200.0f, 18, (Color){92, 70, 50, 255});
        }
        return;
    }

    DrawUiText("Dice", panel.x + 16.0f, panel.y + 48.0f, 18, (Color){92, 70, 50, 255});
    DrawDie(dieA, uiGetDisplayedDieA(), uiIsDiceRolling() ? -8.0f : -2.0f, 1.0f);
    DrawDie(dieB, uiGetDisplayedDieB(), uiIsDiceRolling() ? 7.0f : 2.0f, 1.0f);
    DrawUiText(shownTotal > 0 ? TextFormat("%d", shownTotal) : "-", panel.x + 178.0f, panel.y + 72.0f, 30, textColor);
    if (uiIsDiceRolling())
    {
        DrawUiText("Rolling...", panel.x + 148.0f, panel.y + 106.0f, 16, (Color){92, 70, 50, 255});
    }

    UpdateTurnButtonAnimation(&gRollDiceButtonAnimation, rollDiceButton, !diceLocked);
    UpdateTurnButtonAnimation(&gEndTurnButtonAnimation, endTurnButton, canEndTurn);
    DrawTurnActionButton(rollDiceButton, "Roll Dice (Enter)", 22, diceLocked ? mutedButton : actionColor, diceLocked ? borderColor : ColorBrightness(actionColor, -0.18f), diceLocked ? (Color){108, 86, 67, 255} : RAYWHITE, actionColor, &gRollDiceButtonAnimation);
    DrawTurnActionButton(endTurnButton, "End Turn", 22, canEndTurn ? mutedButton : (Color){228, 220, 202, 255}, canEndTurn ? borderColor : (Color){154, 132, 108, 255}, canEndTurn ? textColor : (Color){132, 112, 91, 255}, (Color){182, 141, 97, 255}, &gEndTurnButtonAnimation);
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
    const Color borderColor = (Color){118, 88, 56, 255};
    const Color panelColor = (Color){244, 236, 217, 252};
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){92, 70, 50, 255};
    char headline[64];
    char winnerLabel[64];
    char subtitle[48];
    BuildVictoryHeadline(map, winner, headline, sizeof(headline));
    BuildVictorySubheadline(map, winner, winnerLabel, sizeof(winnerLabel));
    snprintf(subtitle, sizeof(subtitle), "%d victory points", winnerPoints);
    const int headlineWidth = MeasureUiText(headline, 34);
    const int winnerWidth = MeasureUiText(winnerLabel, 24);
    const int subtitleWidth = MeasureUiText(subtitle, 18);
    const char *hint = "Choose what to do next.";
    const int hintWidth = MeasureUiText(hint, 18);
    const char *restartLabel = "Restart Game";
    const char *menuLabel = "Back to Menu";
    const int restartWidth = MeasureUiText(restartLabel, 19);
    const int menuWidth = MeasureUiText(menuLabel, 19);

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.36f));
    DrawRectangleRounded((Rectangle){panel.x + 8.0f, panel.y + 10.0f, panel.width, panel.height}, 0.08f, 8, Fade(BLACK, 0.14f));
    DrawRectangleRounded(panel, 0.08f, 8, panelColor);
    DrawRectangleLinesEx(panel, 2.0f, borderColor);

    DrawUiText("Game Over", panel.x + panel.width * 0.5f - MeasureUiText("Game Over", 20) * 0.5f, panel.y + 20.0f, 20, mutedText);
    DrawUiText(headline, panel.x + panel.width * 0.5f - headlineWidth * 0.5f, panel.y + 54.0f, 34, winner == LocalHumanPlayer(map) ? (Color){54, 130, 72, 255} : (Color){171, 82, 54, 255});
    DrawUiText(winnerLabel, panel.x + panel.width * 0.5f - winnerWidth * 0.5f, panel.y + 98.0f, 24, PlayerColor(winner));
    DrawUiText(subtitle, panel.x + panel.width * 0.5f - subtitleWidth * 0.5f, panel.y + 130.0f, 18, textColor);
    DrawUiText(hint, panel.x + panel.width * 0.5f - hintWidth * 0.5f, panel.y + 154.0f, 18, mutedText);

    DrawRectangleRounded((Rectangle){restartButton.x + 4.0f, restartButton.y + 6.0f, restartButton.width, restartButton.height}, 0.22f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(restartButton, 0.22f, 8, (Color){171, 82, 54, 255});
    DrawRectangleLinesEx(restartButton, 2.0f, (Color){120, 58, 37, 255});
    DrawUiText(restartLabel, restartButton.x + restartButton.width * 0.5f - restartWidth * 0.5f, restartButton.y + 11.0f, 19, RAYWHITE);

    DrawRectangleRounded((Rectangle){menuButton.x + 4.0f, menuButton.y + 6.0f, menuButton.width, menuButton.height}, 0.22f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(menuButton, 0.22f, 8, (Color){222, 212, 193, 255});
    DrawRectangleLinesEx(menuButton, 2.0f, borderColor);
    DrawUiText(menuLabel, menuButton.x + menuButton.width * 0.5f - menuWidth * 0.5f, menuButton.y + 11.0f, 19, textColor);
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
}

static void DrawTurnActionButton(Rectangle bounds, const char *label, int fontSize, Color fillColor, Color borderColor, Color textColor, Color glowColor, const struct ButtonAnimationState *state)
{
    const float hoverAmount = state->hoverAmount;
    const float pressAmount = state->pressAmount;
    const float scale = 1.0f + hoverAmount * 0.028f - pressAmount * 0.015f;
    const float lift = hoverAmount * 3.0f - pressAmount * 1.6f;
    const Rectangle body = ScaleRectangleFromCenter((Rectangle){bounds.x, bounds.y - lift, bounds.width, bounds.height}, scale);
    const Rectangle shadow = {body.x + 4.0f, body.y + 6.0f + hoverAmount * 1.4f, body.width, body.height};
    const Rectangle glow = {body.x - 3.0f, body.y - 3.0f, body.width + 6.0f, body.height + 6.0f};
    const int labelWidth = MeasureUiText(label, fontSize);
    const float labelX = body.x + body.width * 0.5f - labelWidth * 0.5f;
    const float labelY = body.y + body.height * 0.5f - (float)fontSize * 0.48f + pressAmount * 1.0f;

    DrawRectangleRounded(shadow, 0.22f, 8, Fade(BLACK, 0.10f + hoverAmount * 0.08f - pressAmount * 0.03f));
    if (hoverAmount > 0.01f || pressAmount > 0.01f) {
        DrawRectangleRounded(glow, 0.24f, 8, Fade(glowColor, hoverAmount * 0.10f + pressAmount * 0.16f));
    }
    DrawRectangleRounded(body, 0.18f, 8, ColorBrightness(fillColor, hoverAmount * 0.10f - pressAmount * 0.08f));
    DrawRectangleLinesEx(body, 2.0f, ColorBrightness(borderColor, hoverAmount * 0.10f));
    DrawUiText(label, labelX, labelY, fontSize, textColor);
}

Rectangle GetPlayerPanelBounds(void)
{
    const float panelWidth = 250.0f;
    const float panelHeight = 238.0f;
    return (Rectangle){(float)GetScreenWidth() - panelWidth - 24.0f, 24.0f, panelWidth, panelHeight};
}

Rectangle GetTurnPanelBounds(void)
{
    const float panelWidth = 250.0f;
    const float panelHeight = 280.0f;
    return (Rectangle){(float)GetScreenWidth() - panelWidth - 24.0f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f - 42.0f, panelWidth, panelHeight};
}

Rectangle GetTradeButtonBounds(void)
{
    const float panelWidth = 250.0f;
    const float panelHeight = 44.0f;
    const Rectangle turnPanel = GetTurnPanelBounds();
    return (Rectangle){turnPanel.x, turnPanel.y + turnPanel.height + 18.0f, panelWidth, panelHeight};
}

Rectangle GetPlayerTradeButtonBounds(void)
{
    const Rectangle waterTrade = GetTradeButtonBounds();
    return (Rectangle){waterTrade.x, waterTrade.y + waterTrade.height + 12.0f, waterTrade.width, waterTrade.height};
}

Rectangle GetSettingsButtonBounds(void)
{
    return (Rectangle){24.0f, 24.0f, 156.0f, 42.0f};
}

Rectangle GetTradeModalBounds(void)
{
    const float panelWidth = 460.0f;
    const float panelHeight = 380.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetSettingsModalBounds(void)
{
    const float panelWidth = 320.0f;
    const float panelHeight = 468.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetPlayerTradeModalBounds(void)
{
    const float panelWidth = 440.0f;
    const float panelHeight = 430.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetDiscardModalBounds(void)
{
    const float panelWidth = 390.0f;
    const float panelHeight = 360.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetThiefVictimModalBounds(void)
{
    const float panelWidth = 430.0f;
    const float panelHeight = 190.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetBuildPanelBounds(void)
{
    const float closedWidth = 220.0f;
    const float openWidth = 392.0f;
    const float closedHeight = 54.0f;
    const float openHeight = 182.0f;
    const float panelWidth = closedWidth + (openWidth - closedWidth) * uiGetBuildPanelOpenAmount();
    const float panelHeight = closedHeight + (openHeight - closedHeight) * uiGetBuildPanelOpenAmount();
    return (Rectangle){(float)GetScreenWidth() - panelWidth - 24.0f, (float)GetScreenHeight() - panelHeight - 24.0f, panelWidth, panelHeight};
}

Rectangle GetBuildPanelDevelopmentCardBounds(void)
{
    const Rectangle panel = GetBuildPanelBounds();
    return (Rectangle){panel.x + 204.0f, panel.y + 118.0f, 170.0f, 56.0f};
}

Rectangle GetDevelopmentPurchaseOverlayBounds(void)
{
    const Rectangle buildPanel = GetBuildPanelBounds();
    return (Rectangle){buildPanel.x + 10.0f, buildPanel.y + 10.0f, buildPanel.width - 20.0f, buildPanel.height - 20.0f};
}

Rectangle GetDevelopmentPurchaseConfirmButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPurchaseOverlayBounds();
    return (Rectangle){panel.x + 18.0f, panel.y + panel.height - 48.0f, panel.width - 132.0f, 36.0f};
}

Rectangle GetDevelopmentPurchaseCancelButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPurchaseOverlayBounds();
    return (Rectangle){panel.x + panel.width - 100.0f, panel.y + panel.height - 48.0f, 82.0f, 36.0f};
}

Rectangle GetDevelopmentPlayOverlayBounds(void)
{
    const float panelWidth = 476.0f;
    const float panelHeight = 320.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f - 24.0f,
        panelWidth,
        panelHeight};
}

Rectangle GetDevelopmentPlayConfirmButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPlayOverlayBounds();
    return (Rectangle){panel.x + 24.0f, panel.y + panel.height - 54.0f, panel.width - 164.0f, 38.0f};
}

Rectangle GetDevelopmentPlayCancelButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPlayOverlayBounds();
    return (Rectangle){panel.x + panel.width - 116.0f, panel.y + panel.height - 54.0f, 92.0f, 38.0f};
}

Rectangle GetRollDiceButtonBounds(void)
{
    const Rectangle panel = GetTurnPanelBounds();
    return (Rectangle){panel.x + 16.0f, panel.y + 176.0f, panel.width - 32.0f, 42.0f};
}

Rectangle GetEndTurnButtonBounds(void)
{
    const Rectangle panel = GetTurnPanelBounds();
    return (Rectangle){panel.x + 16.0f, panel.y + 226.0f, panel.width - 32.0f, 42.0f};
}

Rectangle GetVictoryOverlayBounds(void)
{
    const float panelWidth = 500.0f;
    const float panelHeight = 262.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

Rectangle GetVictoryOverlayRestartButtonBounds(void)
{
    const Rectangle panel = GetVictoryOverlayBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + panel.height - 62.0f, 196.0f, 42.0f};
}

Rectangle GetVictoryOverlayMenuButtonBounds(void)
{
    const Rectangle panel = GetVictoryOverlayBounds();
    return (Rectangle){panel.x + panel.width - 230.0f, panel.y + panel.height - 62.0f, 196.0f, 42.0f};
}

static void DrawDie(Rectangle bounds, int value, float tilt, float alpha)
{
    const Color body = Fade((Color){246, 240, 225, 255}, alpha);
    const Color edge = Fade((Color){118, 88, 56, 255}, alpha);
    const Color pip = Fade((Color){54, 39, 29, 255}, alpha);
    const Vector2 center = {bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f};
    const float dx = bounds.width * 0.22f;
    const float dy = bounds.height * 0.22f;
    const float r = bounds.width * 0.075f;
    const Vector2 pips[7] = {
        center,
        {center.x - dx, center.y - dy},
        {center.x + dx, center.y + dy},
        {center.x + dx, center.y - dy},
        {center.x - dx, center.y + dy},
        {center.x - dx, center.y},
        {center.x + dx, center.y}};

    DrawRectanglePro((Rectangle){bounds.x + 3.0f, bounds.y + 4.0f, bounds.width, bounds.height}, (Vector2){bounds.width * 0.5f, bounds.height * 0.5f}, tilt, Fade(BLACK, 0.10f * alpha));
    DrawRectangleRounded((Rectangle){bounds.x, bounds.y, bounds.width, bounds.height}, 0.18f, 8, body);
    DrawRectangleRoundedLinesEx(bounds, 0.18f, 8, 2.0f, edge);

    switch (value)
    {
    case 1:
        DrawCircleV(pips[0], r, pip);
        break;
    case 2:
        DrawCircleV(pips[1], r, pip);
        DrawCircleV(pips[2], r, pip);
        break;
    case 3:
        DrawCircleV(pips[1], r, pip);
        DrawCircleV(pips[0], r, pip);
        DrawCircleV(pips[2], r, pip);
        break;
    case 4:
        DrawCircleV(pips[1], r, pip);
        DrawCircleV(pips[2], r, pip);
        DrawCircleV(pips[3], r, pip);
        DrawCircleV(pips[4], r, pip);
        break;
    case 5:
        DrawCircleV(pips[1], r, pip);
        DrawCircleV(pips[2], r, pip);
        DrawCircleV(pips[3], r, pip);
        DrawCircleV(pips[4], r, pip);
        DrawCircleV(pips[0], r, pip);
        break;
    case 6:
        DrawCircleV(pips[1], r, pip);
        DrawCircleV(pips[4], r, pip);
        DrawCircleV(pips[5], r, pip);
        DrawCircleV(pips[6], r, pip);
        DrawCircleV(pips[3], r, pip);
        DrawCircleV(pips[2], r, pip);
        break;
    default:
        break;
    }
}

static const struct PlayerState *CurrentPlayerState(const struct Map *map)
{
    enum PlayerType displayedPlayer = PLAYER_NONE;

    if (map == NULL)
    {
        return NULL;
    }

    if (IsPrivateInfoPinnedToLocalHuman(map))
    {
        displayedPlayer = LocalHumanPlayer(map);
    }
    else
    {
        displayedPlayer = map->currentPlayer;
    }

    if (displayedPlayer < PLAYER_RED || displayedPlayer > PLAYER_BLACK)
    {
        return NULL;
    }

    return &map->players[displayedPlayer];
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

static bool IsPrivateInfoPinnedToLocalHuman(const struct Map *map)
{
    const enum PlayerType localHuman = LocalHumanPlayer(map);
    return localHuman != PLAYER_NONE && map != NULL && map->currentPlayer != localHuman;
}

static const char *PlayerName(enum PlayerType player)
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
    default:
        return "None";
    }
}

static const char *ResourceName(enum ResourceType resource)
{
    switch (resource)
    {
    case RESOURCE_WOOD:
        return "Wood";
    case RESOURCE_WHEAT:
        return "Wheat";
    case RESOURCE_CLAY:
        return "Clay";
    case RESOURCE_SHEEP:
        return "Sheep";
    case RESOURCE_STONE:
        return "Stone";
    default:
        return "";
    }
}

static int BuildDevelopmentHandLayout(const struct Map *map, struct DevelopmentHandCard cards[DEVELOPMENT_CARD_COUNT])
{
    const struct PlayerState *player = CurrentPlayerState(map);
    int cardCount = 0;

    if (player == NULL || cards == NULL)
    {
        return 0;
    }

    for (int type = 0; type < DEVELOPMENT_CARD_COUNT; type++)
    {
        const int owned = gameGetDevelopmentCardCount(map, player->type, (enum DevelopmentCardType)type);
        if (owned > 0 && cardCount < DEVELOPMENT_CARD_COUNT)
        {
            cards[cardCount].type = (enum DevelopmentCardType)type;
            cards[cardCount].count = owned;
            cards[cardCount].playable = gameCanPlayDevelopmentCard(map, (enum DevelopmentCardType)type);
            cardCount++;
        }
    }

    if (cardCount <= 0)
    {
        return 0;
    }

    const float safeLeft = 56.0f;
    const float safeRight = (float)GetScreenWidth() - 56.0f;
    const float availableWidth = safeRight - safeLeft;
    if (availableWidth <= 120.0f)
    {
        return 0;
    }

    float cardWidth = cardCount >= 4 ? 146.0f : 156.0f;
    const float cardHeight = 202.0f;
    if (cardWidth > availableWidth - 12.0f)
    {
        cardWidth = availableWidth - 12.0f;
    }

    float step = cardCount > 1 ? (availableWidth - cardWidth) / (float)(cardCount - 1) : 0.0f;
    if (step < 70.0f)
    {
        step = 70.0f;
    }
    if (step > 82.0f)
    {
        step = 82.0f;
    }
    if (cardCount > 1 && cardWidth + step * (float)(cardCount - 1) > availableWidth)
    {
        step = (availableWidth - cardWidth) / (float)(cardCount - 1);
    }

    const float totalWidth = cardWidth + step * (float)(cardCount - 1);
    const float centerX = (float)GetScreenWidth() * 0.5f;
    const float startX = centerX - totalWidth * 0.5f + cardWidth * 0.5f;
    const float baseBottom = (float)GetScreenHeight() + 82.0f;

    for (int i = 0; i < cardCount; i++)
    {
        const float mid = (float)(cardCount - 1) * 0.5f;
        const float offset = (float)i - mid;
        const float normalized = mid > 0.0f ? offset / mid : 0.0f;
        const float arcLift = (1.0f - fabsf(normalized)) * 8.0f + (cardCount == 1 ? 8.0f : 0.0f);

        cards[i].rotation = 0.0f;
        cards[i].bounds = (Rectangle){
            startX + offset * step - cardWidth * 0.5f,
            baseBottom - cardHeight - arcLift,
            cardWidth,
            cardHeight};
    }

    return cardCount;
}

static Rectangle DevelopmentHandHitBounds(Rectangle bounds)
{
    return (Rectangle){
        bounds.x,
        bounds.y - 34.0f,
        bounds.width,
        bounds.height + 34.0f};
}

static void DrawAwardCard(Rectangle bounds, const char *title, const char *subtitle, const char *detail, enum PlayerType owner, Color accent)
{
    const Color parchment = (Color){245, 236, 217, 248};
    const Color border = (Color){118, 88, 56, 255};
    const Color textColor = (Color){54, 39, 29, 255};
    const Color mutedText = (Color){88, 68, 50, 255};
    const Color detailText = (Color){48, 35, 24, 255};
    const Rectangle ribbon = {bounds.x + 14.0f, bounds.y + 14.0f, bounds.width - 28.0f, 26.0f};
    const Rectangle detailPanel = {bounds.x + 16.0f, bounds.y + 102.0f, bounds.width - 32.0f, 30.0f};
    const Rectangle ownerChip = {bounds.x + 16.0f, bounds.y + bounds.height - 34.0f, bounds.width - 32.0f, 22.0f};
    const bool claimed = owner >= PLAYER_RED && owner <= PLAYER_BLACK;
    const Color chipFill = claimed ? Fade(PlayerColor(owner), 0.92f) : (Color){221, 212, 194, 255};
    const Color chipText = claimed ? RAYWHITE : mutedText;
    const char *ownerLabel = claimed ? PlayerName(owner) : "Unclaimed";
    const int titleWidth = MeasureUiText(title, 22);
    const int subtitleWidth = MeasureUiText(subtitle, 13);
    const int detailWidth = MeasureUiText(detail, 15);
    const int ownerWidth = MeasureUiText(ownerLabel, 14);

    DrawRectangleRounded((Rectangle){bounds.x + 6.0f, bounds.y + 8.0f, bounds.width, bounds.height}, 0.10f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(bounds, 0.10f, 8, parchment);
    DrawRectangleLinesEx(bounds, 2.0f, border);
    DrawRectangleRounded(ribbon, 0.16f, 8, Fade(accent, 0.96f));
    DrawRectangleLinesEx(ribbon, 1.4f, Fade(border, 0.80f));
    DrawRectangleRounded(detailPanel, 0.28f, 8, (Color){249, 243, 232, 255});
    DrawRectangleLinesEx(detailPanel, 1.5f, Fade(border, 0.54f));

    DrawUiText(title, bounds.x + bounds.width * 0.5f - titleWidth * 0.5f, bounds.y + 52.0f, 22, textColor);
    DrawUiText(subtitle, bounds.x + bounds.width * 0.5f - subtitleWidth * 0.5f, bounds.y + 84.0f, 13, mutedText);
    DrawUiText(detail, detailPanel.x + detailPanel.width * 0.5f - detailWidth * 0.5f, detailPanel.y + 7.0f, 15, Fade((Color){255, 255, 255, 255}, 0.32f));
    DrawUiText(detail, detailPanel.x + detailPanel.width * 0.5f - detailWidth * 0.5f - 0.6f, detailPanel.y + 6.0f, 15, detailText);
    DrawUiText(detail, detailPanel.x + detailPanel.width * 0.5f - detailWidth * 0.5f, detailPanel.y + 6.0f, 15, detailText);

    DrawRectangleRounded(ownerChip, 0.40f, 8, chipFill);
    DrawRectangleLinesEx(ownerChip, 1.4f, claimed ? Fade(border, 0.64f) : (Color){154, 132, 108, 255});
    DrawUiText(ownerLabel, ownerChip.x + ownerChip.width * 0.5f - ownerWidth * 0.5f, ownerChip.y + 3.0f, 14, chipText);
}

static const char *DevelopmentCardTitle(enum DevelopmentCardType type)
{
    switch (type)
    {
    case DEVELOPMENT_CARD_KNIGHT:
        return "Knight";
    case DEVELOPMENT_CARD_VICTORY_POINT:
        return "Victory Point";
    case DEVELOPMENT_CARD_ROAD_BUILDING:
        return "Road Building";
    case DEVELOPMENT_CARD_YEAR_OF_PLENTY:
        return "Year of Plenty";
    case DEVELOPMENT_CARD_MONOPOLY:
        return "Monopoly";
    default:
        return "Development";
    }
}

static const char *DevelopmentCardDescription(enum DevelopmentCardType type)
{
    switch (type)
    {
    case DEVELOPMENT_CARD_KNIGHT:
        return "Move the thief\nand steal\n1 random card.";
    case DEVELOPMENT_CARD_VICTORY_POINT:
        return "Worth 1 hidden\nvictory point\nat the end.";
    case DEVELOPMENT_CARD_ROAD_BUILDING:
        return "Place 2 roads\nfor free.";
    case DEVELOPMENT_CARD_YEAR_OF_PLENTY:
        return "Take any\n2 resources\nfrom the bank.";
    case DEVELOPMENT_CARD_MONOPOLY:
        return "Choose 1 resource.\nAll opponents give\nyou that type.";
    default:
        return "";
    }
}

static Color DevelopmentCardAccent(enum DevelopmentCardType type)
{
    switch (type)
    {
    case DEVELOPMENT_CARD_KNIGHT:
        return (Color){92, 108, 126, 255};
    case DEVELOPMENT_CARD_VICTORY_POINT:
        return (Color){188, 145, 53, 255};
    case DEVELOPMENT_CARD_ROAD_BUILDING:
        return (Color){156, 86, 58, 255};
    case DEVELOPMENT_CARD_YEAR_OF_PLENTY:
        return (Color){108, 142, 74, 255};
    case DEVELOPMENT_CARD_MONOPOLY:
        return (Color){57, 116, 122, 255};
    default:
        return (Color){118, 88, 56, 255};
    }
}

static float Clamp01(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }
    if (value > 1.0f)
    {
        return 1.0f;
    }
    return value;
}

static float LerpFloat(float a, float b, float t)
{
    return a + (b - a) * t;
}

static Rectangle LerpRectangle(Rectangle a, Rectangle b, float t)
{
    return (Rectangle){
        LerpFloat(a.x, b.x, t),
        LerpFloat(a.y, b.y, t),
        LerpFloat(a.width, b.width, t),
        LerpFloat(a.height, b.height, t)};
}

static float EaseOutCubic(float t)
{
    t = Clamp01(t);
    return 1.0f - powf(1.0f - t, 3.0f);
}

static float EaseInOutCubic(float t)
{
    t = Clamp01(t);
    if (t < 0.5f)
    {
        return 4.0f * t * t * t;
    }
    return 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

static void DrawDevelopmentCardVisual(Font font, Rectangle card, float rotation, enum DevelopmentCardType type, int count, float alpha, bool emphasized)
{
    const Rectangle shadow = {
        card.x + 7.0f,
        card.y + 10.0f,
        card.width,
        card.height
    };
    const Rectangle border = {
        card.x - 2.0f,
        card.y - 2.0f,
        card.width + 4.0f,
        card.height + 4.0f
    };
    const Rectangle ribbon = {
        card.x + 12.0f,
        card.y + 14.0f,
        card.width - 24.0f,
        34.0f
    };
    const Rectangle textPanel = {
        card.x + 12.0f,
        card.y + 58.0f,
        card.width - 24.0f,
        card.height - 78.0f
    };
    const Rectangle badge = {
        card.x + card.width - 48.0f,
        card.y + 14.0f,
        28.0f,
        22.0f
    };
    const Color accent = emphasized ? ColorBrightness(DevelopmentCardAccent(type), 0.08f) : DevelopmentCardAccent(type);
    const Color body = emphasized ? ColorBrightness((Color){246, 239, 223, 255}, 0.04f) : (Color){246, 239, 223, 255};
    const Color bodyShade = (Color){233, 223, 204, 255};
    const Color borderColor = (Color){111, 83, 55, 255};

    DrawRotatedCardLayer(shadow, rotation, Fade(BLACK, (emphasized ? 0.24f : 0.16f) * alpha));
    DrawRotatedCardLayer(border, rotation, Fade(borderColor, 0.96f * alpha));
    DrawRotatedCardLayer(card, rotation, Fade(body, alpha));
    DrawRotatedCardLayer((Rectangle){card.x + 6.0f, card.y + 8.0f, card.width - 12.0f, 12.0f}, rotation, Fade(RAYWHITE, 0.30f * alpha));
    DrawRotatedCardLayer((Rectangle){card.x + 7.0f, card.y + 45.0f, card.width - 14.0f, card.height - 52.0f}, rotation, Fade(bodyShade, alpha));
    DrawRotatedCardLayer(ribbon, rotation, Fade(accent, alpha));
    DrawRotatedCardLayer(textPanel, rotation, Fade(RAYWHITE, (emphasized ? 0.54f : 0.44f) * alpha));
    DrawRotatedCardLayer((Rectangle){textPanel.x + 8.0f, textPanel.y + 8.0f, textPanel.width - 16.0f, 2.0f}, rotation, Fade(body, 0.90f * alpha));

    DrawCardText(font, card, rotation, (Vector2){18.0f, 22.0f}, DevelopmentCardTitle(type), 17, Fade(RAYWHITE, alpha));
    DrawCardText(font, card, rotation, (Vector2){18.0f, 74.0f}, DevelopmentCardDescription(type), 14, Fade((Color){62, 46, 34, 255}, alpha));

    if (count > 1)
    {
        const char *countLabel = TextFormat("x%d", count);
        const int countFontSize = 13;
        const float countSpacing = (float)countFontSize * 0.04f;
        const Vector2 countSize = MeasureTextEx(font, countLabel, (float)countFontSize, countSpacing);
        DrawRotatedCardLayer(badge, rotation, Fade((Color){72, 53, 34, 255}, 0.88f * alpha));
        DrawTextPro(
            font,
            countLabel,
            TransformCardPoint(card, rotation, (Vector2){badge.x - card.x + badge.width * 0.5f - countSize.x * 0.5f, badge.y - card.y + 4.0f}),
            (Vector2){0.0f, 0.0f},
            rotation,
            (float)countFontSize,
            countSpacing,
            Fade(RAYWHITE, alpha));
    }
}

static void DrawRotatedCardLayer(Rectangle bounds, float rotationDegrees, Color color)
{
    if (fabsf(rotationDegrees) < 0.01f)
    {
        DrawRectangleRec(bounds, color);
        return;
    }

    DrawRectanglePro(
        (Rectangle){
            bounds.x + bounds.width * 0.5f,
            bounds.y + bounds.height * 0.5f,
            bounds.width,
            bounds.height},
        (Vector2){bounds.width * 0.5f, bounds.height * 0.5f},
        rotationDegrees,
        color);
}

static bool PointInRotatedRectangle(Vector2 point, Rectangle bounds, float rotationDegrees)
{
    const Vector2 center = {
        bounds.x + bounds.width * 0.5f,
        bounds.y + bounds.height * 0.5f};
    const Vector2 local = RotateVector(
        (Vector2){point.x - center.x, point.y - center.y},
        -rotationDegrees * DEG2RAD);

    return fabsf(local.x) <= bounds.width * 0.5f &&
           fabsf(local.y) <= bounds.height * 0.5f;
}

static Vector2 RotateVector(Vector2 v, float radians)
{
    const float c = cosf(radians);
    const float s = sinf(radians);
    return (Vector2){
        v.x * c - v.y * s,
        v.x * s + v.y * c};
}

static Vector2 TransformCardPoint(Rectangle bounds, float rotationDegrees, Vector2 localPoint)
{
    const float radians = rotationDegrees * DEG2RAD;
    const Vector2 center = {
        bounds.x + bounds.width * 0.5f,
        bounds.y + bounds.height * 0.5f};
    const Vector2 offset = {
        localPoint.x - bounds.width * 0.5f,
        localPoint.y - bounds.height * 0.5f};
    const Vector2 rotated = RotateVector(offset, radians);
    return (Vector2){
        center.x + rotated.x,
        center.y + rotated.y};
}

static void DrawCardText(Font font, Rectangle bounds, float rotationDegrees, Vector2 localPoint, const char *text, int fontSize, Color color)
{
    char line[128];
    const float maxWidth = bounds.width - localPoint.x * 2.0f;
    const char *cursor = text;
    float y = localPoint.y;

    if (text == NULL || text[0] == '\0')
    {
        return;
    }

    while (*cursor != '\0')
    {
        size_t lineLength = 0;
        int lineFontSize = fontSize;
        float spacing = (float)lineFontSize * 0.04f;

        while (cursor[lineLength] != '\0' && cursor[lineLength] != '\n' && lineLength < sizeof(line) - 1)
        {
            lineLength++;
        }

        memcpy(line, cursor, lineLength);
        line[lineLength] = '\0';

        while (lineFontSize > 10 && MeasureTextEx(font, line, (float)lineFontSize, spacing).x > maxWidth)
        {
            lineFontSize--;
            spacing = (float)lineFontSize * 0.04f;
        }

        DrawTextPro(
            font,
            line,
            TransformCardPoint(bounds, rotationDegrees, (Vector2){localPoint.x, y}),
            (Vector2){0.0f, 0.0f},
            rotationDegrees,
            (float)lineFontSize,
            spacing,
            color);

        y += (float)lineFontSize * 1.20f;
        cursor += lineLength;
        if (*cursor == '\n')
        {
            cursor++;
        }
    }
}
