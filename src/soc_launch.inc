static void ConfigureLocalMatch(struct MatchSession *session, const struct MainMenuLobbyConfig *config)
{
    enum PlayerType primaryHuman = PLAYER_NONE;
    int humanCount = 0;

    if (session == NULL)
    {
        return;
    }

    if (config == NULL)
    {
        return;
    }

    aiConfigureHotseatMatch(&session->map);
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        const enum PlayerControlMode controlMode = config->seatControl[player] == PLAYER_CONTROL_AI
                                                       ? PLAYER_CONTROL_AI
                                                       : PLAYER_CONTROL_HUMAN;

        session->map.players[player].controlMode = controlMode;
        session->map.players[player].aiDifficulty = controlMode == PLAYER_CONTROL_AI
                                                       ? config->aiDifficulty
                                                       : AI_DIFFICULTY_EASY;
        if (controlMode == PLAYER_CONTROL_HUMAN)
        {
            humanCount++;
            if (primaryHuman == PLAYER_NONE)
            {
                primaryHuman = (enum PlayerType)player;
            }
        }
    }

    if (config->primaryHumanColor >= PLAYER_RED &&
        config->primaryHumanColor <= PLAYER_BLACK &&
        session->map.players[config->primaryHumanColor].controlMode == PLAYER_CONTROL_HUMAN)
    {
        primaryHuman = config->primaryHumanColor;
    }

    if (humanCount <= 1)
    {
        if (primaryHuman == PLAYER_NONE)
        {
            primaryHuman = PLAYER_RED;
        }

        matchSessionConfigureSinglePlayer(session, primaryHuman);
        return;
    }

    matchSessionConfigureHotseat(session);
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (session->map.players[player].controlMode == PLAYER_CONTROL_AI)
        {
            matchSessionSetSeatAuthority(session, (enum PlayerType)player, MATCH_SEAT_AI);
        }
    }
}

static void ConfigurePrivateMatch(struct MatchSession *session,
                                  enum PlayerType hostPlayer,
                                  enum PlayerType remotePlayer,
                                  enum AiDifficulty difficulty,
                                  bool localIsHost)
{
    if (session == NULL)
    {
        return;
    }

    aiConfigureHotseatMatch(&session->map);
    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (player == hostPlayer || player == remotePlayer)
        {
            session->map.players[player].controlMode = PLAYER_CONTROL_HUMAN;
            session->map.players[player].aiDifficulty = AI_DIFFICULTY_EASY;
        }
        else
        {
            session->map.players[player].controlMode = PLAYER_CONTROL_AI;
            session->map.players[player].aiDifficulty = difficulty;
        }
    }

    if (localIsHost)
    {
        matchSessionConfigurePrivateHost(session, hostPlayer);
        matchSessionSetSeatAuthority(session, remotePlayer, MATCH_SEAT_REMOTE);
    }
    else
    {
        /* Client seat is authoritative from host hello; avoid guessing via menu color. */
        matchSessionConfigurePrivateClient(session, PLAYER_NONE);
        matchSessionSetSeatAuthority(session, hostPlayer, MATCH_SEAT_REMOTE);
    }

    for (int player = PLAYER_RED; player <= PLAYER_BLACK; player++)
    {
        if (player != hostPlayer && player != remotePlayer)
        {
            matchSessionSetSeatAuthority(session, (enum PlayerType)player, MATCH_SEAT_AI);
        }
    }
}

static void LoadPersistedSettings(void)
{
    struct PersistedSettings settings;

    settingsStoreLoadDefaults(&settings);
    settingsStoreLoad(&settings);

    uiSetTheme(settings.theme);
    uiSetWindowMode(settings.windowMode);
    locSetLanguage(settings.language);
    uiSetAiSpeedSetting(settings.aiSpeed);
    MainMenuSetAiDifficulty(settings.aiDifficulty);
    MainMenuSetHumanColor(settings.humanColor);
    uiSetProfileName(settings.profileName);
    MainMenuSetMultiplayerHostAddress(settings.multiplayerHostAddress);
    MainMenuSetMultiplayerPort(settings.multiplayerPort);
    uiSetPersistedTotalPlaytimeSeconds(settings.totalPlaytimeSeconds);
    uiSetPersistedMatchRecord(settings.totalWins, settings.totalLosses);
}

static void InitLaunchOptions(struct LaunchOptions *options)
{
    if (options == NULL)
    {
        return;
    }

    memset(options, 0, sizeof(*options));
    options->port = NETPLAY_DEFAULT_PORT;
    options->localPlayer = PLAYER_RED;
    options->remotePlayer = PLAYER_BLUE;
    options->aiDifficulty = AI_DIFFICULTY_MEDIUM;
    options->hostAddress[0] = '\0';
}

static void PrintUsage(const char *programName)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s\n", programName);
    fprintf(stderr, "  %s --host [--player red|blue|green|black] [--remote-player color] [--port number] [--ai-difficulty easy|medium|hard]\n", programName);
    fprintf(stderr, "  %s --join <ip-or-hostname> [--player red|blue|green|black] [--remote-player color] [--port number]\n", programName);
}

static bool ParsePlayerType(const char *value, enum PlayerType *player)
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

static bool ParseAiDifficulty(const char *value, enum AiDifficulty *difficulty)
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

static bool ParseLaunchOptions(int argc, char **argv, struct LaunchOptions *options)
{
    bool localPlayerSpecified = false;
    bool remotePlayerSpecified = false;

    if (options == NULL)
    {
        return false;
    }

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        if (strcmp(arg, "--host") == 0)
        {
            options->hostMode = true;
        }
        else if (strcmp(arg, "--join") == 0)
        {
            if (i + 1 >= argc)
            {
                return false;
            }
            options->clientMode = true;
            snprintf(options->hostAddress, sizeof(options->hostAddress), "%s", argv[++i]);
        }
        else if (strcmp(arg, "--player") == 0 || strcmp(arg, "--local-player") == 0)
        {
            if (i + 1 >= argc || !ParsePlayerType(argv[++i], &options->localPlayer))
            {
                return false;
            }
            localPlayerSpecified = true;
        }
        else if (strcmp(arg, "--remote-player") == 0)
        {
            if (i + 1 >= argc || !ParsePlayerType(argv[++i], &options->remotePlayer))
            {
                return false;
            }
            remotePlayerSpecified = true;
        }
        else if (strcmp(arg, "--port") == 0)
        {
            long parsedPort = 0;
            if (i + 1 >= argc)
            {
                return false;
            }
            parsedPort = strtol(argv[++i], NULL, 10);
            if (parsedPort <= 0 || parsedPort > 65535)
            {
                return false;
            }
            options->port = (unsigned short)parsedPort;
        }
        else if (strcmp(arg, "--ai-difficulty") == 0)
        {
            if (i + 1 >= argc || !ParseAiDifficulty(argv[++i], &options->aiDifficulty))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    if (options->hostMode && options->clientMode)
    {
        return false;
    }

    if (options->clientMode && options->hostAddress[0] == '\0')
    {
        return false;
    }

    if (!options->hostMode && !options->clientMode)
    {
        return true;
    }

    if (!localPlayerSpecified)
    {
        options->localPlayer = options->hostMode ? PLAYER_RED : PLAYER_BLUE;
    }
    if (!remotePlayerSpecified)
    {
        options->remotePlayer = options->localPlayer == PLAYER_RED ? PLAYER_BLUE : PLAYER_RED;
    }

    return options->localPlayer != options->remotePlayer;
}
