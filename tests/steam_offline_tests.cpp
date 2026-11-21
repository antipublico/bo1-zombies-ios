#include <steam/steam_api.h>
#include <steam/isteamgameserver.h>

#include <stdio.h>
#include <string.h>

namespace
{
    int failures = 0;
    int checks = 0;

    void Check(bool condition, const char* description)
    {
        ++checks;
        if (!condition)
        {
            ++failures;
            fprintf(stderr, "FAIL: %s\n", description);
        }
    }

    struct CallbackReceiver
    {
        int calls = 0;
        CCallback<CallbackReceiver, UserStatsReceived_t> stats;
        CCallResult<CallbackReceiver, EncryptedAppTicketResponse_t> ticket;

        CallbackReceiver() : stats(this, &CallbackReceiver::OnStats) {}

        void OnStats(UserStatsReceived_t*) { ++calls; }
        void OnTicket(EncryptedAppTicketResponse_t*, bool) { ++calls; }
    };

    // Engine callback registration also occurs during static initialization.
    CallbackReceiver globalReceiver;

    void CheckInterfaces()
    {
        Check(SteamUser() == nullptr, "user interface unavailable");
        Check(SteamFriends() == nullptr, "friends interface unavailable");
        Check(SteamUtils() == nullptr, "utils interface unavailable");
        Check(SteamClient() == nullptr, "client interface unavailable");
        Check(SteamNetworking() == nullptr, "networking interface unavailable");
        Check(SteamUserStats() == nullptr, "stats interface unavailable");
        Check(SteamApps() == nullptr, "apps interface unavailable");
        Check(SteamGameServer() == nullptr, "server interface unavailable");
        Check(SteamAPI_GetHSteamUser() == 0, "client user handle unavailable");
        Check(SteamGameServer_GetHSteamUser() == 0, "server user handle unavailable");
    }
}

int main()
{
    CheckInterfaces();
    Check(!SteamAPI_Init(), "legacy init reports unavailable");

    SteamErrMsg error;
    memset(error, 'x', sizeof(error));
    Check(SteamAPI_InitEx(&error) == k_ESteamAPIInitResult_NoSteamClient,
        "init reports no Steam client");
    Check(strcmp(error, "Steam services are disabled in this build.") == 0,
        "init supplies a terminated diagnostic");
    Check(SteamAPI_InitEx(nullptr) == k_ESteamAPIInitResult_NoSteamClient,
        "init accepts omitted diagnostic");
    CheckInterfaces();

    void* context[3] = {nullptr, nullptr, nullptr};
    void* slot = SteamInternal_ContextInit(context);
    Check(slot == &context[2], "context lookup returns the interface slot address");
    Check(*static_cast<void**>(slot) == nullptr, "interface slot remains null");
    Check(SteamInternal_CreateInterface("unknown") == nullptr, "direct interface lookup unavailable");
    Check(SteamInternal_FindOrCreateUserInterface(0, "unknown") == nullptr,
        "user interface lookup unavailable");
    Check(SteamInternal_FindOrCreateGameServerInterface(0, "unknown") == nullptr,
        "server interface lookup unavailable");

    {
        CallbackReceiver localReceiver;
        localReceiver.ticket.Set(1, &localReceiver, &CallbackReceiver::OnTicket);
        SteamAPI_RunCallbacks();
        SteamGameServer_RunCallbacks();
        Check(localReceiver.calls == 0, "offline callbacks never report successful results");
        localReceiver.ticket.Cancel();
        Check(!localReceiver.ticket.IsActive(), "SDK call result cancellation succeeds");
    }
    Check(globalReceiver.calls == 0, "static callback registration is safe before init");

    SteamAPI_Shutdown();
    SteamAPI_Shutdown();
    CheckInterfaces();
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
