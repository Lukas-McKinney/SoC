#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <stdbool.h>

#include "player.h"
#include "ui_state.h"

struct PersistedSettings
{
    enum UiTheme theme;
    int aiSpeed;
    enum AiDifficulty aiDifficulty;
    enum PlayerType humanColor;
};

void settingsStoreLoadDefaults(struct PersistedSettings *settings);
bool settingsStoreLoad(struct PersistedSettings *settings);
bool settingsStoreSave(const struct PersistedSettings *settings);
bool settingsStoreSaveCurrent(void);
const char *settingsStorePath(void);

#endif
