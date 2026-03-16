#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <stdbool.h>

#include "localization.h"
#include "player.h"
#include "ui_state.h"

struct PersistedSettings
{
    enum UiTheme theme;
    enum UiLanguage language;
    int aiSpeed;
    enum AiDifficulty aiDifficulty;
    enum PlayerType humanColor;
    unsigned long long totalPlaytimeSeconds;
    unsigned long long totalWins;
    unsigned long long totalLosses;
};

void settingsStoreLoadDefaults(struct PersistedSettings *settings);
bool settingsStoreLoad(struct PersistedSettings *settings);
bool settingsStoreSave(const struct PersistedSettings *settings);
bool settingsStoreSaveCurrent(void);
const char *settingsStorePath(void);

#endif
