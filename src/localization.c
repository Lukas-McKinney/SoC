#include "localization.h"

#include <stddef.h>
#include <string.h>

struct TranslationEntry
{
    const char *english;
    const char *german;
};

static enum UiLanguage gCurrentLanguage = UI_LANGUAGE_ENGLISH;

static const struct TranslationEntry kTranslations[] = {
    {"Catan Map", "Catan-Karte"},
    {"Settlers", "Siedler"},
    {"Quick Start", "Schnellstart"},
    {"Build, trade, and test a full match flow.", "Baue, handle und teste einen kompletten Spielablauf."},
    {"Start Game", "Spiel starten"},
    {"Start vs AI", "Gegen KI starten"},
    {"Statistics", "Statistiken"},
    {"Theme", "Design"},
    {"Theme: %s", "Design: %s"},
    {"Light", "Hell"},
    {"Dark", "Dunkel"},
    {"Language", "Sprache"},
    {"Language: %s", "Sprache: %s"},
    {"Quit", "Beenden"},
    {"Setup order is random every match.", "Die Aufbau-Reihenfolge ist in jeder Partie zufällig."},
    {"Start Match", "Partie starten"},
    {"Start Hotseat", "Hotseat starten"},
    {"Cancel", "Abbrechen"},
    {"Tracked across sessions.", "Wird über mehrere Sitzungen hinweg gespeichert."},
    {"Wins and losses count single-player matches.", "Siege und Niederlagen zählen nur Einzelspieler-Partien."},
    {"Total Playtime", "Gesamtspielzeit"},
    {"Wins", "Siege"},
    {"Losses", "Niederlagen"},
    {"Win Rate", "Siegquote"},
    {"Red", "Rot"},
    {"Blue", "Blau"},
    {"Green", "Grün"},
    {"Black", "Schwarz"},
    {"Player", "Spieler"},
    {"Wood", "Holz"},
    {"Wheat", "Getreide"},
    {"Clay", "Lehm"},
    {"Sheep", "Wolle"},
    {"Stone", "Erz"},
    {"Your Color: %s", "Deine Farbe: %s"},
    {"AI Difficulty: %s", "KI-Schwierigkeitsgrad: %s"},
    {"Close", "Schließen"},
    {"Build (B)", "Bauen (B)"},
    {"Road", "Straße"},
    {"Wood + Clay", "Holz + Lehm"},
    {"Settlement", "Siedlung"},
    {"Wood Clay Wheat Sheep", "Holz + Lehm + Getreide + Wolle"},
    {"City", "Stadt"},
    {"2 Wheat + 3 Stone", "2 Getreide + 3 Erz"},
    {"Development", "Entwicklungskarte"},
    {"Wheat Sheep Stone", "Getreide + Wolle + Erz"},
    {"Water Trade (W)", "Seehandel (W)"},
    {"Player Trade (T)", "Handel mit Spielern (T)"},
    {"Settings (Esc)", "Optionen (Esc)"},
    {"Settings", "Optionen"},
    {"Light Mode", "Heller Modus"},
    {"Dark Mode", "Dunkler Modus"},
    {"AI Speed", "KI-Geschwindigkeit"},
    {"0 slow", "0 langsam"},
    {"10 instant", "10 sofort"},
    {"Game", "Spiel"},
    {"Restart Game", "Spiel neu starten"},
    {"Back to Menu", "Zum Menü"},
    {"Quit Game", "Spiel beenden"},
    {"Return to Menu", "Zum Hauptmenü"},
    {"Back to main menu?", "Zum Hauptmenü zurückkehren?"},
    {"Current progress will be lost.", "Der aktuelle Spielfortschritt geht verloren."},
    {"Confirm Restart", "Neustart bestätigen"},
    {"Restart game?", "Spiel neu starten?"},
    {"Confirm Quit", "Beenden bestätigen"},
    {"Quit game?", "Spiel beenden?"},
    {"The application will close.", "Die Anwendung wird geschlossen."},
    {"Available Water Trades", "Verfügbare Seehandelsangebote"},
    {"Give", "Geben"},
    {"Receive", "Erhalten"},
    {"Amount", "Menge"},
    {"Trade %d %s for %d %s", "Tausche %d %s gegen %d %s"},
    {"Player Trade", "Spielerhandel"},
    {"Trade With", "Handelspartner"},
    {"You Give", "Du gibst"},
    {"You Receive", "Du erhältst"},
    {"Your Amount", "Deine Menge"},
    {"Their Amount", "Menge des Gegners"},
    {"Discard Half", "Hälfte der Karten abwerfen"},
    {"%s needs to discard %d cards.", "%s muss %d Karten abwerfen."},
    {"Pass the screen, then click anywhere here.", "Gib den Bildschirm weiter und klicke dann hier."},
    {"Resource counts stay hidden until they continue.", "Die Rohstoffanzahl bleibt verborgen, bis der Spieler fortfährt."},
    {"%s must discard %d cards", "%s muss %d Karten abwerfen"},
    {"Selected %d / %d", "Ausgewählt %d / %d"},
    {"Confirm Discard", "Abwerfen bestätigen"},
    {"Steal Resource", "Rohstoff stehlen"},
    {"%s is choosing an enemy.", "%s wählt einen Gegner."},
    {"Choose one adjacent player to steal from", "Wähle einen benachbarten Spieler zum Berauben"},
    {"Your Hand", "Deine Hand"},
    {"Current Player", "Aktiver Spieler"},
    {"Victory Points", "Siegpunkte"},
    {"Development Cards", "Entwicklungskarten"},
    {"Developement Cards", "Entwicklungskarten"},
    {"Resources", "Rohstoffe"},
    {"Opponents", "Gegner"},
    {"Visible VP", "Sichtbare SP"},
    {"Buy Development Card?", "Entwicklungskarte kaufen?"},
    {"Cost: Wheat + Sheep + Stone", "Kosten: Getreide + Wolle + Erz"},
    {"%d cards left in deck", "%d Karten im Stapel"},
    {"Confirm Buy", "Kauf bestätigen"},
    {"Use %s?", "%s verwenden?"},
    {"Move the thief and steal 1 random card.", "Bewege den Räuber und stiehl eine zufällige Karte."},
    {"You can still roll this turn afterward.", "Du kannst danach in diesem Zug noch würfeln."},
    {"Place up to 2 roads for free.", "Baue bis zu 2 Straßen kostenlos."},
    {"The board will switch into road placement mode.", "Das Spielfeld wechselt in den Straßenbau-Modus."},
    {"Choose 2 resources to take from the bank.", "Wähle 2 Rohstoffe aus der Bank."},
    {"First Resource", "Erster Rohstoff"},
    {"Second Resource", "Zweiter Rohstoff"},
    {"Choose 1 resource. Every opponent gives you that type.", "Wähle 1 Rohstoff. Jeder Gegner gibt dir diesen Rohstoff."},
    {"Target Resource", "Zielrohstoff"},
    {"Victory point cards are counted automatically.", "Siegpunktkarten werden automatisch gezählt."},
    {"Development cards can't be played the turn you buy them.\nOnly 1 development card can be played per turn.", "Entwicklungskarten können nicht im Zug gespielt werden, in dem sie gekauft wurden.\nPro Zug darf nur eine Entwicklungskarte gespielt werden."},
    {"%s accepts your trade.", "%s akzeptiert dein Handelsangebot."},
    {"%s declines the trade.", "%s lehnt dein Handelsangebot ab."},
    {"Confirm Use", "Verwendung bestätigen"},
    {"Gain Resources", "Rohstoffe erhalten"},
    {"Claim Resource", "Rohstoff nehmen"},
    {"Place Roads", "Straßen bauen"},
    {"Turn", "Zug"},
    {"Game Over", "Spiel vorbei"},
    {"%d victory points", "%d Siegpunkte"},
    {"Board is locked", "Spielfeld gesperrt"},
    {"Use the overlay buttons", "Nutze die Overlay-Schaltflächen"},
    {"to restart or return", "zum Neustarten oder Zurückkehren"},
    {"Setup Phase", "Aufbauphase"},
    {"Place 1 road", "1 Straße bauen"},
    {"Place 1 settlement", "1 Siedlung bauen"},
    {"Round %d / 8", "Runde %d / 8"},
    {"Dice", "Würfel"},
    {"Discard Cards", "Karten abwerfen"},
    {"Waiting for AI discard", "Warte auf KI-Abwurf"},
    {"%s is up next", "%s ist als Nächstes dran"},
    {"Pass the screen to continue", "Bildschirm weitergeben"},
    {"Choose %d cards to discard", "Wähle %d Karten zum Abwerfen"},
    {"Move Thief", "Räuber bewegen"},
    {"Select a land tile", "Wähle ein Landfeld"},
    {"before ending turn", "bevor du den Zug beendest"},
    {"%s is choosing", "%s wählt"},
    {"an enemy", "einen Gegner"},
    {"Choose one adjacent", "Wähle einen benachbarten"},
    {"player to rob", "Spieler zum Berauben"},
    {"Rolling...", "Würfeln..."},
    {"Choose what to do next.", "Wähle deine nächste Aktion."},
    {"Roll Dice (Enter)", "Würfeln (Enter)"},
    {"End Turn", "Zug beenden"},
    {"Unclaimed", "Unbeansprucht"},
    {"Knight", "Ritter"},
    {"Victory Point", "Siegpunkt"},
    {"Road Building", "Straßenbau"},
    {"Year of Plenty", "Erfindung"},
    {"Monopoly", "Monopol"},
    {"Playtime %s", "Spielzeit %s"},
    {"Match length %s", "Partiedauer %s"},
    {"Total playtime %s", "Gesamtspielzeit %s"},
    {"You won", "Du hast gewonnen"},
    {"You lost", "Du hast verloren"},
    {"%s won", "%s hat gewonnen"},
    {"You reached 10 points", "Du hast 10 Siegpunkte erreicht"},
    {"%s reaches 10 points", "%s erreicht 10 Siegpunkte"},
    {"Longest Road", "Längste Handelsstraße"},
    {"Largest Army", "Größte Rittermacht"},
    {"2 Victory Points", "2 Siegpunkte"},
    {"Road length: %d", "Straßenlänge: %d"},
    {"Need 5-road chain", "Benötigt eine Straßenkette mit Länge 5"},
    {"Need 3 knights", "Benötigt 3 Ritter"},
    {"Played knights: %d", "Gespielte Ritter: %d"},
    {"%d left", "%d übrig"},
    {"English", "English"},
    {"German", "Deutsch"},
    {"Easy", "Einfach"},
    {"Medium", "Mittel"},
    {"Hard", "Schwer"},
    {"VP", "SP"},
    {"Your turn.", "Du bist am Zug."},
    {"Your turn. You got %s.", "Du bist am Zug. Du hast %s erhalten."},
    {"You got %s.", "Du hast %s erhalten."},
    {"%s stole %s.", "%s hat %s gestohlen."},
    {"Lost %s.", "%s verloren."},
    {"A player", "Ein Spieler"},
    {"Waiting for remote player...", "Warte auf den anderen Spieler..."},
    {"Connecting to host...", "Verbinde mit Host..."},
    {"Syncing match state...", "Synchronisiere Spielstatus..."},
    {"Private multiplayer connected", "Privates Mehrspielerspiel verbunden"},
    {"Disconnected from remote player", "Verbindung zum anderen Spieler getrennt"},
    {"Private multiplayer error", "Fehler im privaten Mehrspielerspiel"},
    {"Remote player connected.", "Anderer Spieler verbunden."},
    {"Connected to host.", "Mit Host verbunden."},
    {"Remote player disconnected.", "Anderer Spieler getrennt."},
    {"Action rejected by host.", "Aktion vom Host abgelehnt."},
    {"Remote player trade offers are not available yet.", "Handelsangebote mit entfernten Spielern sind noch nicht verfügbar."},
    {"Restart is not available during private multiplayer.", "Neustart ist im privaten Mehrspielermodus nicht verfügbar."},
    {"Only the host can restart private multiplayer.", "Nur der Host kann das private Mehrspielerspiel neu starten."},
    {"Show Multiplayer Info", "Mehrspieler-Info anzeigen"},
    {"Hide Multiplayer Info", "Mehrspieler-Info ausblenden"},
    {"Host waits for one remote player.", "Host wartet auf einen entfernten Spieler."},
    {"Client follows host-authoritative state.", "Client folgt dem vom Host vorgegebenen Spielstand."},
    {"Trade offers support accept and decline.", "Handelsangebote unterstützen Annehmen und Ablehnen."},
    {"Trade offer sent to host.", "Handelsangebot an den Host gesendet."},
    {"Trade offer sent to remote player.", "Handelsangebot an den anderen Spieler gesendet."},
    {"Incoming Trade Offer", "Eingehendes Handelsangebot"},
    {"From %s", "Von %s"},
    {"Accept", "Annehmen"},
    {"Decline", "Ablehnen"},
    {"Accept or decline this offer.", "Nimm dieses Angebot an oder lehne es ab."},
    {"Remote trade offer received.", "Handelsangebot vom anderen Spieler erhalten."},
    {"Trade offer received.", "Handelsangebot erhalten."},
    {"Remote player accepted your trade.", "Der andere Spieler hat deinen Handel angenommen."},
    {"Remote player declined your trade.", "Der andere Spieler hat deinen Handel abgelehnt."},
    {"Host accepted your trade.", "Der Host hat deinen Handel angenommen."},
    {"Host declined your trade.", "Der Host hat deinen Handel abgelehnt."},
    {"Trade failed on host.", "Handel beim Host fehlgeschlagen."},
    {"Trade offer received. Press Y to accept or N to decline.", "Handelsangebot erhalten. Drücke Y zum Annehmen oder N zum Ablehnen."},
    {"Trade accepted.", "Handel angenommen."},
    {"Trade declined.", "Handel abgelehnt."},
    {"Multiplayer", "Mehrspieler"}};

void locSetLanguage(enum UiLanguage language)
{
    if (language < UI_LANGUAGE_ENGLISH || language >= UI_LANGUAGE_COUNT)
    {
        language = UI_LANGUAGE_ENGLISH;
    }

    gCurrentLanguage = language;
}

enum UiLanguage locGetLanguage(void)
{
    return gCurrentLanguage;
}

const char *loc(const char *englishText)
{
    if (englishText == NULL || englishText[0] == '\0' || gCurrentLanguage == UI_LANGUAGE_ENGLISH)
    {
        return englishText == NULL ? "" : englishText;
    }

    for (size_t i = 0; i < sizeof(kTranslations) / sizeof(kTranslations[0]); i++)
    {
        if (strcmp(kTranslations[i].english, englishText) == 0)
        {
            return kTranslations[i].german;
        }
    }

    return englishText;
}

const char *locLanguageDisplayName(enum UiLanguage language)
{
    switch (language)
    {
    case UI_LANGUAGE_GERMAN:
        return "Deutsch";
    case UI_LANGUAGE_ENGLISH:
    default:
        return "English";
    }
}

const char *locLanguageConfigName(enum UiLanguage language)
{
    switch (language)
    {
    case UI_LANGUAGE_GERMAN:
        return "german";
    case UI_LANGUAGE_ENGLISH:
    default:
        return "english";
    }
}

bool locParseLanguage(const char *value, enum UiLanguage *language)
{
    if (value == NULL || language == NULL)
    {
        return false;
    }

    if (strcmp(value, "english") == 0)
    {
        *language = UI_LANGUAGE_ENGLISH;
        return true;
    }
    if (strcmp(value, "german") == 0)
    {
        *language = UI_LANGUAGE_GERMAN;
        return true;
    }

    return false;
}

const char *locPlayerName(enum PlayerType player)
{
    switch (player)
    {
    case PLAYER_RED:
        return loc("Red");
    case PLAYER_BLUE:
        return loc("Blue");
    case PLAYER_GREEN:
        return loc("Green");
    case PLAYER_BLACK:
        return loc("Black");
    case PLAYER_NONE:
    default:
        return loc("Player");
    }
}

const char *locResourceName(enum ResourceType resource)
{
    switch (resource)
    {
    case RESOURCE_WOOD:
        return loc("Wood");
    case RESOURCE_WHEAT:
        return loc("Wheat");
    case RESOURCE_CLAY:
        return loc("Clay");
    case RESOURCE_SHEEP:
        return loc("Sheep");
    case RESOURCE_STONE:
        return loc("Stone");
    default:
        return "";
    }
}

const char *locResourceShort(enum ResourceType resource)
{
    if (gCurrentLanguage == UI_LANGUAGE_GERMAN)
    {
        switch (resource)
        {
        case RESOURCE_WOOD:
            return "Ho";
        case RESOURCE_WHEAT:
            return "Ge";
        case RESOURCE_CLAY:
            return "Le";
        case RESOURCE_SHEEP:
            return "Wo";
        case RESOURCE_STONE:
            return "Er";
        default:
            return "";
        }
    }

    switch (resource)
    {
    case RESOURCE_WOOD:
        return "Wd";
    case RESOURCE_WHEAT:
        return "Wh";
    case RESOURCE_CLAY:
        return "Cl";
    case RESOURCE_SHEEP:
        return "Sh";
    case RESOURCE_STONE:
        return "St";
    default:
        return "";
    }
}

const char *locPortResourceShort(enum ResourceType resource)
{
    if (gCurrentLanguage == UI_LANGUAGE_GERMAN)
    {
        switch (resource)
        {
        case RESOURCE_WOOD:
            return "H";
        case RESOURCE_WHEAT:
            return "G";
        case RESOURCE_CLAY:
            return "L";
        case RESOURCE_SHEEP:
            return "W";
        case RESOURCE_STONE:
            return "E";
        default:
            return "";
        }
    }

    switch (resource)
    {
    case RESOURCE_WOOD:
        return "W";
    case RESOURCE_WHEAT:
        return "H";
    case RESOURCE_CLAY:
        return "C";
    case RESOURCE_SHEEP:
        return "S";
    case RESOURCE_STONE:
        return "O";
    default:
        return "";
    }
}

const char *locDevelopmentCardTitle(enum DevelopmentCardType type)
{
    switch (type)
    {
    case DEVELOPMENT_CARD_KNIGHT:
        return loc("Knight");
    case DEVELOPMENT_CARD_VICTORY_POINT:
        return loc("Victory Point");
    case DEVELOPMENT_CARD_ROAD_BUILDING:
        return loc("Road Building");
    case DEVELOPMENT_CARD_YEAR_OF_PLENTY:
        return loc("Year of Plenty");
    case DEVELOPMENT_CARD_MONOPOLY:
        return loc("Monopoly");
    default:
        return loc("Development");
    }
}

const char *locDevelopmentCardDescription(enum DevelopmentCardType type)
{
    switch (type)
    {
    case DEVELOPMENT_CARD_KNIGHT:
        return loc("Move the thief\nand steal\n1 random card.");
    case DEVELOPMENT_CARD_VICTORY_POINT:
        return loc("Worth 1 hidden\nvictory point\nat the end.");
    case DEVELOPMENT_CARD_ROAD_BUILDING:
        return loc("Place 2 roads\nfor free.");
    case DEVELOPMENT_CARD_YEAR_OF_PLENTY:
        return loc("Take any\n2 resources\nfrom the bank.");
    case DEVELOPMENT_CARD_MONOPOLY:
        return loc("Choose 1 resource.\nAll opponents give\nyou that type.");
    default:
        return "";
    }
}

const char *locAiDifficultyLabel(enum AiDifficulty difficulty)
{
    switch (difficulty)
    {
    case AI_DIFFICULTY_EASY:
        return loc("Easy");
    case AI_DIFFICULTY_MEDIUM:
        return loc("Medium");
    case AI_DIFFICULTY_HARD:
        return loc("Hard");
    default:
        return loc("Easy");
    }
}
