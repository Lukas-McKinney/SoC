static int BuildWrappedUiLinesDetailed(const char *message, int fontSize, int maxWidth, char lines[][96], int maxLines, bool *truncated)
{
    char textBuffer[192];
    int lineCount = 0;
    char *paragraph = NULL;
    bool wasTruncated = false;

    if (truncated != NULL)
    {
        *truncated = false;
    }

    if (message == NULL || message[0] == '\0' || lines == NULL || maxLines <= 0)
    {
        return 0;
    }

    strncpy(textBuffer, message, sizeof(textBuffer) - 1);
    textBuffer[sizeof(textBuffer) - 1] = '\0';
    paragraph = textBuffer;
    while (paragraph != NULL)
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
        while (word != NULL)
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
                candidate[sizeof(candidate) - 1] = '\0';
            }

            if (currentLine[0] != '\0' && MeasureUiText(candidate, fontSize) > maxWidth)
            {
                if (lineCount >= maxLines)
                {
                    wasTruncated = true;
                    break;
                }
                strncpy(lines[lineCount], currentLine, 95);
                lines[lineCount][95] = '\0';
                lineCount++;
                currentLine[0] = '\0';
                if (lineCount >= maxLines)
                {
                    wasTruncated = true;
                    break;
                }
                strncpy(currentLine, word, sizeof(currentLine) - 1);
                currentLine[sizeof(currentLine) - 1] = '\0';
            }
            else
            {
                strncpy(currentLine, candidate, sizeof(currentLine) - 1);
                currentLine[sizeof(currentLine) - 1] = '\0';
            }

            word = strtok(NULL, " ");
        }

        if (currentLine[0] != '\0')
        {
            if (lineCount >= maxLines)
            {
                wasTruncated = true;
                break;
            }
            strncpy(lines[lineCount], currentLine, 95);
            lines[lineCount][95] = '\0';
            lineCount++;
        }

        if (word != NULL)
        {
            wasTruncated = true;
            break;
        }

        if (nextParagraph != NULL && lineCount >= maxLines)
        {
            wasTruncated = true;
            break;
        }

        paragraph = nextParagraph;
    }

    if (truncated != NULL)
    {
        *truncated = wasTruncated;
    }

    return lineCount;
}

static int BuildWrappedUiLines(const char *message, int fontSize, int maxWidth, char lines[][96], int maxLines)
{
    return BuildWrappedUiLinesDetailed(message, fontSize, maxWidth, lines, maxLines, NULL);
}

static int MeasureWidestWrappedUiLine(char lines[][96], int lineCount, int fontSize)
{
    int widestLine = 0;

    for (int i = 0; i < lineCount; i++)
    {
        const int lineWidth = MeasureUiText(lines[i], fontSize);
        if (lineWidth > widestLine)
        {
            widestLine = lineWidth;
        }
    }

    return widestLine;
}

static int FitUiTextFontSize(const char *text, int preferredFontSize, int minFontSize, int maxWidth)
{
    if (text == NULL || text[0] == '\0' || maxWidth <= 0)
    {
        return preferredFontSize;
    }

    if (minFontSize > preferredFontSize)
    {
        minFontSize = preferredFontSize;
    }

    for (int fontSize = preferredFontSize; fontSize >= minFontSize; fontSize--)
    {
        if (MeasureUiText(text, fontSize) <= maxWidth)
        {
            return fontSize;
        }
    }

    return minFontSize;
}

static int BuildWrappedUiLinesFitted(const char *message, int preferredFontSize, int minFontSize, int maxWidth, char lines[][96], int maxLines, int *outFontSize)
{
    int lineCount = 0;

    if (outFontSize != NULL)
    {
        *outFontSize = preferredFontSize;
    }

    if (message == NULL || message[0] == '\0' || lines == NULL || maxLines <= 0 || maxWidth <= 0)
    {
        return 0;
    }

    if (minFontSize > preferredFontSize)
    {
        minFontSize = preferredFontSize;
    }

    for (int fontSize = preferredFontSize; fontSize >= minFontSize; fontSize--)
    {
        bool truncated = false;

        memset(lines, 0, (size_t)maxLines * sizeof(lines[0]));
        lineCount = BuildWrappedUiLinesDetailed(message, fontSize, maxWidth, lines, maxLines, &truncated);
        if (!truncated && MeasureWidestWrappedUiLine(lines, lineCount, fontSize) <= maxWidth)
        {
            if (outFontSize != NULL)
            {
                *outFontSize = fontSize;
            }
            return lineCount;
        }
    }

    memset(lines, 0, (size_t)maxLines * sizeof(lines[0]));
    lineCount = BuildWrappedUiLinesDetailed(message, minFontSize, maxWidth, lines, maxLines, NULL);
    if (outFontSize != NULL)
    {
        *outFontSize = minFontSize;
    }

    return lineCount;
}

static int DrawWrappedUiTextBlock(const char *message, float x, float y, int fontSize, int maxWidth, float lineHeight, Color color)
{
    char lines[3][96] = {{0}};
    const int lineCount = BuildWrappedUiLines(message, fontSize, maxWidth, lines, 3);

    for (int i = 0; i < lineCount; i++)
    {
        DrawUiText(lines[i], x, y + (float)i * lineHeight, fontSize, color);
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
        snprintf(buffer, bufferSize, "%s", winner == localHuman ? loc("You won") : loc("You lost"));
        return;
    }

    snprintf(buffer, bufferSize, loc("%s won"), PlayerName(winner));
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
            snprintf(buffer, bufferSize, "%s", loc("You reached 10 points"));
        }
        else
        {
            snprintf(buffer, bufferSize, loc("%s won"), PlayerName(winner));
        }
        return;
    }

    snprintf(buffer, bufferSize, loc("%s reaches 10 points"), PlayerName(winner));
}

static void FormatElapsedDuration(unsigned long long totalSeconds, char *buffer, size_t bufferSize)
{
    const unsigned long long hours = totalSeconds / 3600ULL;
    const unsigned long long minutes = (totalSeconds % 3600ULL) / 60ULL;
    const unsigned long long seconds = totalSeconds % 60ULL;

    if (buffer == NULL || bufferSize == 0)
    {
        return;
    }

    if (hours > 0ULL)
    {
        snprintf(buffer, bufferSize, "%llu:%02llu:%02llu", hours, minutes, seconds);
        return;
    }

    snprintf(buffer, bufferSize, "%02llu:%02llu", minutes, seconds);
}

static void DrawTurnPanelPlaytime(const struct Map *map, Rectangle panel, Color color)
{
    char duration[24];
    char label[48];
    const int fontSize = 16;
    int width = 0;
    const float textY = panel.y + 28.0f;

    if (map == NULL || gameHasWinner(map))
    {
        return;
    }

    FormatElapsedDuration(uiGetCurrentMatchPlaytimeSeconds(), duration, sizeof(duration));
    snprintf(label, sizeof(label), loc("Playtime %s"), duration);
    width = MeasureUiText(label, fontSize);
    DrawUiText(label, panel.x + panel.width - 16.0f - width, textY, fontSize, color);
}

static const char *NetplayStatusLabel(const struct MatchSession *session)
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
        return loc("Private multiplayer connected");
    case MATCH_CONNECTION_DISCONNECTED:
        return loc("Disconnected from remote player");
    case MATCH_CONNECTION_ERROR:
        return loc("Private multiplayer error");
    case MATCH_CONNECTION_LOCAL:
    default:
        return "";
    }
}
