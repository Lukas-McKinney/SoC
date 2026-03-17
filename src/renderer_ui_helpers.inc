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
    const struct MatchSession *session = matchSessionGetActive();
    enum PlayerType humanPlayer = PLAYER_NONE;
    int humanCount = 0;
    int aiCount = 0;

    if (map == NULL)
    {
        return PLAYER_NONE;
    }

    if (session != NULL && session->localPlayer >= PLAYER_RED && session->localPlayer <= PLAYER_BLACK)
    {
        return session->localPlayer;
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
    return locPlayerName(player);
}

static const char *ResourceName(enum ResourceType resource)
{
    return locResourceName(resource);
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
    const Rectangle detailPanel = {bounds.x + 16.0f, bounds.y + 96.0f, bounds.width - 32.0f, 40.0f};
    const Rectangle ownerChip = {bounds.x + 16.0f, bounds.y + bounds.height - 34.0f, bounds.width - 32.0f, 22.0f};
    const bool claimed = owner >= PLAYER_RED && owner <= PLAYER_BLACK;
    const Color chipFill = claimed ? Fade(PlayerColor(owner), 0.92f) : (Color){221, 212, 194, 255};
    const Color chipText = claimed ? RAYWHITE : mutedText;
    const char *ownerLabel = claimed ? PlayerName(owner) : loc("Unclaimed");
    char detailLines[2][96] = {{0}};
    const int titleFont = FitUiTextFontSize(title, 22, 17, (int)bounds.width - 30);
    const int subtitleFont = FitUiTextFontSize(subtitle, 13, 11, (int)bounds.width - 30);
    const int ownerFont = FitUiTextFontSize(ownerLabel, 14, 12, (int)ownerChip.width - 12);
    int detailFont = 15;
    const int detailLineCount = BuildWrappedUiLinesFitted(detail, 15, 12, (int)detailPanel.width - 10, detailLines, 2, &detailFont);
    const int titleWidth = MeasureUiText(title, titleFont);
    const int subtitleWidth = MeasureUiText(subtitle, subtitleFont);
    const int ownerWidth = MeasureUiText(ownerLabel, ownerFont);
    const float detailLineHeight = (float)detailFont + 2.0f;
    const float detailBlockHeight = detailLineCount > 0 ? detailLineHeight * (float)detailLineCount : 0.0f;
    const float detailTextY = detailPanel.y + (detailPanel.height - detailBlockHeight) * 0.5f - 1.0f;

    DrawRectangleRounded((Rectangle){bounds.x + 6.0f, bounds.y + 8.0f, bounds.width, bounds.height}, 0.10f, 8, Fade(BLACK, 0.10f));
    DrawRectangleRounded(bounds, 0.10f, 8, parchment);
    DrawRectangleLinesEx(bounds, 2.0f, border);
    DrawRectangleRounded(ribbon, 0.16f, 8, Fade(accent, 0.96f));
    DrawRectangleLinesEx(ribbon, 1.4f, Fade(border, 0.80f));
    DrawRectangleRounded(detailPanel, 0.28f, 8, (Color){249, 243, 232, 255});
    DrawRectangleLinesEx(detailPanel, 1.5f, Fade(border, 0.54f));

    DrawUiText(title, bounds.x + bounds.width * 0.5f - titleWidth * 0.5f, bounds.y + 52.0f + (22 - titleFont) * 0.5f, titleFont, textColor);
    DrawUiText(subtitle, bounds.x + bounds.width * 0.5f - subtitleWidth * 0.5f, bounds.y + 84.0f + (13 - subtitleFont) * 0.5f, subtitleFont, mutedText);
    for (int i = 0; i < detailLineCount; i++)
    {
        const int detailWidth = MeasureUiText(detailLines[i], detailFont);
        const float detailX = detailPanel.x + detailPanel.width * 0.5f - detailWidth * 0.5f;
        const float lineY = detailTextY + (float)i * detailLineHeight;

        DrawUiText(detailLines[i], detailX, lineY + 1.0f, detailFont, Fade((Color){255, 255, 255, 255}, 0.32f));
        DrawUiText(detailLines[i], detailX, lineY, detailFont, detailText);
    }

    DrawRectangleRounded(ownerChip, 0.40f, 8, chipFill);
    DrawRectangleLinesEx(ownerChip, 1.4f, claimed ? Fade(border, 0.64f) : (Color){154, 132, 108, 255});
    DrawUiText(ownerLabel, ownerChip.x + ownerChip.width * 0.5f - ownerWidth * 0.5f, ownerChip.y + 3.0f + (14 - ownerFont) * 0.5f, ownerFont, chipText);
}

static const char *DevelopmentCardTitle(enum DevelopmentCardType type)
{
    return locDevelopmentCardTitle(type);
}

static const char *DevelopmentCardDescription(enum DevelopmentCardType type)
{
    return locDevelopmentCardDescription(type);
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
