#include "board_rules.h"
#include "game_action.h"
#include "game_logic.h"
#include "match_session.h"
#include "netplay.h"
#include "renderer_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

static int gFailureCount = 0;
static int gTestCount = 0;

static const Vector2 kBoardOrigin = {1600.0f * 0.42f, 900.0f * 0.46f};
static const unsigned int kPumpTimeoutMs = 5000u;
static const unsigned int kRelayReadyTimeoutMs = 3000u;

struct RelayProcess
{
    unsigned short port;
#ifdef _WIN32
    PROCESS_INFORMATION processInfo;
#else
    pid_t pid;
#endif
};

static bool test_relay_remote_play_starts_and_syncs_remote_setup_actions(void);
static bool test_relay_disconnect_emits_single_disconnect_event(void);

static bool choose_and_start_relay(struct RelayProcess *process);
static bool start_relay_process_at_port(struct RelayProcess *process, unsigned short port);
static void stop_relay_process(struct RelayProcess *process);
static bool wait_for_relay_ready(unsigned short port, unsigned int timeoutMs);
static bool wait_for_sessions_to_sync(struct MatchSession *host,
                                      struct MatchSession *client,
                                      unsigned int timeoutMs,
                                      bool (*predicate)(const struct MatchSession *host, const struct MatchSession *client));
static bool keep_sessions_connected_while_idle(struct MatchSession *host,
                                               struct MatchSession *client,
                                               unsigned int durationMs);
static bool lobby_ready_for_match_start(const struct MatchSession *host, const struct MatchSession *client);
static bool match_started_and_synced(const struct MatchSession *host, const struct MatchSession *client);
static bool action_sync_complete(const struct MatchSession *host, const struct MatchSession *client);
static void configure_two_player_relay_host(struct MatchSession *session);
static bool start_match_until_remote_player_turn(struct MatchSession *host,
                                                 struct MatchSession *client,
                                                 enum PlayerType remotePlayer,
                                                 int maxAttempts);
static bool find_valid_setup_settlement(const struct Map *map, enum PlayerType player, struct GameAction *actionOut);
static bool find_valid_setup_road(const struct Map *map, enum PlayerType player, struct GameAction *actionOut);
static unsigned short choose_test_port(unsigned int attempt);
static void pump_sessions_once(struct MatchSession *host, struct MatchSession *client);
static uint64_t now_ms(void);
static void sleep_ms(unsigned int milliseconds);
static bool socket_layer_init(void);
static void socket_layer_shutdown(void);
static bool can_connect_loopback(unsigned short port);

#define ASSERT_TRUE(expr)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __func__, __LINE__, #expr);                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ_INT(expected, actual)                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        const int expectedValue = (expected);                                                                          \
        const int actualValue = (actual);                                                                              \
        if (expectedValue != actualValue)                                                                              \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: expected %d but was %d\n", __func__, __LINE__, expectedValue, actualValue);      \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

int main(void)
{
    const struct
    {
        const char *name;
        bool (*fn)(void);
    } tests[] = {
        {"relay remote play starts a match and syncs remote setup actions",
         test_relay_remote_play_starts_and_syncs_remote_setup_actions},
        {"relay disconnect emits one disconnect event",
         test_relay_disconnect_emits_single_disconnect_event},
    };

    if (!socket_layer_init())
    {
        fprintf(stderr, "failed to initialize socket layer for remote play tests\n");
        return 1;
    }

    for (int i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    {
        gTestCount++;
        if (tests[i].fn())
        {
            printf("[PASS] %s\n", tests[i].name);
        }
        else
        {
            gFailureCount++;
            printf("[FAIL] %s\n", tests[i].name);
        }
    }

    socket_layer_shutdown();
    printf("\n%d/%d remote play tests passed\n", gTestCount - gFailureCount, gTestCount);
    return gFailureCount == 0 ? 0 : 1;
}

static bool test_relay_remote_play_starts_and_syncs_remote_setup_actions(void)
{
    struct RelayProcess relayProcess;
    struct MatchSession host;
    struct MatchSession client;
    struct GameActionContext actionContext = {kBoardOrigin, BOARD_HEX_RADIUS};
    struct GameAction settlementAction;
    struct GameAction roadAction;
    struct GameActionResult actionResult;
    bool relayStarted = false;
    bool success = false;

#define REQUIRE_TRUE(expr)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __func__, __LINE__, #expr);                             \
            goto cleanup;                                                                                              \
        }                                                                                                              \
    } while (0)

#define REQUIRE_EQ_INT(expected, actual)                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        const int expectedValue = (expected);                                                                          \
        const int actualValue = (actual);                                                                              \
        if (expectedValue != actualValue)                                                                              \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: expected %d but was %d\n", __func__, __LINE__, expectedValue, actualValue);      \
            goto cleanup;                                                                                              \
        }                                                                                                              \
    } while (0)

    memset(&relayProcess, 0, sizeof(relayProcess));
    memset(&host, 0, sizeof(host));
    memset(&client, 0, sizeof(client));
    memset(&settlementAction, 0, sizeof(settlementAction));
    memset(&roadAction, 0, sizeof(roadAction));
    memset(&actionResult, 0, sizeof(actionResult));

    srand(1);
    matchSessionInit(&host);
    matchSessionInit(&client);

    if (!choose_and_start_relay(&relayProcess))
    {
        goto cleanup;
    }
    relayStarted = true;

    matchSessionConfigurePrivateHostRelay(&host, PLAYER_BLUE, "127.0.0.1", relayProcess.port, "remote-play-test");
    configure_two_player_relay_host(&host);
    if (!matchSessionOpenPrivateHostRelay(&host, relayProcess.port, "remote-play-test"))
    {
        goto cleanup;
    }

    matchSessionConfigurePrivateClientRelay(&client, PLAYER_RED, "127.0.0.1", relayProcess.port, "remote-play-test");
    if (!matchSessionOpenPrivateClientRelay(&client, relayProcess.port, "remote-play-test"))
    {
        goto cleanup;
    }

    if (!wait_for_sessions_to_sync(&host, &client, kPumpTimeoutMs, lobby_ready_for_match_start))
    {
        goto cleanup;
    }
    REQUIRE_TRUE(matchSessionCanStartNetplayMatch(&host));
    REQUIRE_EQ_INT(MATCH_SEAT_REMOTE, matchSessionGetSeatAuthority(&host, PLAYER_RED));
    REQUIRE_EQ_INT(MATCH_SEAT_LOCAL, matchSessionGetSeatAuthority(&host, PLAYER_BLUE));
    REQUIRE_EQ_INT(PLAYER_RED, matchSessionGetLocalPlayer(&client));
    REQUIRE_EQ_INT(MATCH_SEAT_LOCAL, matchSessionGetSeatAuthority(&client, PLAYER_RED));
    REQUIRE_EQ_INT(PLAYER_CONTROL_DISABLED, client.map.players[PLAYER_GREEN].controlMode);
    REQUIRE_EQ_INT(PLAYER_CONTROL_DISABLED, client.map.players[PLAYER_BLACK].controlMode);
    REQUIRE_TRUE(keep_sessions_connected_while_idle(&host, &client, 100u));

    if (!start_match_until_remote_player_turn(&host, &client, PLAYER_RED, 8))
    {
        goto cleanup;
    }
    REQUIRE_TRUE(gameIsSetupSettlementTurn(&client.map));
    REQUIRE_TRUE(matchSessionLocalCanActOnCurrentDecision(&client));
    REQUIRE_TRUE(keep_sessions_connected_while_idle(&host, &client, 100u));

    REQUIRE_TRUE(find_valid_setup_settlement(&client.map, PLAYER_RED, &settlementAction));
    REQUIRE_TRUE(matchSessionSubmitAction(&client, &settlementAction, &actionContext, &actionResult));
    REQUIRE_TRUE(client.awaitingAuthoritativeUpdate);
    if (!wait_for_sessions_to_sync(&host, &client, kPumpTimeoutMs, action_sync_complete))
    {
        goto cleanup;
    }
    REQUIRE_EQ_INT(STRUCTURE_TOWN, host.map.tiles[settlementAction.tileId].corners[settlementAction.cornerIndex].structure);
    REQUIRE_EQ_INT(PLAYER_RED, host.map.tiles[settlementAction.tileId].corners[settlementAction.cornerIndex].owner);
    REQUIRE_EQ_INT(STRUCTURE_TOWN, client.map.tiles[settlementAction.tileId].corners[settlementAction.cornerIndex].structure);
    REQUIRE_EQ_INT(PLAYER_RED, client.map.tiles[settlementAction.tileId].corners[settlementAction.cornerIndex].owner);
    REQUIRE_TRUE(gameIsSetupRoadTurn(&client.map));
    REQUIRE_TRUE(matchSessionLocalCanActOnCurrentDecision(&client));

    REQUIRE_TRUE(find_valid_setup_road(&client.map, PLAYER_RED, &roadAction));
    REQUIRE_TRUE(matchSessionSubmitAction(&client, &roadAction, &actionContext, &actionResult));
    if (!wait_for_sessions_to_sync(&host, &client, kPumpTimeoutMs, action_sync_complete))
    {
        goto cleanup;
    }
    REQUIRE_TRUE(host.map.tiles[roadAction.tileId].sides[roadAction.sideIndex].isset);
    REQUIRE_EQ_INT(PLAYER_RED, host.map.tiles[roadAction.tileId].sides[roadAction.sideIndex].player);
    REQUIRE_TRUE(client.map.tiles[roadAction.tileId].sides[roadAction.sideIndex].isset);
    REQUIRE_EQ_INT(PLAYER_RED, client.map.tiles[roadAction.tileId].sides[roadAction.sideIndex].player);
    REQUIRE_EQ_INT(1, host.map.setupStep);
    REQUIRE_EQ_INT(1, client.map.setupStep);
    REQUIRE_EQ_INT(PLAYER_BLUE, host.map.currentPlayer);
    REQUIRE_EQ_INT(PLAYER_BLUE, client.map.currentPlayer);
    REQUIRE_TRUE(gameIsSetupSettlementTurn(&host.map));
    REQUIRE_TRUE(gameIsSetupSettlementTurn(&client.map));
    REQUIRE_EQ_INT((int)matchSessionGetStateHash(&host), (int)matchSessionGetStateHash(&client));

    success = true;

cleanup:
    matchSessionShutdown(&client);
    matchSessionShutdown(&host);
    if (relayStarted)
    {
        stop_relay_process(&relayProcess);
    }
#undef REQUIRE_TRUE
#undef REQUIRE_EQ_INT
    return success;
}

static bool test_relay_disconnect_emits_single_disconnect_event(void)
{
    struct RelayProcess relayProcess;
    struct NetplayState *host = NULL;
    struct NetplayState *client = NULL;
    bool relayStarted = false;
    bool hostConnected = false;
    bool clientConnected = false;
    int disconnectEvents = 0;
    bool success = false;

    memset(&relayProcess, 0, sizeof(relayProcess));

#define REQUIRE_TRUE(expr)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __func__, __LINE__, #expr);                             \
            goto cleanup;                                                                                              \
        }                                                                                                              \
    } while (0)

    if (!choose_and_start_relay(&relayProcess))
    {
        goto cleanup;
    }
    relayStarted = true;

    host = netplayCreate();
    client = netplayCreate();
    REQUIRE_TRUE(host != NULL);
    REQUIRE_TRUE(client != NULL);
    REQUIRE_TRUE(netplayStartRelayHost(host, "127.0.0.1", relayProcess.port, "disconnect-spam-test"));
    REQUIRE_TRUE(netplayStartRelayClient(client, "127.0.0.1", relayProcess.port, "disconnect-spam-test"));

    {
        const uint64_t connectDeadline = now_ms() + kPumpTimeoutMs;
        while (now_ms() < connectDeadline && (!hostConnected || !clientConnected))
        {
            struct NetplayEvent event;

            netplayUpdate(host);
            netplayUpdate(client);

            while (netplayPollEvent(host, &event))
            {
                if (event.type == NETPLAY_EVENT_CONNECTED)
                {
                    hostConnected = true;
                }
            }

            while (netplayPollEvent(client, &event))
            {
                if (event.type == NETPLAY_EVENT_CONNECTED)
                {
                    clientConnected = true;
                }
            }

            if (!hostConnected || !clientConnected)
            {
                sleep_ms(10u);
            }
        }
    }

    REQUIRE_TRUE(hostConnected);
    REQUIRE_TRUE(clientConnected);

    netplayDestroy(client);
    client = NULL;

    {
        const uint64_t disconnectDeadline = now_ms() + kPumpTimeoutMs;
        bool sawDisconnect = false;

        while (now_ms() < disconnectDeadline && !sawDisconnect)
        {
            struct NetplayEvent event;

            netplayUpdate(host);
            while (netplayPollEvent(host, &event))
            {
                if (event.type == NETPLAY_EVENT_DISCONNECTED)
                {
                    disconnectEvents++;
                    sawDisconnect = true;
                }
            }

            if (!sawDisconnect)
            {
                sleep_ms(10u);
            }
        }
    }

    REQUIRE_TRUE(disconnectEvents == 1);

    {
        const uint64_t quietDeadline = now_ms() + 200u;
        while (now_ms() < quietDeadline)
        {
            struct NetplayEvent event;

            netplayUpdate(host);
            while (netplayPollEvent(host, &event))
            {
                if (event.type == NETPLAY_EVENT_DISCONNECTED)
                {
                    disconnectEvents++;
                }
            }
            sleep_ms(10u);
        }
    }

    REQUIRE_TRUE(disconnectEvents == 1);
    success = true;

cleanup:
    if (client != NULL)
    {
        netplayDestroy(client);
    }
    if (host != NULL)
    {
        netplayDestroy(host);
    }
    if (relayStarted)
    {
        stop_relay_process(&relayProcess);
    }
#undef REQUIRE_TRUE
    return success;
}

static bool choose_and_start_relay(struct RelayProcess *process)
{
    if (process == NULL)
    {
        return false;
    }

    for (unsigned int attempt = 0; attempt < 16u; attempt++)
    {
        const unsigned short port = choose_test_port(attempt);
        if (start_relay_process_at_port(process, port))
        {
            return true;
        }
    }

    return false;
}

static bool start_relay_process_at_port(struct RelayProcess *process, unsigned short port)
{
    if (process == NULL)
    {
        return false;
    }

#ifdef _WIN32
    {
        STARTUPINFOA startupInfo;
        char commandLine[256];
        const char *relayBinary = getenv("SOC_RELAY_TEST_BIN");

        memset(&startupInfo, 0, sizeof(startupInfo));
        memset(&process->processInfo, 0, sizeof(process->processInfo));
        startupInfo.cb = sizeof(startupInfo);
        snprintf(commandLine,
                 sizeof(commandLine),
                 "\"%s\" %u",
                 (relayBinary != NULL && relayBinary[0] != '\0') ? relayBinary : "soc_relay.exe",
                 (unsigned int)port);

        if (!CreateProcessA(NULL,
                            commandLine,
                            NULL,
                            NULL,
                            FALSE,
                            CREATE_NO_WINDOW,
                            NULL,
                            NULL,
                            &startupInfo,
                            &process->processInfo))
        {
            return false;
        }
    }
#else
    {
        char portText[16];
        const char *relayBinary = getenv("SOC_RELAY_TEST_BIN");
        process->pid = fork();
        if (process->pid < 0)
        {
            return false;
        }

        if (process->pid == 0)
        {
            snprintf(portText, sizeof(portText), "%u", (unsigned int)port);
            execl((relayBinary != NULL && relayBinary[0] != '\0') ? relayBinary : "./soc_relay",
                  (relayBinary != NULL && relayBinary[0] != '\0') ? relayBinary : "./soc_relay",
                  portText,
                  NULL);
            _exit(127);
        }
    }
#endif

    process->port = port;
    if (wait_for_relay_ready(port, kRelayReadyTimeoutMs))
    {
        return true;
    }

    stop_relay_process(process);
    return false;
}

static void stop_relay_process(struct RelayProcess *process)
{
    if (process == NULL)
    {
        return;
    }

#ifdef _WIN32
    if (process->processInfo.hProcess != NULL)
    {
        TerminateProcess(process->processInfo.hProcess, 0);
        WaitForSingleObject(process->processInfo.hProcess, 2000u);
        CloseHandle(process->processInfo.hThread);
        CloseHandle(process->processInfo.hProcess);
        process->processInfo.hThread = NULL;
        process->processInfo.hProcess = NULL;
    }
#else
    if (process->pid > 0)
    {
        kill(process->pid, SIGTERM);
        (void)waitpid(process->pid, NULL, 0);
        process->pid = 0;
    }
#endif
}

static bool wait_for_relay_ready(unsigned short port, unsigned int timeoutMs)
{
    const uint64_t deadline = now_ms() + timeoutMs;

    while (now_ms() < deadline)
    {
        if (can_connect_loopback(port))
        {
            return true;
        }
        sleep_ms(50u);
    }

    return false;
}

static bool wait_for_sessions_to_sync(struct MatchSession *host,
                                      struct MatchSession *client,
                                      unsigned int timeoutMs,
                                      bool (*predicate)(const struct MatchSession *host, const struct MatchSession *client))
{
    const uint64_t deadline = now_ms() + timeoutMs;

    if (predicate == NULL)
    {
        return false;
    }

    while (now_ms() < deadline)
    {
        pump_sessions_once(host, client);
        if (predicate(host, client))
        {
            return true;
        }
        sleep_ms(10u);
    }

    return predicate(host, client);
}

static bool keep_sessions_connected_while_idle(struct MatchSession *host,
                                               struct MatchSession *client,
                                               unsigned int durationMs)
{
    const uint64_t deadline = now_ms() + durationMs;

    while (now_ms() < deadline)
    {
        pump_sessions_once(host, client);
        if (host == NULL ||
            client == NULL ||
            host->connectionStatus == MATCH_CONNECTION_DISCONNECTED ||
            host->connectionStatus == MATCH_CONNECTION_ERROR ||
            client->connectionStatus == MATCH_CONNECTION_DISCONNECTED ||
            client->connectionStatus == MATCH_CONNECTION_ERROR)
        {
            return false;
        }
        sleep_ms(10u);
    }

    return true;
}

static bool lobby_ready_for_match_start(const struct MatchSession *host, const struct MatchSession *client)
{
    return host != NULL &&
           client != NULL &&
           host->connectionStatus == MATCH_CONNECTION_CONNECTED &&
           client->connectionStatus == MATCH_CONNECTION_CONNECTED &&
           host->ready &&
           client->ready &&
           !host->matchStarted &&
           !client->matchStarted &&
           matchSessionGetLocalPlayer(client) == PLAYER_RED;
}

static bool match_started_and_synced(const struct MatchSession *host, const struct MatchSession *client)
{
    return host != NULL &&
           client != NULL &&
           host->ready &&
           client->ready &&
           host->matchStarted &&
           client->matchStarted &&
           host->connectionStatus == MATCH_CONNECTION_CONNECTED &&
           client->connectionStatus == MATCH_CONNECTION_CONNECTED &&
           matchSessionGetStateHash(host) == matchSessionGetStateHash(client);
}

static bool action_sync_complete(const struct MatchSession *host, const struct MatchSession *client)
{
    return host != NULL &&
           client != NULL &&
           host->connectionStatus == MATCH_CONNECTION_CONNECTED &&
           client->connectionStatus == MATCH_CONNECTION_CONNECTED &&
           !client->awaitingAuthoritativeUpdate &&
           matchSessionGetStateHash(host) == matchSessionGetStateHash(client);
}

static void configure_two_player_relay_host(struct MatchSession *session)
{
    if (session == NULL)
    {
        return;
    }

    session->seatAuthority[PLAYER_GREEN] = MATCH_SEAT_AI;
    session->seatAuthority[PLAYER_BLACK] = MATCH_SEAT_AI;
    session->map.players[PLAYER_RED].controlMode = PLAYER_CONTROL_HUMAN;
    session->map.players[PLAYER_BLUE].controlMode = PLAYER_CONTROL_HUMAN;
    session->map.players[PLAYER_GREEN].controlMode = PLAYER_CONTROL_DISABLED;
    session->map.players[PLAYER_BLACK].controlMode = PLAYER_CONTROL_DISABLED;
    session->map.players[PLAYER_RED].aiDifficulty = AI_DIFFICULTY_EASY;
    session->map.players[PLAYER_BLUE].aiDifficulty = AI_DIFFICULTY_EASY;
    session->map.players[PLAYER_GREEN].aiDifficulty = AI_DIFFICULTY_EASY;
    session->map.players[PLAYER_BLACK].aiDifficulty = AI_DIFFICULTY_EASY;
}

static bool start_match_until_remote_player_turn(struct MatchSession *host,
                                                 struct MatchSession *client,
                                                 enum PlayerType remotePlayer,
                                                 int maxAttempts)
{
    if (host == NULL || client == NULL || maxAttempts <= 0)
    {
        return false;
    }

    for (int attempt = 0; attempt < maxAttempts; attempt++)
    {
        if (attempt == 0)
        {
            if (!matchSessionStartNetplayMatch(host))
            {
                return false;
            }
        }
        else if (!matchSessionRestartNetplayMatch(host))
        {
            return false;
        }

        if (!wait_for_sessions_to_sync(host, client, kPumpTimeoutMs, match_started_and_synced))
        {
            return false;
        }

        if (host->map.currentPlayer == remotePlayer &&
            client->map.currentPlayer == remotePlayer &&
            gameIsSetupSettlementTurn(&host->map) &&
            gameIsSetupSettlementTurn(&client->map))
        {
            return true;
        }
    }

    return false;
}

static bool find_valid_setup_settlement(const struct Map *map, enum PlayerType player, struct GameAction *actionOut)
{
    if (map == NULL || actionOut == NULL)
    {
        return false;
    }

    memset(actionOut, 0, sizeof(*actionOut));
    actionOut->type = GAME_ACTION_PLACE_SETTLEMENT;

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int cornerIndex = 0; cornerIndex < HEX_CORNERS; cornerIndex++)
        {
            if (!IsCanonicalSharedCorner(tileId, cornerIndex))
            {
                continue;
            }

            if (!boardIsValidSettlementPlacement(map, tileId, cornerIndex, player, kBoardOrigin, BOARD_HEX_RADIUS))
            {
                continue;
            }

            actionOut->tileId = tileId;
            actionOut->cornerIndex = cornerIndex;
            return true;
        }
    }

    return false;
}

static bool find_valid_setup_road(const struct Map *map, enum PlayerType player, struct GameAction *actionOut)
{
    if (map == NULL || actionOut == NULL)
    {
        return false;
    }

    memset(actionOut, 0, sizeof(*actionOut));
    actionOut->type = GAME_ACTION_PLACE_ROAD;

    for (int tileId = 0; tileId < LAND_TILE_COUNT; tileId++)
    {
        for (int sideIndex = 0; sideIndex < HEX_CORNERS; sideIndex++)
        {
            if (!IsCanonicalSharedEdge(tileId, sideIndex) ||
                IsSharedEdgeOccupied(map, tileId, sideIndex) ||
                !boardIsValidRoadPlacement(map, tileId, sideIndex, player, kBoardOrigin, BOARD_HEX_RADIUS) ||
                !boardEdgeTouchesCorner(tileId,
                                        sideIndex,
                                        map->setupSettlementTileId,
                                        map->setupSettlementCornerIndex,
                                        kBoardOrigin,
                                        BOARD_HEX_RADIUS))
            {
                continue;
            }

            actionOut->tileId = tileId;
            actionOut->sideIndex = sideIndex;
            return true;
        }
    }

    return false;
}

static unsigned short choose_test_port(unsigned int attempt)
{
    const unsigned int base =
#ifdef _WIN32
        (unsigned int)GetCurrentProcessId();
#else
        (unsigned int)getpid();
#endif
    return (unsigned short)(30000u + ((base + attempt) % 20000u));
}

static void pump_sessions_once(struct MatchSession *host, struct MatchSession *client)
{
    if (host != NULL)
    {
        matchSessionUpdate(host);
    }
    if (client != NULL)
    {
        matchSessionUpdate(client);
    }
}

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return (uint64_t)spec.tv_sec * 1000u + (uint64_t)(spec.tv_nsec / 1000000u);
#endif
}

static void sleep_ms(unsigned int milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep((useconds_t)milliseconds * 1000u);
#endif
}

static bool socket_layer_init(void)
{
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

static void socket_layer_shutdown(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static bool can_connect_loopback(unsigned short port)
{
    bool connected = false;
    struct sockaddr_in address;
#ifdef _WIN32
    SOCKET socketHandle = INVALID_SOCKET;
#else
    int socketHandle = -1;
#endif

    socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (socketHandle == INVALID_SOCKET)
#else
    if (socketHandle < 0)
#endif
    {
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connected = connect(socketHandle, (const struct sockaddr *)&address, (int)sizeof(address)) == 0;

#ifdef _WIN32
    closesocket(socketHandle);
#else
    close(socketHandle);
#endif
    return connected;
}
