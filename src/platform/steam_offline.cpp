#include <steam/steam_api.h>

// This backend keeps the engine's existing unavailable-Steam paths active.
// It is selected at link time, so SDK interface contexts retain their initial
// null slot for the lifetime of the process.
extern "C"
{
    ESteamAPIInitResult S_CALLTYPE SteamInternal_SteamAPI_Init(const char*, SteamErrMsg* error)
    {
        if (error != nullptr)
        {
            constexpr char message[] = "Steam services are disabled in this build.";
            static_assert(sizeof(message) <= sizeof(SteamErrMsg));
            memcpy(*error, message, sizeof(message));
        }
        return k_ESteamAPIInitResult_NoSteamClient;
    }

    HSteamUser S_CALLTYPE SteamAPI_GetHSteamUser()
    {
        return 0;
    }

    HSteamUser S_CALLTYPE SteamGameServer_GetHSteamUser()
    {
        return 0;
    }

    void* S_CALLTYPE SteamInternal_ContextInit(void* contextInitData)
    {
        // STEAM_DEFINE_INTERFACE_ACCESSOR supplies three pointer-sized slots.
        // It expects the address of the interface slot and dereferences it.
        return static_cast<void**>(contextInitData) + 2;
    }

    void* S_CALLTYPE SteamInternal_CreateInterface(const char*)
    {
        return nullptr;
    }

    void* S_CALLTYPE SteamInternal_FindOrCreateUserInterface(HSteamUser, const char*)
    {
        return nullptr;
    }

    void* S_CALLTYPE SteamInternal_FindOrCreateGameServerInterface(HSteamUser, const char*)
    {
        return nullptr;
    }

    void S_CALLTYPE SteamAPI_RunCallbacks() {}
    void S_CALLTYPE SteamGameServer_RunCallbacks() {}
    void S_CALLTYPE SteamAPI_Shutdown() {}
    void S_CALLTYPE SteamAPI_RegisterCallback(CCallbackBase*, int) {}
    void S_CALLTYPE SteamAPI_UnregisterCallback(CCallbackBase*) {}
    void S_CALLTYPE SteamAPI_RegisterCallResult(CCallbackBase*, SteamAPICall_t) {}
    void S_CALLTYPE SteamAPI_UnregisterCallResult(CCallbackBase*, SteamAPICall_t) {}
}
