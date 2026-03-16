#include "settings_store.h"

#include "main_menu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim_whitespace(char *text);
static bool parse_theme(const char *value, enum UiTheme *theme);
static bool parse_difficulty(const char *value, enum AiDifficulty *difficulty);
static bool parse_player_color(const char *value, enum PlayerType *player);
static const char *theme_label(enum UiTheme theme);
static const char *difficulty_label(enum AiDifficulty difficulty);
static const char *player_color_label(enum PlayerType player);

void settingsStoreLoadDefaults(struct PersistedSettings *settings)
{
    if (settings == NULL)
    {
        return;
    }

    settings->theme = UI_THEME_LIGHT;
    settings->aiSpeed = 3;
    settings->aiDifficulty = AI_DIFFICULTY_MEDIUM;
    settings->humanColor = PLAYER_RED;
}

const char *settingsStorePath(void)
{
    static char path[1024];
    static bool initialized = false;

    if (!initialized)
    {
        const char *base = NULL;

#ifdef _WIN32
        base = getenv("APPDATA");
        if (base != NULL && base[0] != '\0')
        {
            snprintf(path, sizeof(path), "%s\\SoC.settings.cfg", base);
        }
        else
        {
            base = getenv("USERPROFILE");
            if (base != NULL && base[0] != '\0')
            {
                snprintf(path, sizeof(path), "%s\\SoC.settings.cfg", base);
            }
            else
            {
                snprintf(path, sizeof(path), "SoC.settings.cfg");
            }
        }
#else
        base = getenv("HOME");
        if (base != NULL && base[0] != '\0')
        {
            snprintf(path, sizeof(path), "%s/.soc_settings.cfg", base);
        }
        else
        {
            snprintf(path, sizeof(path), ".soc_settings.cfg");
        }
#endif

        initialized = true;
    }

    return path;
}

bool settingsStoreLoad(struct PersistedSettings *settings)
{
    FILE *file = NULL;
    char line[128];
    bool loaded = false;

    if (settings == NULL)
    {
        return false;
    }

    settingsStoreLoadDefaults(settings);
    file = fopen(settingsStorePath(), "r");
    if (file == NULL)
    {
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *key = NULL;
        char *value = NULL;
        char *separator = NULL;

        key = trim_whitespace(line);
        if (key[0] == '\0' || key[0] == '#')
        {
            continue;
        }

        separator = strchr(key, '=');
        if (separator == NULL)
        {
            continue;
        }

        *separator = '\0';
        value = trim_whitespace(separator + 1);
        key = trim_whitespace(key);

        if (strcmp(key, "theme") == 0)
        {
            loaded |= parse_theme(value, &settings->theme);
        }
        else if (strcmp(key, "ai_speed") == 0)
        {
            const int parsed = atoi(value);
            settings->aiSpeed = parsed < 0 ? 0 : (parsed > 10 ? 10 : parsed);
            loaded = true;
        }
        else if (strcmp(key, "ai_difficulty") == 0)
        {
            loaded |= parse_difficulty(value, &settings->aiDifficulty);
        }
        else if (strcmp(key, "human_color") == 0)
        {
            loaded |= parse_player_color(value, &settings->humanColor);
        }
    }

    fclose(file);
    return loaded;
}

bool settingsStoreSave(const struct PersistedSettings *settings)
{
    FILE *file = NULL;

    if (settings == NULL)
    {
        return false;
    }

    file = fopen(settingsStorePath(), "w");
    if (file == NULL)
    {
        return false;
    }

    fprintf(file, "theme=%s\n", theme_label(settings->theme));
    fprintf(file, "ai_speed=%d\n", settings->aiSpeed);
    fprintf(file, "ai_difficulty=%s\n", difficulty_label(settings->aiDifficulty));
    fprintf(file, "human_color=%s\n", player_color_label(settings->humanColor));

    fclose(file);
    return true;
}

bool settingsStoreSaveCurrent(void)
{
    struct PersistedSettings settings;

    settings.theme = uiGetTheme();
    settings.aiSpeed = uiGetAiSpeedSetting();
    settings.aiDifficulty = MainMenuGetAiDifficulty();
    settings.humanColor = MainMenuGetHumanColor();
    return settingsStoreSave(&settings);
}

static char *trim_whitespace(char *text)
{
    char *start = text;
    char *end = NULL;

    if (text == NULL)
    {
        return NULL;
    }

    while (*start != '\0' && isspace((unsigned char)*start))
    {
        start++;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1]))
    {
        end--;
    }

    *end = '\0';
    return start;
}

static bool parse_theme(const char *value, enum UiTheme *theme)
{
    if (value == NULL || theme == NULL)
    {
        return false;
    }

    if (strcmp(value, "dark") == 0)
    {
        *theme = UI_THEME_DARK;
        return true;
    }
    if (strcmp(value, "light") == 0)
    {
        *theme = UI_THEME_LIGHT;
        return true;
    }

    return false;
}

static bool parse_difficulty(const char *value, enum AiDifficulty *difficulty)
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

static bool parse_player_color(const char *value, enum PlayerType *player)
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

static const char *theme_label(enum UiTheme theme)
{
    return theme == UI_THEME_DARK ? "dark" : "light";
}

static const char *difficulty_label(enum AiDifficulty difficulty)
{
    switch (difficulty)
    {
    case AI_DIFFICULTY_EASY:
        return "easy";
    case AI_DIFFICULTY_HARD:
        return "hard";
    case AI_DIFFICULTY_MEDIUM:
    default:
        return "medium";
    }
}

static const char *player_color_label(enum PlayerType player)
{
    switch (player)
    {
    case PLAYER_BLUE:
        return "blue";
    case PLAYER_GREEN:
        return "green";
    case PLAYER_BLACK:
        return "black";
    case PLAYER_RED:
    default:
        return "red";
    }
}
