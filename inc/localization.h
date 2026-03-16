#ifndef LOCALIZATION_H
#define LOCALIZATION_H

#include <stdbool.h>

#include "player.h"

enum UiLanguage
{
    UI_LANGUAGE_ENGLISH,
    UI_LANGUAGE_GERMAN,
    UI_LANGUAGE_COUNT
};

void locSetLanguage(enum UiLanguage language);
enum UiLanguage locGetLanguage(void);
const char *loc(const char *englishText);

const char *locLanguageDisplayName(enum UiLanguage language);
const char *locLanguageConfigName(enum UiLanguage language);
bool locParseLanguage(const char *value, enum UiLanguage *language);

const char *locPlayerName(enum PlayerType player);
const char *locResourceName(enum ResourceType resource);
const char *locResourceShort(enum ResourceType resource);
const char *locPortResourceShort(enum ResourceType resource);
const char *locDevelopmentCardTitle(enum DevelopmentCardType type);
const char *locDevelopmentCardDescription(enum DevelopmentCardType type);
const char *locAiDifficultyLabel(enum AiDifficulty difficulty);

#endif
