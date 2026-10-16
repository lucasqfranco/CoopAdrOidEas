#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <sstream>
#include <fstream>
#include <algorithm>

#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>
#define LOG_TAG "CoopAndreasBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#if __has_include(<jni.h>)
#include <jni.h>
#else
typedef void* JNIEnv;
typedef void* jobject;
typedef void* jclass;
typedef void* jstring;
typedef int32_t jint;
typedef int64_t jlong;
typedef float jfloat;
typedef uint8_t jboolean;
#define JNI_TRUE 1
#define JNI_FALSE 0
#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL
#endif
#define LOGI(...) do { fprintf(stdout, "[CoopAndreasBridge INFO] " __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
#define LOGW(...) do { fprintf(stdout, "[CoopAndreasBridge WARN] " __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, "[CoopAndreasBridge ERROR] " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
#include <unistd.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

namespace coopandreas {

// ============================================================================
// CONSTANTES DE PROTOCOLO TORNAMIC/COOPANDREAS (PUERTO UDP 6767 + 8990)
// ============================================================================
constexpr int kMaxCoopPlayers = 8;
constexpr int kCoopAndreasUdpPort = 6767;           // Puerto oficial Tornamic/CoopAndreas
constexpr int kDefaultControlTcpPort = 8989;        // Puerto TCP Lobby App
constexpr int kDefaultTelemetryVoiceUdpPort = 8990; // Puerto UDP App Telemetría/Voz
constexpr int kDefaultDiscoveryUdpPort = 8988;      // Puerto UDP Broadcast LAN
constexpr int kTickRateHz = 30;                     // 30 Hz sync loop (33ms por tick)

// Tipos de Paquetes Binarios (Protocolo Tornamic/CoopAndreas Mobile)
enum PacketType : uint8_t {
    PKT_HANDSHAKE    = 0x01,
    PKT_PED_SYNC     = 0x02,
    PKT_VEHICLE_SYNC = 0x03,
    PKT_MISSION_SYNC = 0x04,
    PKT_WEAPON_FIRE  = 0x05
};

// Opcodes Personalizados estilo sa_sbl_coopandreas (0D00 - 0D04)
enum CustomCoopOpcode : uint16_t {
    OPCODE_COOP_SYNC_LOCAL_PLAYER       = 0x0D00, // Sincroniza jugador local CJ y libera cámara 3D
    OPCODE_COOP_APPLY_REMOTE_PED_STATE  = 0x0D01, // Spawnea/interpola actores remotos P2..P4
    OPCODE_COOP_SYNC_VEHICLE_DRIVEBY    = 0x0D02, // Sincroniza conductor + pasajeros con drive-by
    OPCODE_COOP_SYNC_MISSION_CHECKPOINT = 0x0D03, // Sincroniza $ONMISSION y checkpoints de historia
    OPCODE_COOP_SYNC_WEAPON_FIRE        = 0x0D04  // Sincroniza disparo, puntería e impactos
};

// IMPORTANTE: CVector con alineación natural de 4 bytes (sin pragma pack(1))
// para que ARM64 AAPCS64 lo pase como Homogeneous Floating-Point Aggregate (HFA) en s0, s1, s2
// al invocar funciones nativas de libGTASA.so (_Z15FindPlayerCoorsi, _ZN4CPed8TeleportE7CVectorh, etc.)
struct CVector {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
static_assert(sizeof(CVector) == 12, "CVector must be 12 bytes");

#pragma pack(push, 1)
// Cabecera común de paquete binario UDP (puerto 6767)
struct BinaryPacketHeader {
    uint32_t magic = 0x504F4F43; // 'COOP'
    uint8_t version = 2;
    uint8_t packetType = PKT_PED_SYNC;
    uint8_t playerSlot = 1;
    uint8_t flags = 0;           // bit 0 = isHost, bit 1 = independentCam, bit 2 = onMission, bit 3 = hasRealGtaCoords
    char roomCode[16] = "GROVE-404";
    uint32_t sequenceNumber = 0;
    uint64_t timestampMs = 0;
};

struct HandshakePacket {
    BinaryPacketHeader header;
    int32_t skinModelId = 0;
    char playerName[48] = "Carl_CJ";
    uint16_t listenUdpPort = kCoopAndreasUdpPort;
};

struct PedSyncPacket {
    BinaryPacketHeader header;
    int32_t skinModelId = 0;
    CVector position{2495.0f, -1687.0f, 13.5f};
    CVector velocity{0.0f, 0.0f, 0.0f};
    float heading = 0.0f;
    int32_t health = 100;
    int32_t armor = 100;
    int32_t currentWeaponId = 28;
    int32_t ammoInClip = 500;
    uint16_t animGroupId = 0;
    uint16_t animId = 0;
    uint8_t isCrouching = 0;
    uint8_t isJumping = 0;
    uint8_t isAiming = 0;
    uint8_t independentCamera = 1; // 1 = Cámara 3D independiente en cada teléfono
};

struct VehicleSyncPacket {
    BinaryPacketHeader header;
    int32_t vehicleNetId = 101;
    int32_t vehicleModelId = 492; // 492 = Greenwood
    int32_t seatIndex = 0;        // -1 = a pie, 0 = conductor, 1..3 = pasajeros
    CVector position{2495.0f, -1687.0f, 13.5f};
    CVector velocity{0.0f, 0.0f, 0.0f};
    CVector turnSpeed{0.0f, 0.0f, 0.0f};
    float heading = 0.0f;
    float steerAngle = 0.0f;
    float gasPedal = 0.0f;
    float brakePedal = 0.0f;
    float vehicleHealth = 1000.0f;
    uint8_t primaryColor = 16;    // Grove Green
    uint8_t secondaryColor = 1;
    uint8_t isDriveByActive = 0;  // 1 = Pasajero disparando por ventanilla (Opcode 0713)
    uint8_t sirenOrHorn = 0;
};

struct MissionSyncPacket {
    BinaryPacketHeader header;
    int32_t onMissionFlag = 0;    // _ZN11CTheScripts14OnAMissionFlagE ($ONMISSION)
    char missionId[32] = "ls_02";
    int32_t checkpointIndex = 0;
    int32_t totalCheckpoints = 3;
    CVector checkpointPos{2495.0f, -1687.0f, 13.5f};
    float checkpointRadius = 4.5f;
    int32_t wantedLevel = 0;
};

struct WeaponFirePacket {
    BinaryPacketHeader header;
    int32_t weaponId = 28;
    CVector muzzleOrigin{2495.0f, -1687.0f, 14.0f};
    CVector targetImpact{2505.0f, -1680.0f, 14.0f};
    int32_t hitEntityNetId = -1;
    uint8_t hitEntityType = 0;    // 0 = mundo, 1 = ped, 2 = vehículo
    uint8_t damageAmount = 15;
};

// Archivo binario de memoria compartida (coop_telemetry.bin) entre libCoopAndreasBridge.so,
// los scripts .csa y la aplicación Android
struct SharedTelemetryBinaryFile {
    uint32_t magic = 0x4D4C5443; // 'CTLM'
    uint32_t version = 210;
    uint64_t lastUpdateMs = 0;
    uint8_t isHost = 1;
    uint8_t localPlayerSlot = 1;
    uint8_t maxPlayers = 4;
    uint8_t independentCameraMode = 1;
    char roomCode[16] = "GROVE-404";
    char hostIp[32] = "0.0.0.0";
    uint16_t udpPort6767 = kCoopAndreasUdpPort;
    uint16_t udpPort8990 = kDefaultTelemetryVoiceUdpPort;
    MissionSyncPacket missionState;
    PedSyncPacket playersPed[kMaxCoopPlayers];
    VehicleSyncPacket playersVeh[kMaxCoopPlayers];
    WeaponFirePacket lastWeaponFire[kMaxCoopPlayers];
};
#pragma pack(pop)

// ============================================================================
// PUNTEROS A FUNCIONES Y SÍMBOLOS NATIVOS DE LIBGTASA.SO (v2.00 / v2.10 ARM64)
// ============================================================================
using FnFindPlayerPed = void* (*)(int playerId);
using FnFindPlayerCoors = CVector (*)(int playerId);
using FnFindPlayerSpeed = CVector (*)(int playerId);
using FnFindPlayerHeading = float (*)(int playerId);
using FnFindPlayerVehicle = void* (*)(int playerId, bool bIncludeRemote);
using FnIsPlayerOnAMission = bool (*)();
using FnCWorldProcess = void (*)();
using FnCPedOperatorNew = void* (*)(size_t size);
using FnCCivilianPedCtor = void* (*)(void* thisPed, int pedType, uint32_t modelIndex);
using FnCPedSetCharCreatedBy = void (*)(void* thisPed, uint8_t createdBy);
using FnCPedTeleport = void (*)(void* thisPed, CVector pos, uint8_t keepTasks);
using FnCPedSetModelIndex = void (*)(void* thisPed, uint32_t modelIndex);
using FnCPedGiveWeapon = void (*)(void* thisPed, int weaponType, uint32_t ammo, bool likeUnused);
using FnCPedSetCurrentWeapon = void (*)(void* thisPed, int weaponType);
using FnCWorldAdd = void (*)(void* entity);
using FnCWorldRemove = void (*)(void* entity);
using FnCPoolsGetPedRef = int (*)(void* ped);
using FnCPoolsGetPed = void* (*)(int handle);
using FnCStreamingRequestModel = void (*)(int modelId, int flags);
using FnCStreamingLoadAllRequestedModels = void (*)(bool onlyPriority);
using FnCMatrixSetRotateZOnly = void (*)(void* thisMatrix, float angleRad);
using FnCMatrixUpdateRwMatrix = void (*)(const void* thisMatrix, void* rwMatrix);
using FnCEntityUpdateRwFrame = void (*)(void* thisEntity);
using FnCRadarSetEntityBlip = int (*)(int blipType, int entityHandle, uint32_t color, int blipDisplay, char* scriptName);
using FnCRadarClearBlipForEntity = void (*)(int blipType, int entityHandle);
using FnC3dMarkersPlaceMarkerCone = void (*)(
    uint64_t id, CVector& pos, float scale,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    uint16_t pulsePeriod, float pulseFraction, int16_t rotateRate, uint8_t enableCollision
);

struct GtaSaNativeSymbols {
    void* libHandle = nullptr;
    uintptr_t libBase = 0;

    // Core symbols requested in spec
    void* cworldPlayers = nullptr;               // _ZN6CWorld7PlayersE
    void* cpoolsPedPool = nullptr;               // _ZN6CPools11ms_pPedPoolE
    void* cpoolsVehiclePool = nullptr;           // _ZN6CPools15ms_pVehiclePoolE
    uint8_t* scriptSpace = nullptr;              // _ZN11CTheScripts11ScriptSpaceE
    uint32_t* onAMissionFlagOffset = nullptr;    // _ZN11CTheScripts14OnAMissionFlagE
    void* processOneCommandFn = nullptr;         // _ZN14CRunningScript18ProcessOneCommandEv

    // Engine functions for real 3D Ped Spawning, Sync & Independent Camera
    FnFindPlayerPed findPlayerPedFn = nullptr;
    FnFindPlayerCoors findPlayerCoorsFn = nullptr;
    FnFindPlayerSpeed findPlayerSpeedFn = nullptr;
    FnFindPlayerHeading findPlayerHeadingFn = nullptr;
    FnFindPlayerVehicle findPlayerVehicleFn = nullptr;
    FnIsPlayerOnAMission isPlayerOnAMissionFn = nullptr;

    void* cgameProcessFn = nullptr;              // _ZN5CGame7ProcessEv
    FnCWorldProcess cworldProcessFn = nullptr;   // _ZN6CWorld7ProcessEv
    FnCPedOperatorNew cpedOperatorNewFn = nullptr;
    FnCCivilianPedCtor ccivilianPedCtorFn = nullptr;
    FnCPedSetCharCreatedBy cpedSetCharCreatedByFn = nullptr;
    FnCPedTeleport cpedTeleportFn = nullptr;
    FnCPedSetModelIndex cpedSetModelIndexFn = nullptr;
    FnCPedGiveWeapon cpedGiveWeaponFn = nullptr;
    FnCPedSetCurrentWeapon cpedSetCurrentWeaponFn = nullptr;
    FnCWorldAdd cworldAddFn = nullptr;
    FnCWorldRemove cworldRemoveFn = nullptr;
    FnCPoolsGetPedRef cpoolsGetPedRefFn = nullptr;
    FnCPoolsGetPed cpoolsGetPedFn = nullptr;
    FnCStreamingRequestModel cstreamingRequestModelFn = nullptr;
    FnCStreamingLoadAllRequestedModels cstreamingLoadAllRequestedModelsFn = nullptr;
    uint8_t* cstreamingInfoForModel = nullptr;   // _ZN10CStreaming16ms_aInfoForModelE
    FnCMatrixSetRotateZOnly cmatrixSetRotateZOnlyFn = nullptr;
    FnCMatrixUpdateRwMatrix cmatrixUpdateRwMatrixFn = nullptr;
    FnCEntityUpdateRwFrame centityUpdateRwFrameFn = nullptr;
    FnCRadarSetEntityBlip cradarSetEntityBlipFn = nullptr;
    FnCRadarClearBlipForEntity cradarClearBlipForEntityFn = nullptr;
    FnC3dMarkersPlaceMarkerCone c3dMarkersPlaceMarkerConeFn = nullptr;
    uint8_t* bPlayersCanBeInSeparateCars = nullptr; // _ZN10CGameLogic27bPlayersCanBeInSeparateCarsE

    bool allCoreResolved = false;

    bool resolveFromLibGtaSa() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        libHandle = dlopen("libGTASA.so", RTLD_LAZY | RTLD_NOLOAD);
        if (!libHandle) {
            libHandle = dlopen("libGTASA.so", RTLD_LAZY);
        }
        if (!libHandle) {
            return false;
        }

        cworldPlayers = dlsym(libHandle, "_ZN6CWorld7PlayersE");
        cpoolsPedPool = dlsym(libHandle, "_ZN6CPools11ms_pPedPoolE");
        cpoolsVehiclePool = dlsym(libHandle, "_ZN6CPools15ms_pVehiclePoolE");
        scriptSpace = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN11CTheScripts11ScriptSpaceE"));
        onAMissionFlagOffset = reinterpret_cast<uint32_t*>(dlsym(libHandle, "_ZN11CTheScripts14OnAMissionFlagE"));
        if (!onAMissionFlagOffset) {
            onAMissionFlagOffset = reinterpret_cast<uint32_t*>(dlsym(libHandle, "_ZN11CTheScripts11OnAMissionFlagE"));
        }
        processOneCommandFn = dlsym(libHandle, "_ZN14CRunningScript18ProcessOneCommandEv");

        findPlayerPedFn = reinterpret_cast<FnFindPlayerPed>(dlsym(libHandle, "_Z13FindPlayerPedi"));
        if (!findPlayerPedFn) {
            findPlayerPedFn = reinterpret_cast<FnFindPlayerPed>(dlsym(libHandle, "_Z13FindPlayerPedv"));
        }
        findPlayerCoorsFn = reinterpret_cast<FnFindPlayerCoors>(dlsym(libHandle, "_Z15FindPlayerCoorsi"));
        findPlayerSpeedFn = reinterpret_cast<FnFindPlayerSpeed>(dlsym(libHandle, "_Z15FindPlayerSpeedi"));
        findPlayerHeadingFn = reinterpret_cast<FnFindPlayerHeading>(dlsym(libHandle, "_Z17FindPlayerHeadingi"));
        findPlayerVehicleFn = reinterpret_cast<FnFindPlayerVehicle>(dlsym(libHandle, "_Z17FindPlayerVehicleib"));
        isPlayerOnAMissionFn = reinterpret_cast<FnIsPlayerOnAMission>(dlsym(libHandle, "_ZN11CTheScripts18IsPlayerOnAMissionEv"));

        cgameProcessFn = dlsym(libHandle, "_ZN5CGame7ProcessEv");
        cworldProcessFn = reinterpret_cast<FnCWorldProcess>(dlsym(libHandle, "_ZN6CWorld7ProcessEv"));

        cpedOperatorNewFn = reinterpret_cast<FnCPedOperatorNew>(dlsym(libHandle, "_ZN4CPednwEm"));
        if (!cpedOperatorNewFn) {
            cpedOperatorNewFn = reinterpret_cast<FnCPedOperatorNew>(dlsym(libHandle, "_ZN4CPednwEj"));
        }
        ccivilianPedCtorFn = reinterpret_cast<FnCCivilianPedCtor>(dlsym(libHandle, "_ZN12CCivilianPedC1E8ePedTypej"));
        cpedSetCharCreatedByFn = reinterpret_cast<FnCPedSetCharCreatedBy>(dlsym(libHandle, "_ZN4CPed16SetCharCreatedByEh"));
        cpedTeleportFn = reinterpret_cast<FnCPedTeleport>(dlsym(libHandle, "_ZN4CPed8TeleportE7CVectorh"));
        cpedSetModelIndexFn = reinterpret_cast<FnCPedSetModelIndex>(dlsym(libHandle, "_ZN4CPed13SetModelIndexEj"));
        cpedGiveWeaponFn = reinterpret_cast<FnCPedGiveWeapon>(dlsym(libHandle, "_ZN4CPed10GiveWeaponE11eWeaponTypejb"));
        cpedSetCurrentWeaponFn = reinterpret_cast<FnCPedSetCurrentWeapon>(dlsym(libHandle, "_ZN4CPed16SetCurrentWeaponE11eWeaponType"));

        cworldAddFn = reinterpret_cast<FnCWorldAdd>(dlsym(libHandle, "_ZN6CWorld3AddEP7CEntity"));
        cworldRemoveFn = reinterpret_cast<FnCWorldRemove>(dlsym(libHandle, "_ZN6CWorld6RemoveEP7CEntity"));
        cpoolsGetPedRefFn = reinterpret_cast<FnCPoolsGetPedRef>(dlsym(libHandle, "_ZN6CPools9GetPedRefEP4CPed"));
        cpoolsGetPedFn = reinterpret_cast<FnCPoolsGetPed>(dlsym(libHandle, "_ZN6CPools6GetPedEi"));

        cstreamingRequestModelFn = reinterpret_cast<FnCStreamingRequestModel>(dlsym(libHandle, "_ZN10CStreaming12RequestModelEii"));
        cstreamingLoadAllRequestedModelsFn = reinterpret_cast<FnCStreamingLoadAllRequestedModels>(dlsym(libHandle, "_ZN10CStreaming22LoadAllRequestedModelsEb"));
        cstreamingInfoForModel = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN10CStreaming16ms_aInfoForModelE"));

        cmatrixSetRotateZOnlyFn = reinterpret_cast<FnCMatrixSetRotateZOnly>(dlsym(libHandle, "_ZN7CMatrix14SetRotateZOnlyEf"));
        cmatrixUpdateRwMatrixFn = reinterpret_cast<FnCMatrixUpdateRwMatrix>(dlsym(libHandle, "_ZNK7CMatrix14UpdateRwMatrixEP11RwMatrixTag"));
        centityUpdateRwFrameFn = reinterpret_cast<FnCEntityUpdateRwFrame>(dlsym(libHandle, "_ZN7CEntity13UpdateRwFrameEv"));

        cradarSetEntityBlipFn = reinterpret_cast<FnCRadarSetEntityBlip>(dlsym(libHandle, "_ZN6CRadar13SetEntityBlipE9eBlipTypeij12eBlipDisplayPc"));
        cradarClearBlipForEntityFn = reinterpret_cast<FnCRadarClearBlipForEntity>(dlsym(libHandle, "_ZN6CRadar18ClearBlipForEntityE9eBlipTypei"));
        c3dMarkersPlaceMarkerConeFn = reinterpret_cast<FnC3dMarkersPlaceMarkerCone>(dlsym(libHandle, "_ZN10C3dMarkers15PlaceMarkerConeEyR7CVectorfhhhhtfsh"));
        bPlayersCanBeInSeparateCars = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN10CGameLogic27bPlayersCanBeInSeparateCarsE"));

        if (cworldProcessFn) {
            Dl_info info{};
            if (dladdr(reinterpret_cast<void*>(cworldProcessFn), &info) && info.dli_fbase) {
                libBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
            }
        }

        allCoreResolved = (cworldPlayers != nullptr && scriptSpace != nullptr && processOneCommandFn != nullptr);
        LOGI("libGTASA.so resolved (base=0x%lx): CWorld::Players=%p, PedPool=%p, VehPool=%p, ScriptSpace=%p, OnAMissionFlag=%p, ProcessOneCommand=%p, FindPlayerPed=%p, FindPlayerCoors=%p, CWorld::Process=%p, CCivilianPed=%p",
             static_cast<unsigned long>(libBase), cworldPlayers, cpoolsPedPool, cpoolsVehiclePool,
             scriptSpace, onAMissionFlagOffset, processOneCommandFn,
             reinterpret_cast<void*>(findPlayerPedFn), reinterpret_cast<void*>(findPlayerCoorsFn),
             reinterpret_cast<void*>(cworldProcessFn), reinterpret_cast<void*>(ccivilianPedCtorFn));
        return allCoreResolved;
#else
        return false;
#endif
    }

    bool isModelLoaded(int modelId) const {
        if (!cstreamingInfoForModel || modelId < 0 || modelId > 20000) return false;
        // Each CStreamingInfo is 20 bytes; m_nLoadState (uint8_t) is at offset 0x10 (1 == LOADSTATE_LOADED)
        return cstreamingInfoForModel[modelId * 20 + 16] == 1;
    }
};

struct PlayerState3D {
    int32_t playerSlot = 1;
    bool isActive = false;
    bool hasRealGtaCoords = false;
    uint64_t lastNetworkUpdateMs = 0;
    int32_t skinModelId = 0;
    char playerName[48] = "Player";
    float x = 2495.0f;
    float y = -1687.0f;
    float z = 13.5f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float heading = 0.0f;
    int32_t health = 100;
    int32_t armor = 100;
    int32_t weaponId = 28;
    int32_t ammo = 500;
    int32_t wantedLevel = 0;
    uint16_t animGroupId = 0;
    uint16_t animId = 0;

    int32_t vehicleNetId = 0;
    int32_t vehicleModelId = 492;
    int32_t seatIndex = -1;
    float steerAngle = 0.0f;
    float gasPedal = 0.0f;
    float brakePedal = 0.0f;
    bool isDriveByActive = false;

    CVector lastAimOrigin{2495.0f, -1687.0f, 14.0f};
    CVector lastAimTarget{2505.0f, -1680.0f, 14.0f};
    uint32_t shotsFiredCount = 0;

    int32_t cameraMode = 0;
};

struct MissionSyncState {
    int32_t onMissionFlag = 0;
    char missionId[32] = "ls_02";
    int32_t checkpointIndex = 0;
    int32_t totalCheckpoints = 3;
    float checkpointX = 2495.0f;
    float checkpointY = -1687.0f;
    float checkpointZ = 13.5f;
    float checkpointRadius = 4.5f;
    bool freeRoamAnySave = true;
};

struct BridgeConfig {
    bool isHost = true;
    std::string hostIp = "0.0.0.0";
    std::string roomCode = "GROVE-404";
    std::string connectionMode = "LAN_WIFI";
    std::string telemetryBinPath = "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/coop_telemetry.bin";
    int32_t udpCoopPort = kCoopAndreasUdpPort;             // 6767
    int32_t tcpControlPort = kDefaultControlTcpPort;       // 8989
    int32_t udpStreamPort = kDefaultTelemetryVoiceUdpPort; // 8990
    int32_t discoveryPort = kDefaultDiscoveryUdpPort;     // 8988
    int32_t localPlayerSlot = 1;
    int32_t maxPlayers = 4;
    int32_t cameraMode = 0;
    bool allowIndependentVehicles = true;
    bool allowSharedVehicleDriveBy = true;
    bool freeRoamAnySave = true;
};

#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
struct PeerEndpoint {
    sockaddr_in addr{};
    uint64_t lastSeenMs = 0;
};
#endif

static FnCWorldProcess g_origCWorldProcess = nullptr;
static void Hooked_CWorld_Process();

class CoopEngineBridge {
public:
    static CoopEngineBridge& instance() {
        static CoopEngineBridge inst;
        return inst;
    }

    bool initialize(bool isHost, const std::string& hostIp, const std::string& roomCode, int localSlot, int maxPlayers) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.isHost = isHost;
        config_.hostIp = hostIp.empty() ? (isHost ? "0.0.0.0" : "192.168.1.100") : hostIp;
        config_.roomCode = roomCode.empty() ? "GROVE-404" : roomCode;
        config_.localPlayerSlot = std::clamp(localSlot, 1, kMaxCoopPlayers);
        config_.maxPlayers = std::clamp(maxPlayers, 2, kMaxCoopPlayers);
        config_.cameraMode = 0;

        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            players_[i].playerSlot = i + 1;
            players_[i].isActive = (i == 0 || (i + 1) == config_.localPlayerSlot);
            players_[i].cameraMode = 0;
        }
        players_[0].skinModelId = 0;   // CJ
        std::snprintf(players_[0].playerName, sizeof(players_[0].playerName), "Carl 'CJ' (P1)");
        players_[1].skinModelId = 270; // Sweet
        std::snprintf(players_[1].playerName, sizeof(players_[1].playerName), "Sean 'Sweet' (P2)");
        players_[2].skinModelId = 271; // Ryder
        std::snprintf(players_[2].playerName, sizeof(players_[2].playerName), "Lance 'Ryder' (P3)");
        players_[3].skinModelId = 269; // Big Smoke
        std::snprintf(players_[3].playerName, sizeof(players_[3].playerName), "Big Smoke (P4)");

        gtaSymbols_.resolveFromLibGtaSa();

        initialized_.store(true);
        LOGI("CoopEngineBridge v2.3 initialized: role=%s, hostIp=%s, room=%s, slot=%d, maxPlayers=%d, port=%d",
             isHost ? "HOST" : "GUEST", config_.hostIp.c_str(), config_.roomCode.c_str(),
             config_.localPlayerSlot, config_.maxPlayers, config_.udpCoopPort);
        return true;
    }

    bool installMainThreadGotHook() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (gotHookInstalled_.load()) return true;
        if (!gtaSymbols_.cworldProcessFn) {
            gtaSymbols_.resolveFromLibGtaSa();
        }
        if (!gtaSymbols_.cworldProcessFn) return false;

        void* targetFn = reinterpret_cast<void*>(gtaSymbols_.cworldProcessFn);
        g_origCWorldProcess = gtaSymbols_.cworldProcessFn;

        // 1. Check exact GOT offset 0x843fb0 in libGTASA.so v2.10 arm64-v8a first
        if (gtaSymbols_.libBase != 0) {
            uintptr_t candidateGot = gtaSymbols_.libBase + 0x843fb0;
            uintptr_t pageStart = candidateGot & ~static_cast<uintptr_t>(0xFFF);
            if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                void** slot = reinterpret_cast<void**>(candidateGot);
                if (*slot == targetFn) {
                    *slot = reinterpret_cast<void*>(&Hooked_CWorld_Process);
                    gotHookInstalled_.store(true);
                    LOGI("Installed GOT Hook on _ZN6CWorld7ProcessEv at 0x%lx (orig=%p -> hook=%p)",
                         static_cast<unsigned long>(candidateGot), targetFn, reinterpret_cast<void*>(&Hooked_CWorld_Process));
                    return true;
                }
            }
        }

        // 2. Dynamic scan of rw-p / r--p GOT segments of libGTASA.so via /proc/self/maps
        std::ifstream maps("/proc/self/maps");
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find("libGTASA.so") == std::string::npos) continue;
            if (line.find("rw-p") == std::string::npos && line.find("r--p") == std::string::npos) continue;

            unsigned long startAddr = 0, endAddr = 0;
            if (std::sscanf(line.c_str(), "%lx-%lx", &startAddr, &endAddr) != 2) continue;
            if (endAddr <= startAddr || (endAddr - startAddr) > 0x400000) continue;

            for (uintptr_t p = startAddr; p + sizeof(void*) <= endAddr; p += sizeof(void*)) {
                void** slot = reinterpret_cast<void**>(p);
                if (*slot == targetFn) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CWorld_Process);
                        gotHookInstalled_.store(true);
                        LOGI("Installed scanned GOT Hook on _ZN6CWorld7ProcessEv at 0x%lx (orig=%p)",
                             static_cast<unsigned long>(p), targetFn);
                        return true;
                    }
                }
            }
        }
#endif
        return false;
    }

    // ========================================================================
    // EJECUTADO EN EL HILO PRINCIPAL DE GTA SA (CADA FRAME DE CWorld::Process)
    // ========================================================================
    void onMainGameThreadTick() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        mainThreadTicks_.fetch_add(1);
        if (!gtaSymbols_.findPlayerPedFn || !gtaSymbols_.findPlayerCoorsFn) return;

        void* localPed = gtaSymbols_.findPlayerPedFn(-1);
        if (!localPed) {
            // El jugador aún está en el menú principal o pantalla de carga
            lastLocalPedPtr_ = nullptr;
            for (int i = 0; i < kMaxCoopPlayers; ++i) {
                remotePedPtr_[i] = nullptr;
                remotePedHandle_[i] = -1;
            }
            return;
        }

        if (localPed != lastLocalPedPtr_) {
            // Se acaba de cargar una partida o reiniciar misión
            lastLocalPedPtr_ = localPed;
            initialSyncTeleportDone_ = false;
            for (int i = 0; i < kMaxCoopPlayers; ++i) {
                remotePedPtr_[i] = nullptr;
                remotePedHandle_[i] = -1;
            }
            LOGI("Local Player CJ detected in world: CPlayerPed=%p", localPed);
        }

        // Forzar cámara 3D independiente y vehículos separados permitidos
        if (gtaSymbols_.bPlayersCanBeInSeparateCars) {
            *gtaSymbols_.bPlayersCanBeInSeparateCars = 1;
        }

        // Leer coordenadas 3D reales, rotación, velocidad y salud de CJ
        CVector myPos = gtaSymbols_.findPlayerCoorsFn(-1);
        float myHeading = gtaSymbols_.findPlayerHeadingFn ? gtaSymbols_.findPlayerHeadingFn(-1) : 0.0f;
        CVector mySpeed = gtaSymbols_.findPlayerSpeedFn ? gtaSymbols_.findPlayerSpeedFn(-1) : CVector{0.0f, 0.0f, 0.0f};
        void* myVeh = gtaSymbols_.findPlayerVehicleFn ? gtaSymbols_.findPlayerVehicleFn(-1, false) : nullptr;
        bool onMission = gtaSymbols_.isPlayerOnAMissionFn ? gtaSymbols_.isPlayerOnAMissionFn() : false;

        float rawHealth = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(localPed) + 0x908);
        float rawArmor = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(localPed) + 0x910);
        uint16_t localModelId = *reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(localPed) + 0x32);
        int vehModelId = 0;
        if (myVeh) {
            vehModelId = static_cast<int>(*reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(myVeh) + 0x32));
        }

        uint64_t nowMs = currentTimeMillis();

        std::lock_guard<std::mutex> lock(mutex_);
        int localIdx = std::clamp(config_.localPlayerSlot, 1, kMaxCoopPlayers) - 1;
        PlayerState3D& me = players_[localIdx];
        me.playerSlot = localIdx + 1;
        me.isActive = true;
        if (std::isfinite(myPos.x) && std::isfinite(myPos.y) && std::isfinite(myPos.z) &&
            (std::fabs(myPos.x) > 0.1f || std::fabs(myPos.y) > 0.1f)) {
            me.hasRealGtaCoords = true;
            me.x = myPos.x;
            me.y = myPos.y;
            me.z = myPos.z;
            me.vx = mySpeed.x;
            me.vy = mySpeed.y;
            me.vz = mySpeed.z;
            me.heading = myHeading;
        }
        me.health = std::clamp(static_cast<int>(rawHealth), 0, 200);
        me.armor = std::clamp(static_cast<int>(rawArmor), 0, 200);
        if (localModelId > 0 && localModelId < 300) {
            me.skinModelId = localModelId;
        }
        me.vehicleNetId = myVeh ? 101 : 0;
        me.vehicleModelId = (vehModelId >= 400) ? vehModelId : 492;
        me.seatIndex = myVeh ? 0 : -1;
        me.cameraMode = 0; // Cámara 3D Independiente

        if (config_.isHost) {
            mission_.onMissionFlag = onMission ? 1 : 0;
        } else if (gtaSymbols_.onAMissionFlagOffset && gtaSymbols_.scriptSpace) {
            // Sincronizar flag $ONMISSION del Host al Guest si el Host inició misión
        }

        // Procesar e instanciar jugadores remotos en el mundo 3D de GTA San Andreas
        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            if (i == localIdx) continue;
            PlayerState3D& rem = players_[i];
            bool peerRecentlyActive = (rem.lastNetworkUpdateMs > 0 && (nowMs - rem.lastNetworkUpdateMs) < 20000);
            if (!peerRecentlyActive || !rem.hasRealGtaCoords) continue;

            // Si somos Guest (o al conectar por primera vez) y estamos lejos del compañero (> 50m),
            // teletransportar automáticamente a CJ junto al compañero para que se vean cara a cara de inmediato
            if (!initialSyncTeleportDone_ && gtaSymbols_.cpedTeleportFn) {
                float dx = rem.x - me.x;
                float dy = rem.y - me.y;
                float dz = rem.z - me.z;
                float distSq = dx * dx + dy * dy + dz * dz;
                if (!config_.isHost && distSq > (45.0f * 45.0f) && std::fabs(rem.x) > 1.0f) {
                    CVector meetPos{rem.x + 2.2f, rem.y + 1.5f, rem.z + 0.35f};
                    gtaSymbols_.cpedTeleportFn(localPed, meetPos, 0);
                    me.x = meetPos.x;
                    me.y = meetPos.y;
                    me.z = meetPos.z;
                    initialSyncTeleportDone_ = true;
                    LOGI("Auto-teleported Guest CJ to Host location (%.1f, %.1f, %.1f) for immediate Co-op visibility!",
                         meetPos.x, meetPos.y, meetPos.z);
                } else if (distSq <= (45.0f * 45.0f)) {
                    initialSyncTeleportDone_ = true;
                }
            }

            // Verificar si el CPed remoto sigue válido en CPools::ms_pPedPool
            void* ped = remotePedPtr_[i];
            if (ped && gtaSymbols_.cpoolsGetPedFn && remotePedHandle_[i] > 0) {
                if (gtaSymbols_.cpoolsGetPedFn(remotePedHandle_[i]) != ped) {
                    ped = nullptr;
                    remotePedPtr_[i] = nullptr;
                    remotePedHandle_[i] = -1;
                }
            }

            int desiredModel = rem.skinModelId;
            if (desiredModel <= 0 || desiredModel > 300) {
                desiredModel = (i == 1) ? 270 : 7; // 270 = Sweet, 7 = Male01
            }

            // Solicitar carga del modelo en CStreaming si aún no está cargado
            if (!gtaSymbols_.isModelLoaded(desiredModel) && gtaSymbols_.cstreamingRequestModelFn) {
                gtaSymbols_.cstreamingRequestModelFn(desiredModel, 2);
                if (gtaSymbols_.cstreamingLoadAllRequestedModelsFn) {
                    gtaSymbols_.cstreamingLoadAllRequestedModelsFn(false);
                }
            }

            // Si el CPed remoto aún no fue creado en el mundo 3D, instanciarlo ahora
            if (!ped && gtaSymbols_.cpedOperatorNewFn && gtaSymbols_.ccivilianPedCtorFn && gtaSymbols_.cpedTeleportFn) {
                int spawnModel = gtaSymbols_.isModelLoaded(desiredModel) ? desiredModel : 0; // 0 (CJ) siempre está cargado
                void* newPed = gtaSymbols_.cpedOperatorNewFn(0x9c8);
                if (newPed) {
                    gtaSymbols_.ccivilianPedCtorFn(newPed, 4 /*PED_TYPE_CIVMALE*/, static_cast<uint32_t>(spawnModel));
                    if (gtaSymbols_.cpedSetCharCreatedByFn) {
                        gtaSymbols_.cpedSetCharCreatedByFn(newPed, 2 /*MISSION_CHAR*/);
                    }
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(newPed) + 0x908) = 200.0f;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(newPed) + 0x90c) = 200.0f;

                    CVector spawnPos{rem.x, rem.y, rem.z};
                    gtaSymbols_.cpedTeleportFn(newPed, spawnPos, 0);

                    if (gtaSymbols_.cpedGiveWeaponFn && gtaSymbols_.cpedSetCurrentWeaponFn && rem.weaponId > 0) {
                        gtaSymbols_.cpedGiveWeaponFn(newPed, rem.weaponId, 9999, true);
                        gtaSymbols_.cpedSetCurrentWeaponFn(newPed, rem.weaponId);
                    }

                    int handle = gtaSymbols_.cpoolsGetPedRefFn ? gtaSymbols_.cpoolsGetPedRefFn(newPed) : -1;
                    remotePedPtr_[i] = newPed;
                    remotePedHandle_[i] = handle;
                    remotePedSpawnedModel_[i] = spawnModel;

                    if (handle > 0 && gtaSymbols_.cradarSetEntityBlipFn) {
                        remoteBlipHandle_[i] = gtaSymbols_.cradarSetEntityBlipFn(
                            4 /*BLIP_CHAR*/, handle, 1 /*GREEN*/, 3 /*BLIP_DISPLAY_BOTH*/, nullptr
                        );
                    }

                    ped = newPed;
                    LOGI("Spawned 3D Remote Player P%d (ped=%p, handle=%d, model=%d) at (%.2f, %.2f, %.2f)",
                         i + 1, newPed, handle, spawnModel, spawnPos.x, spawnPos.y, spawnPos.z);
                }
            }

            if (ped) {
                // Actualizar skin si terminó de cargar en CStreaming
                if (remotePedSpawnedModel_[i] != desiredModel &&
                    gtaSymbols_.isModelLoaded(desiredModel) &&
                    gtaSymbols_.cpedSetModelIndexFn) {
                    gtaSymbols_.cpedSetModelIndexFn(ped, static_cast<uint32_t>(desiredModel));
                    remotePedSpawnedModel_[i] = desiredModel;
                }

                // Mantener vivo al actor remoto
                *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x908) = 200.0f;

                // Leer posición actual de la matriz del actor remoto
                void* mat = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ped) + 0x18);
                float curX = rem.x, curY = rem.y, curZ = rem.z;
                if (mat) {
                    curX = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(mat) + 0x30);
                    curY = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(mat) + 0x34);
                    curZ = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(mat) + 0x38);
                }

                float dx = rem.x - curX;
                float dy = rem.y - curY;
                float dz = rem.z - curZ;
                float distSq = dx * dx + dy * dy + dz * dz;

                if (distSq > 16.0f && gtaSymbols_.cpedTeleportFn) {
                    gtaSymbols_.cpedTeleportFn(ped, CVector{rem.x, rem.y, rem.z}, 0);
                } else if (mat) {
                    float nextX = curX + dx * 0.55f;
                    float nextY = curY + dy * 0.55f;
                    float nextZ = curZ + dz * 0.55f;

                    if (gtaSymbols_.cworldRemoveFn) gtaSymbols_.cworldRemoveFn(ped);

                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(mat) + 0x30) = nextX;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(mat) + 0x34) = nextY;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(mat) + 0x38) = nextZ;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x08) = nextX;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x0c) = nextY;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x10) = nextZ;

                    // Velocidad física para suavizar movimiento
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x78) = rem.vx;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x7c) = rem.vy;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x80) = rem.vz;

                    // Rotación Z (heading en radianes)
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x6ac) = rem.heading;
                    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(ped) + 0x6b0) = rem.heading;
                    if (gtaSymbols_.cmatrixSetRotateZOnlyFn) {
                        gtaSymbols_.cmatrixSetRotateZOnlyFn(mat, rem.heading);
                    }
                    if (gtaSymbols_.centityUpdateRwFrameFn) {
                        gtaSymbols_.centityUpdateRwFrameFn(ped);
                    }
                    if (gtaSymbols_.cworldAddFn) gtaSymbols_.cworldAddFn(ped);
                }

                // Dibujar marcador 3D (cono verde brillante) sobre la cabeza del jugador remoto
                if (gtaSymbols_.c3dMarkersPlaceMarkerConeFn) {
                    CVector conePos{rem.x, rem.y, rem.z + 1.35f};
                    gtaSymbols_.c3dMarkersPlaceMarkerConeFn(
                        0xC0090000ULL + static_cast<uint64_t>(i),
                        conePos,
                        0.55f,
                        40, 255, 90, 225,
                        1024, 0.2f, 5, 0
                    );
                }
            }
        }
#endif
    }

    void startNativeUdp6767Thread(int customPort = kCoopAndreasUdpPort) {
        if (udpThreadRunning_.exchange(true)) return;
        config_.udpCoopPort = customPort;

        udpWorkerThread_ = std::thread([this]() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
            int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock < 0) {
                udpThreadRunning_.store(false);
                return;
            }
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 12000; // 12ms poll interval
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            sockaddr_in bindAddr{};
            bindAddr.sin_family = AF_INET;
            bindAddr.sin_addr.s_addr = INADDR_ANY;
            bindAddr.sin_port = htons(static_cast<uint16_t>(config_.udpCoopPort));
            int bindRes = ::bind(sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
            LOGI("UDP 6767 socket bound (res=%d, port=%d)", bindRes, config_.udpCoopPort);

            // Detectar nuestra propia IP Wi-Fi local y dirección de broadcast de subred
            uint32_t myLocalIpNet = detectLocalWifiIpv4NetOrder();
            std::string myLocalIpStr = ipv4NetToString(myLocalIpNet);
            std::string subnetBroadcastStr = "255.255.255.255";
            if (myLocalIpNet != 0) {
                uint32_t hostOrder = ntohl(myLocalIpNet);
                uint32_t bcastNet = htonl((hostOrder & 0xFFFFFF00U) | 0x000000FFU);
                subnetBroadcastStr = ipv4NetToString(bcastNet);
            }
            LOGI("Detected local Wi-Fi IP=%s, SubnetBroadcast=%s", myLocalIpStr.c_str(), subnetBroadcastStr.c_str());

            // Si coop_config.ini ya especificaba una IP de par válida, agregarla a la lista de pares activos
            {
                std::lock_guard<std::mutex> lock(mutex_);
                addConfiguredPeerLocked(myLocalIpNet);
            }

            uint8_t recvBuf[1024];
            uint64_t lastBroadcastMs = 0;
            uint64_t lastUnicastMs = 0;
            uint64_t lastLogWriteMs = 0;
            uint64_t lastIniReloadMs = 0;

            while (udpThreadRunning_.load()) {
                // Asegurar que el hook GOT en el hilo principal esté instalado
                if (!gotHookInstalled_.load()) {
                    installMainThreadGotHook();
                }

                // Drenar todos los paquetes UDP entrantes disponibles en este ciclo
                for (int drain = 0; drain < 8; ++drain) {
                    sockaddr_in peerAddr{};
                    socklen_t peerLen = sizeof(peerAddr);
                    ssize_t n = ::recvfrom(sock, recvBuf, sizeof(recvBuf), 0, reinterpret_cast<sockaddr*>(&peerAddr), &peerLen);
                    if (n < static_cast<ssize_t>(sizeof(BinaryPacketHeader))) break;

                    // Ignorar nuestros propios paquetes de broadcast
                    if (myLocalIpNet != 0 && peerAddr.sin_addr.s_addr == myLocalIpNet) {
                        continue;
                    }
                    if (ntohl(peerAddr.sin_addr.s_addr) == INADDR_LOOPBACK) {
                        continue;
                    }

                    registerIncomingPeer(peerAddr, myLocalIpNet, recvBuf, static_cast<size_t>(n));
                    processIncomingBinaryPacket(recvBuf, static_cast<size_t>(n));
                }

                uint64_t nowMs = currentTimeMillis();

                // Recargar coop_config.ini cada 3 segundos por si la app actualizó rol/IP/sala
                if (nowMs - lastIniReloadMs >= 3000) {
                    lastIniReloadMs = nowMs;
                    loadConfigFromIniFiles(
                        "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/coop_config.ini",
                        "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/SAMP/settings.ini"
                    );
                    std::lock_guard<std::mutex> lock(mutex_);
                    addConfiguredPeerLocked(myLocalIpNet);
                }

                // Enviar paquetes de sincronización 3D a 30 Hz (cada 33 ms) TANTO en HOST como en GUEST
                if (nowMs - lastUnicastMs >= 33) {
                    lastUnicastMs = nowMs;
                    int mySlot = config_.localPlayerSlot;
                    PedSyncPacket pedPkt = buildPedSyncPacket(mySlot);
                    MissionSyncPacket misPkt = buildMissionSyncPacket();

                    std::vector<sockaddr_in> peersCopy;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        for (const auto& pe : activePeers_) {
                            if (nowMs - pe.lastSeenMs < 30000) {
                                peersCopy.push_back(pe.addr);
                            }
                        }
                    }

                    for (const auto& dest : peersCopy) {
                        ::sendto(sock, &pedPkt, sizeof(pedPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        int idx = std::clamp(mySlot, 1, kMaxCoopPlayers) - 1;
                        if (players_[idx].vehicleNetId > 0) {
                            VehicleSyncPacket vehPkt = buildVehicleSyncPacket(mySlot);
                            ::sendto(sock, &vehPkt, sizeof(vehPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        }
                        if (config_.isHost) {
                            ::sendto(sock, &misPkt, sizeof(misPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        }
                    }

                    // También enviar telemetría en texto al puerto 8990 (local y remoto) para la App Android
                    std::string telemStr = serializeTelemetryPacket(mySlot);
                    sendRawUdpPacket(sock, "127.0.0.1", config_.udpStreamPort, telemStr.data(), telemStr.size());
                    for (const auto& dest : peersCopy) {
                        sockaddr_in dest8990 = dest;
                        dest8990.sin_port = htons(static_cast<uint16_t>(config_.udpStreamPort));
                        ::sendto(sock, telemStr.data(), telemStr.size(), 0, reinterpret_cast<const sockaddr*>(&dest8990), sizeof(dest8990));
                    }
                }

                // Broadcast LAN automático cada 500ms en el puerto 6767 para que ambos teléfonos se descubran solos
                if (nowMs - lastBroadcastMs >= 500) {
                    lastBroadcastMs = nowMs;
                    int mySlot = config_.localPlayerSlot;
                    HandshakePacket hs = buildHandshakePacket(mySlot);
                    PedSyncPacket pedPkt = buildPedSyncPacket(mySlot);

                    sendRawUdpPacket(sock, subnetBroadcastStr, config_.udpCoopPort, &hs, sizeof(hs));
                    sendRawUdpPacket(sock, subnetBroadcastStr, config_.udpCoopPort, &pedPkt, sizeof(pedPkt));
                    if (subnetBroadcastStr != "255.255.255.255") {
                        sendRawUdpPacket(sock, "255.255.255.255", config_.udpCoopPort, &hs, sizeof(hs));
                        sendRawUdpPacket(sock, "255.255.255.255", config_.udpCoopPort, &pedPkt, sizeof(pedPkt));
                    }
                }

                // Actualizar coop_telemetry.bin y coop_bridge_live.log cada 600ms
                if (nowMs - lastLogWriteMs >= 600) {
                    lastLogWriteMs = nowMs;
                    syncWithLibGtaSaEngineMemory();
                    writeLiveDiagnosticLog(myLocalIpStr);
                }
            }
            ::close(sock);
#endif
        });
        udpWorkerThread_.detach();
    }

    void stopNativeUdp6767Thread() {
        udpThreadRunning_.store(false);
    }

    bool executeCustomCoopOpcode(uint16_t opcode, int32_t paramSlot = 1, float pX = 0.0f, float pY = 0.0f, float pZ = 0.0f) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(paramSlot, 1, kMaxCoopPlayers) - 1;

        switch (opcode) {
            case OPCODE_COOP_SYNC_LOCAL_PLAYER: {
                players_[idx].cameraMode = 0;
                if (pX != 0.0f || pY != 0.0f) {
                    players_[idx].x = pX;
                    players_[idx].y = pY;
                    players_[idx].z = pZ;
                }
                writeStateToScriptSpaceLocked();
                opcodesExecuted_.fetch_add(1);
                return true;
            }
            case OPCODE_COOP_APPLY_REMOTE_PED_STATE: {
                players_[idx].isActive = true;
                players_[idx].cameraMode = 0;
                writeStateToScriptSpaceLocked();
                opcodesExecuted_.fetch_add(1);
                return true;
            }
            case OPCODE_COOP_SYNC_VEHICLE_DRIVEBY: {
                if (players_[idx].seatIndex > 0) {
                    players_[idx].isDriveByActive = true;
                }
                writeStateToScriptSpaceLocked();
                opcodesExecuted_.fetch_add(1);
                return true;
            }
            case OPCODE_COOP_SYNC_MISSION_CHECKPOINT: {
                writeStateToScriptSpaceLocked();
                opcodesExecuted_.fetch_add(1);
                return true;
            }
            case OPCODE_COOP_SYNC_WEAPON_FIRE: {
                players_[idx].lastAimTarget = CVector{pX, pY, pZ};
                players_[idx].shotsFiredCount++;
                opcodesExecuted_.fetch_add(1);
                return true;
            }
            default:
                return false;
        }
    }

    HandshakePacket buildHandshakePacket(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        HandshakePacket pkt{};
        fillHeaderLocked(pkt.header, PKT_HANDSHAKE, static_cast<uint8_t>(idx + 1));
        pkt.skinModelId = players_[idx].skinModelId;
        std::memcpy(pkt.playerName, players_[idx].playerName, sizeof(pkt.playerName));
        pkt.playerName[sizeof(pkt.playerName) - 1] = '\0';
        pkt.listenUdpPort = static_cast<uint16_t>(config_.udpCoopPort);
        return pkt;
    }

    PedSyncPacket buildPedSyncPacket(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        const PlayerState3D& p = players_[idx];
        PedSyncPacket pkt{};
        fillHeaderLocked(pkt.header, PKT_PED_SYNC, static_cast<uint8_t>(idx + 1));
        pkt.skinModelId = p.skinModelId;
        pkt.position = CVector{p.x, p.y, p.z};
        pkt.velocity = CVector{p.vx, p.vy, p.vz};
        pkt.heading = p.heading;
        pkt.health = p.health;
        pkt.armor = p.armor;
        pkt.currentWeaponId = p.weaponId;
        pkt.ammoInClip = p.ammo;
        pkt.animGroupId = p.animGroupId;
        pkt.animId = p.animId;
        pkt.independentCamera = (p.cameraMode == 0) ? 1 : 0;
        return pkt;
    }

    VehicleSyncPacket buildVehicleSyncPacket(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        const PlayerState3D& p = players_[idx];
        VehicleSyncPacket pkt{};
        fillHeaderLocked(pkt.header, PKT_VEHICLE_SYNC, static_cast<uint8_t>(idx + 1));
        pkt.vehicleNetId = p.vehicleNetId;
        pkt.vehicleModelId = p.vehicleModelId;
        pkt.seatIndex = p.seatIndex;
        pkt.position = CVector{p.x, p.y, p.z};
        pkt.velocity = CVector{p.vx, p.vy, p.vz};
        pkt.heading = p.heading;
        pkt.steerAngle = p.steerAngle;
        pkt.gasPedal = p.gasPedal;
        pkt.brakePedal = p.brakePedal;
        pkt.isDriveByActive = p.isDriveByActive ? 1 : 0;
        return pkt;
    }

    MissionSyncPacket buildMissionSyncPacket() {
        std::lock_guard<std::mutex> lock(mutex_);
        MissionSyncPacket pkt{};
        fillHeaderLocked(pkt.header, PKT_MISSION_SYNC, static_cast<uint8_t>(config_.localPlayerSlot));
        pkt.onMissionFlag = mission_.onMissionFlag;
        std::memcpy(pkt.missionId, mission_.missionId, sizeof(mission_.missionId));
        pkt.missionId[sizeof(mission_.missionId) - 1] = '\0';
        pkt.checkpointIndex = mission_.checkpointIndex;
        pkt.totalCheckpoints = mission_.totalCheckpoints;
        pkt.checkpointPos = CVector{mission_.checkpointX, mission_.checkpointY, mission_.checkpointZ};
        pkt.checkpointRadius = mission_.checkpointRadius;
        return pkt;
    }

    WeaponFirePacket buildWeaponFirePacket(int slot, int weaponId, float targetX, float targetY, float targetZ) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        const PlayerState3D& p = players_[idx];
        WeaponFirePacket pkt{};
        fillHeaderLocked(pkt.header, PKT_WEAPON_FIRE, static_cast<uint8_t>(idx + 1));
        pkt.weaponId = weaponId;
        pkt.muzzleOrigin = CVector{p.x, p.y, p.z + 0.7f};
        pkt.targetImpact = CVector{targetX, targetY, targetZ};
        pkt.damageAmount = 20;
        return pkt;
    }

    bool processIncomingBinaryPacket(const uint8_t* data, size_t len) {
        if (!data || len < sizeof(BinaryPacketHeader)) return false;
        const auto* hdr = reinterpret_cast<const BinaryPacketHeader*>(data);
        if (hdr->magic != 0x504F4F43) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        int slot = std::clamp(static_cast<int>(hdr->playerSlot), 1, kMaxCoopPlayers);
        int idx = slot - 1;
        uint64_t nowMs = currentTimeMillis();

        switch (hdr->packetType) {
            case PKT_HANDSHAKE: {
                if (len < sizeof(HandshakePacket)) return false;
                const auto* hs = reinterpret_cast<const HandshakePacket*>(data);
                players_[idx].playerSlot = slot;
                players_[idx].isActive = true;
                players_[idx].lastNetworkUpdateMs = nowMs;
                if (hs->skinModelId >= 0) players_[idx].skinModelId = hs->skinModelId;
                std::memcpy(players_[idx].playerName, hs->playerName, sizeof(players_[idx].playerName));
                players_[idx].playerName[sizeof(players_[idx].playerName) - 1] = '\0';
                packetsProcessed_.fetch_add(1);
                return true;
            }
            case PKT_PED_SYNC: {
                if (len < sizeof(PedSyncPacket)) return false;
                const auto* ped = reinterpret_cast<const PedSyncPacket*>(data);
                PlayerState3D& p = players_[idx];
                p.playerSlot = slot;
                p.isActive = true;
                p.lastNetworkUpdateMs = nowMs;
                p.hasRealGtaCoords = true;
                if (ped->skinModelId >= 0) p.skinModelId = ped->skinModelId;

                float dx = ped->position.x - p.x;
                float dy = ped->position.y - p.y;
                float dz = ped->position.z - p.z;
                float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq > 0.0001f && distSq < 100.0f) {
                    p.x = p.x + dx * 0.70f;
                    p.y = p.y + dy * 0.70f;
                    p.z = p.z + dz * 0.70f;
                } else {
                    p.x = ped->position.x;
                    p.y = ped->position.y;
                    p.z = ped->position.z;
                }
                p.vx = ped->velocity.x;
                p.vy = ped->velocity.y;
                p.vz = ped->velocity.z;
                p.heading = ped->heading;
                p.health = ped->health;
                p.armor = ped->armor;
                p.weaponId = ped->currentWeaponId;
                p.ammo = ped->ammoInClip;
                p.animGroupId = ped->animGroupId;
                p.animId = ped->animId;
                p.cameraMode = (ped->independentCamera == 1) ? 0 : 1;
                packetsProcessed_.fetch_add(1);
                return true;
            }
            case PKT_VEHICLE_SYNC: {
                if (len < sizeof(VehicleSyncPacket)) return false;
                const auto* veh = reinterpret_cast<const VehicleSyncPacket*>(data);
                PlayerState3D& p = players_[idx];
                p.isActive = true;
                p.lastNetworkUpdateMs = nowMs;
                p.hasRealGtaCoords = true;
                p.vehicleNetId = veh->vehicleNetId;
                p.vehicleModelId = veh->vehicleModelId;
                p.seatIndex = veh->seatIndex;
                p.x = veh->position.x;
                p.y = veh->position.y;
                p.z = veh->position.z;
                p.heading = veh->heading;
                p.steerAngle = veh->steerAngle;
                p.gasPedal = veh->gasPedal;
                p.brakePedal = veh->brakePedal;
                p.isDriveByActive = (veh->isDriveByActive != 0);

                if (veh->vehicleNetId > 0 && veh->seatIndex == 0) {
                    for (int i = 0; i < kMaxCoopPlayers; ++i) {
                        if (i != idx && players_[i].isActive && players_[i].vehicleNetId == veh->vehicleNetId && players_[i].seatIndex > 0) {
                            players_[i].x = veh->position.x;
                            players_[i].y = veh->position.y;
                            players_[i].z = veh->position.z;
                            players_[i].heading = veh->heading;
                        }
                    }
                }
                packetsProcessed_.fetch_add(1);
                return true;
            }
            case PKT_MISSION_SYNC: {
                if (len < sizeof(MissionSyncPacket)) return false;
                const auto* ms = reinterpret_cast<const MissionSyncPacket*>(data);
                mission_.onMissionFlag = ms->onMissionFlag;
                std::memcpy(mission_.missionId, ms->missionId, sizeof(mission_.missionId));
                mission_.missionId[sizeof(mission_.missionId) - 1] = '\0';
                mission_.checkpointIndex = ms->checkpointIndex;
                mission_.totalCheckpoints = ms->totalCheckpoints;
                mission_.checkpointX = ms->checkpointPos.x;
                mission_.checkpointY = ms->checkpointPos.y;
                mission_.checkpointZ = ms->checkpointPos.z;
                mission_.checkpointRadius = ms->checkpointRadius;
                packetsProcessed_.fetch_add(1);
                return true;
            }
            case PKT_WEAPON_FIRE: {
                if (len < sizeof(WeaponFirePacket)) return false;
                const auto* wf = reinterpret_cast<const WeaponFirePacket*>(data);
                PlayerState3D& p = players_[idx];
                p.weaponId = wf->weaponId;
                p.lastAimOrigin = wf->muzzleOrigin;
                p.lastAimTarget = wf->targetImpact;
                p.shotsFiredCount++;
                packetsProcessed_.fetch_add(1);
                return true;
            }
            default:
                return false;
        }
    }

    bool writeTelemetryBinFile(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mutex_);
        SharedTelemetryBinaryFile bin{};
        bin.lastUpdateMs = currentTimeMillis();
        bin.isHost = config_.isHost ? 1 : 0;
        bin.localPlayerSlot = static_cast<uint8_t>(config_.localPlayerSlot);
        bin.maxPlayers = static_cast<uint8_t>(config_.maxPlayers);
        bin.independentCameraMode = (config_.cameraMode == 0) ? 1 : 0;
        std::snprintf(bin.roomCode, sizeof(bin.roomCode), "%s", config_.roomCode.c_str());
        std::snprintf(bin.hostIp, sizeof(bin.hostIp), "%s", config_.hostIp.c_str());

        fillHeaderLocked(bin.missionState.header, PKT_MISSION_SYNC, bin.localPlayerSlot);
        bin.missionState.onMissionFlag = mission_.onMissionFlag;
        std::memcpy(bin.missionState.missionId, mission_.missionId, sizeof(bin.missionState.missionId));
        bin.missionState.missionId[sizeof(bin.missionState.missionId) - 1] = '\0';
        bin.missionState.checkpointIndex = mission_.checkpointIndex;
        bin.missionState.checkpointPos = CVector{mission_.checkpointX, mission_.checkpointY, mission_.checkpointZ};

        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            const PlayerState3D& p = players_[i];
            fillHeaderLocked(bin.playersPed[i].header, PKT_PED_SYNC, static_cast<uint8_t>(i + 1));
            bin.playersPed[i].skinModelId = p.skinModelId;
            bin.playersPed[i].position = CVector{p.x, p.y, p.z};
            bin.playersPed[i].velocity = CVector{p.vx, p.vy, p.vz};
            bin.playersPed[i].heading = p.heading;
            bin.playersPed[i].health = p.health;
            bin.playersPed[i].armor = p.armor;
            bin.playersPed[i].currentWeaponId = p.weaponId;
            bin.playersPed[i].independentCamera = (p.cameraMode == 0) ? 1 : 0;

            fillHeaderLocked(bin.playersVeh[i].header, PKT_VEHICLE_SYNC, static_cast<uint8_t>(i + 1));
            bin.playersVeh[i].vehicleNetId = p.vehicleNetId;
            bin.playersVeh[i].vehicleModelId = p.vehicleModelId;
            bin.playersVeh[i].seatIndex = p.seatIndex;
            bin.playersVeh[i].position = CVector{p.x, p.y, p.z};
            bin.playersVeh[i].isDriveByActive = p.isDriveByActive ? 1 : 0;
        }

        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(&bin), sizeof(bin));
        return out.good();
    }

    bool loadConfigFromIniFiles(const std::string& coopIniPath, const std::string& sampIniPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool loadedAny = false;

        std::ifstream coopFile(coopIniPath);
        if (coopFile.is_open()) {
            loadedAny = true;
            std::string line;
            while (std::getline(coopFile, line)) {
                parseIniLine(line);
            }
        }

        std::ifstream sampFile(sampIniPath);
        if (sampFile.is_open()) {
            loadedAny = true;
            std::string line;
            while (std::getline(sampFile, line)) {
                parseIniLine(line);
            }
        }

        return loadedAny;
    }

    void updateLocalPlayer(
        int slot, int skinId, float x, float y, float z, float heading,
        int health, int armor, int weaponId,
        int vehicleNetId, int vehicleModelId, int seatIndex, bool isDriveBy
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        PlayerState3D& p = players_[idx];

        p.vx = (x - p.x) * 0.5f;
        p.vy = (y - p.y) * 0.5f;
        p.vz = (z - p.z) * 0.5f;

        p.playerSlot = idx + 1;
        p.isActive = true;
        p.hasRealGtaCoords = true;
        p.lastNetworkUpdateMs = currentTimeMillis();
        p.skinModelId = skinId;
        p.x = x;
        p.y = y;
        p.z = z;
        p.heading = heading;
        p.health = std::clamp(health, 0, 200);
        p.armor = std::clamp(armor, 0, 200);
        p.weaponId = weaponId;
        p.vehicleNetId = vehicleNetId;
        p.vehicleModelId = vehicleModelId;
        p.seatIndex = seatIndex;
        p.isDriveByActive = isDriveBy && (seatIndex >= 1);
        p.cameraMode = config_.cameraMode;
        packetsProcessed_.fetch_add(1);
    }

    std::string serializeTelemetryPacket(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        const PlayerState3D& p = players_[idx];

        char buf[512];
        std::snprintf(
            buf, sizeof(buf),
            "COOP_TELEM_V2|%s|%d|%d|%.3f|%.3f|%.3f|%.2f|%.3f|%.3f|%.3f|%d|%d|%d|%d|%d|%d|%d|%d|%d|%s|%d|%.2f|%.2f|%.2f",
            config_.roomCode.c_str(),
            p.playerSlot,
            p.skinModelId,
            p.x, p.y, p.z,
            p.heading,
            p.vx, p.vy, p.vz,
            p.health, p.armor, p.weaponId,
            p.vehicleNetId, p.vehicleModelId, p.seatIndex,
            p.isDriveByActive ? 1 : 0,
            p.cameraMode,
            mission_.onMissionFlag,
            mission_.missionId,
            mission_.checkpointIndex,
            mission_.checkpointX, mission_.checkpointY, mission_.checkpointZ
        );
        return std::string(buf);
    }

    bool parseAndApplyRemoteTelemetry(const std::string& packet) {
        if (packet.rfind("COOP_TELEM_V2|", 0) != 0 && packet.rfind("TELEM|", 0) != 0) {
            return false;
        }

        std::vector<std::string> tokens;
        std::stringstream ss(packet);
        std::string item;
        while (std::getline(ss, item, '|')) {
            tokens.push_back(item);
        }

        if (tokens.size() < 17) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        // Support both 17-token Kotlin format and 25-token C++ format
        bool hasRoomToken = (tokens.size() >= 22);
        int baseIdx = hasRoomToken ? 2 : 1;

        int slot = std::atoi(tokens[baseIdx].c_str());
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        PlayerState3D& p = players_[idx];

        p.playerSlot = idx + 1;
        p.isActive = true;
        p.hasRealGtaCoords = true;
        p.lastNetworkUpdateMs = currentTimeMillis();
        p.skinModelId = std::atoi(tokens[baseIdx + 1].c_str());

        float targetX = std::strtof(tokens[baseIdx + 2].c_str(), nullptr);
        float targetY = std::strtof(tokens[baseIdx + 3].c_str(), nullptr);
        float targetZ = std::strtof(tokens[baseIdx + 4].c_str(), nullptr);

        float distSq = (targetX - p.x) * (targetX - p.x) +
                       (targetY - p.y) * (targetY - p.y) +
                       (targetZ - p.z) * (targetZ - p.z);
        if (distSq > 0.001f && distSq < 400.0f) {
            constexpr float alpha = 0.65f;
            p.x = p.x + (targetX - p.x) * alpha;
            p.y = p.y + (targetY - p.y) * alpha;
            p.z = p.z + (targetZ - p.z) * alpha;
        } else {
            p.x = targetX;
            p.y = targetY;
            p.z = targetZ;
        }

        p.heading = std::strtof(tokens[baseIdx + 5].c_str(), nullptr);
        if (hasRoomToken && tokens.size() >= 24) {
            p.vx = std::strtof(tokens[8].c_str(), nullptr);
            p.vy = std::strtof(tokens[9].c_str(), nullptr);
            p.vz = std::strtof(tokens[10].c_str(), nullptr);
            p.health = std::atoi(tokens[11].c_str());
            p.armor = std::atoi(tokens[12].c_str());
            p.weaponId = std::atoi(tokens[13].c_str());
            p.vehicleNetId = std::atoi(tokens[14].c_str());
            p.vehicleModelId = std::atoi(tokens[15].c_str());
            p.seatIndex = std::atoi(tokens[16].c_str());
            p.isDriveByActive = (std::atoi(tokens[17].c_str()) == 1);
            mission_.onMissionFlag = std::atoi(tokens[19].c_str());
            std::snprintf(mission_.missionId, sizeof(mission_.missionId), "%s", tokens[20].c_str());
            mission_.checkpointIndex = std::atoi(tokens[21].c_str());
        } else {
            p.health = std::atoi(tokens[7].c_str());
            p.armor = std::atoi(tokens[8].c_str());
            p.weaponId = std::atoi(tokens[9].c_str());
            p.vehicleNetId = std::atoi(tokens[10].c_str());
            p.seatIndex = std::atoi(tokens[11].c_str());
            p.isDriveByActive = (std::atoi(tokens[12].c_str()) == 1);
            mission_.onMissionFlag = std::atoi(tokens[14].c_str());
            std::snprintf(mission_.missionId, sizeof(mission_.missionId), "%s", tokens[15].c_str());
            mission_.checkpointIndex = std::atoi(tokens[16].c_str());
        }

        writeStateToScriptSpaceLocked();
        packetsProcessed_.fetch_add(1);
        return true;
    }

    void syncMissionState(int onMissionFlag, const std::string& missionId, int checkpointIndex, float cpX, float cpY, float cpZ) {
        std::lock_guard<std::mutex> lock(mutex_);
        mission_.onMissionFlag = (onMissionFlag != 0) ? 1 : 0;
        std::snprintf(mission_.missionId, sizeof(mission_.missionId), "%s", missionId.c_str());
        mission_.checkpointIndex = std::max(0, checkpointIndex);
        mission_.checkpointX = cpX;
        mission_.checkpointY = cpY;
        mission_.checkpointZ = cpZ;
        writeStateToScriptSpaceLocked();
    }

    void setIndependentCameraMode(bool independent) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.cameraMode = independent ? 0 : 1;
        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            players_[i].cameraMode = config_.cameraMode;
        }
    }

    std::string getPlayerStateJson(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        const PlayerState3D& p = players_[idx];

        char buf[640];
        std::snprintf(
            buf, sizeof(buf),
            "{\"slot\":%d,\"active\":%s,\"skinId\":%d,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,"
            "\"heading\":%.1f,\"health\":%d,\"armor\":%d,\"weaponId\":%d,"
            "\"vehicleNetId\":%d,\"vehicleModelId\":%d,\"seatIndex\":%d,\"isDriveBy\":%s,"
            "\"independentCamera\":%s,\"onMission\":%d,\"missionId\":\"%s\",\"checkpoint\":%d,"
            "\"shotsFired\":%u}",
            p.playerSlot,
            p.isActive ? "true" : "false",
            p.skinModelId,
            p.x, p.y, p.z,
            p.heading,
            p.health, p.armor, p.weaponId,
            p.vehicleNetId, p.vehicleModelId, p.seatIndex,
            p.isDriveByActive ? "true" : "false",
            (p.cameraMode == 0) ? "true" : "false",
            mission_.onMissionFlag,
            mission_.missionId,
            mission_.checkpointIndex,
            p.shotsFiredCount
        );
        return std::string(buf);
    }

    std::string getBridgeSummary() {
        std::lock_guard<std::mutex> lock(mutex_);
        int activeCount = 0;
        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            if (players_[i].isActive) activeCount++;
        }
        int localIdx = std::clamp(config_.localPlayerSlot, 1, kMaxCoopPlayers) - 1;
        int remoteIdx = (localIdx == 0) ? 1 : 0;
        char buf[860];
        std::snprintf(
            buf, sizeof(buf),
            "libCoopAndreasBridge.so v2.3 (Tornamic Port) | Role=%s (Slot %d) | Room=%s | UDP=%d/%d | "
            "Players=%d/%d | Cam=%s | GOTHook=%s (Ticks=%lld) | LocalCJ=(%.1f,%.1f,%.1f) | "
            "RemoteP%d=(%.1f,%.1f,%.1f ped=%p) | $ONMISSION=%d (%s CP#%d) | Opcodes=%lld | Packets=%lld",
            config_.isHost ? "HOST" : "GUEST",
            config_.localPlayerSlot,
            config_.roomCode.c_str(),
            config_.udpCoopPort, config_.udpStreamPort,
            activeCount, config_.maxPlayers,
            (config_.cameraMode == 0) ? "3D_INDEPENDENT" : "SHARED",
            gotHookInstalled_.load() ? "ACTIVE" : "PENDING",
            static_cast<long long>(mainThreadTicks_.load()),
            players_[localIdx].x, players_[localIdx].y, players_[localIdx].z,
            remoteIdx + 1, players_[remoteIdx].x, players_[remoteIdx].y, players_[remoteIdx].z, remotePedPtr_[remoteIdx],
            mission_.onMissionFlag,
            mission_.missionId,
            mission_.checkpointIndex,
            static_cast<long long>(opcodesExecuted_.load()),
            static_cast<long long>(packetsProcessed_.load())
        );
        return std::string(buf);
    }

    void shutdown() {
        stopNativeUdp6767Thread();
        initialized_.store(false);
    }

private:
    CoopEngineBridge() = default;

    static uint64_t currentTimeMillis() {
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    void fillHeaderLocked(BinaryPacketHeader& hdr, PacketType type, uint8_t slot) {
        int idx = std::clamp(static_cast<int>(slot), 1, kMaxCoopPlayers) - 1;
        hdr.magic = 0x504F4F43;
        hdr.version = 2;
        hdr.packetType = type;
        hdr.playerSlot = slot;
        hdr.flags = (config_.isHost ? 0x01 : 0x00) |
                    ((config_.cameraMode == 0) ? 0x02 : 0x00) |
                    ((mission_.onMissionFlag != 0) ? 0x04 : 0x00) |
                    (players_[idx].hasRealGtaCoords ? 0x08 : 0x00);
        std::snprintf(hdr.roomCode, sizeof(hdr.roomCode), "%s", config_.roomCode.c_str());
        hdr.sequenceNumber = static_cast<uint32_t>(packetsProcessed_.load() + 1);
        hdr.timestampMs = currentTimeMillis();
    }

    void syncWithLibGtaSaEngineMemory() {
        writeTelemetryBinFile(config_.telemetryBinPath);
    }

    void writeStateToScriptSpaceLocked() {
        // State is synced directly on the Main Game Thread inside onMainGameThreadTick()
    }

#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
    static uint32_t detectLocalWifiIpv4NetOrder() {
        int tmpSock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (tmpSock < 0) return 0;
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
        uint32_t ipNet = 0;
        if (::connect(tmpSock, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0) {
            sockaddr_in local{};
            socklen_t len = sizeof(local);
            if (::getsockname(tmpSock, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
                ipNet = local.sin_addr.s_addr;
            }
        }
        ::close(tmpSock);
        return ipNet;
    }

    static std::string ipv4NetToString(uint32_t ipNet) {
        if (ipNet == 0) return "0.0.0.0";
        char buf[INET_ADDRSTRLEN] = {0};
        in_addr addr{};
        addr.s_addr = ipNet;
        if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
            return std::string(buf);
        }
        return "0.0.0.0";
    }

    void addConfiguredPeerLocked(uint32_t myLocalIpNet) {
        if (config_.hostIp.empty() || config_.hostIp == "0.0.0.0" || config_.hostIp == "127.0.0.1" || config_.hostIp == "192.168.1.100") {
            return;
        }
        sockaddr_in peer{};
        peer.sin_family = AF_INET;
        peer.sin_port = htons(static_cast<uint16_t>(config_.udpCoopPort));
        if (inet_pton(AF_INET, config_.hostIp.c_str(), &peer.sin_addr) <= 0) return;
        if (myLocalIpNet != 0 && peer.sin_addr.s_addr == myLocalIpNet) return;

        uint64_t nowMs = currentTimeMillis();
        for (auto& existing : activePeers_) {
            if (existing.addr.sin_addr.s_addr == peer.sin_addr.s_addr) {
                existing.lastSeenMs = nowMs;
                return;
            }
        }
        activePeers_.push_back(PeerEndpoint{peer, nowMs});
    }

    void registerIncomingPeer(const sockaddr_in& peerAddr, uint32_t myLocalIpNet, uint8_t* rawBuf, size_t len) {
        if (len < sizeof(BinaryPacketHeader)) return;
        auto* hdr = reinterpret_cast<BinaryPacketHeader*>(rawBuf);
        if (hdr->magic != 0x504F4F43) return;

        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t nowMs = currentTimeMillis();
        bool found = false;
        for (auto& existing : activePeers_) {
            if (existing.addr.sin_addr.s_addr == peerAddr.sin_addr.s_addr) {
                existing.addr.sin_port = htons(static_cast<uint16_t>(config_.udpCoopPort));
                existing.lastSeenMs = nowMs;
                found = true;
                break;
            }
        }
        if (!found) {
            sockaddr_in norm = peerAddr;
            norm.sin_port = htons(static_cast<uint16_t>(config_.udpCoopPort));
            activePeers_.push_back(PeerEndpoint{norm, nowMs});
            std::string peerIpStr = ipv4NetToString(peerAddr.sin_addr.s_addr);
            LOGI("Discovered Co-op Peer on LAN: %s:%d (pktType=%d, slot=%d)",
                 peerIpStr.c_str(), config_.udpCoopPort, hdr->packetType, hdr->playerSlot);
        }

        // Si ambos teléfonos tenían el mismo slot (ej. ambos en Slot 1 por defecto),
        // resolver automáticamente el conflicto comparando las IPs para que uno sea P1 (CJ) y el otro P2 (Sweet)
        if (hdr->playerSlot == static_cast<uint8_t>(config_.localPlayerSlot)) {
            if (myLocalIpNet != 0 && peerAddr.sin_addr.s_addr != myLocalIpNet) {
                bool iHaveLowerIp = (ntohl(myLocalIpNet) < ntohl(peerAddr.sin_addr.s_addr));
                if (iHaveLowerIp) {
                    config_.isHost = true;
                    config_.localPlayerSlot = 1;
                    hdr->playerSlot = 2;
                } else {
                    config_.isHost = false;
                    config_.localPlayerSlot = 2;
                    hdr->playerSlot = 1;
                }
            } else {
                hdr->playerSlot = (config_.localPlayerSlot == 1) ? 2 : 1;
            }
        }
    }

    void writeLiveDiagnosticLog(const std::string& myIp) {
        FILE* logf = fopen("/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/coop_bridge_live.log", "w");
        if (!logf) return;
        std::string summary = getBridgeSummary();
        fprintf(logf, "=== COOPANDREAS NATIVE BRIDGE ACTIVE IN GTA SA ===\nLocalIP: %s\n%s\n", myIp.c_str(), summary.c_str());
        fclose(logf);
    }

    static void sendRawUdpPacket(int sock, const std::string& ip, int port, const void* buf, size_t len) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, ip.c_str(), &dest.sin_addr) <= 0) return;
        ::sendto(sock, buf, len, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    }
#endif

    void parseIniLine(const std::string& rawLine) {
        std::string line = rawLine;
        auto commentPos = line.find(';');
        if (commentPos != std::string::npos) line = line.substr(0, commentPos);
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) return;

        std::string key = trim(line.substr(0, eqPos));
        std::string val = trim(line.substr(eqPos + 1));

        if (key == "Is_Host") config_.isHost = (std::atoi(val.c_str()) == 1);
        else if (key == "Host_IP" || key == "host") config_.hostIp = val;
        else if (key == "Room_Code" || key == "room_code") config_.roomCode = val;
        else if (key == "Coop_UDP_Port" || key == "coop_port") config_.udpCoopPort = std::atoi(val.c_str());
        else if (key == "Port" || key == "port") config_.tcpControlPort = std::atoi(val.c_str());
        else if (key == "Voice_UDP_Port" || key == "voice_port") config_.udpStreamPort = std::atoi(val.c_str());
        else if (key == "Local_Player_Slot") config_.localPlayerSlot = std::clamp(std::atoi(val.c_str()), 1, kMaxCoopPlayers);
        else if (key == "Max_Players" || key == "max_players") config_.maxPlayers = std::clamp(std::atoi(val.c_str()), 2, kMaxCoopPlayers);
        else if (key == "Camera_Mode") config_.cameraMode = std::atoi(val.c_str());
        else if (key == "Allow_Independent_Vehicles") config_.allowIndependentVehicles = (std::atoi(val.c_str()) == 1);
        else if (key == "Free_Roam_Any_Save") mission_.freeRoamAnySave = (std::atoi(val.c_str()) == 1);
        else if (key == "Player1_Skin_ID") players_[0].skinModelId = std::atoi(val.c_str());
        else if (key == "Player2_Skin_ID") players_[1].skinModelId = std::atoi(val.c_str());
        else if (key == "Player3_Skin_ID") players_[2].skinModelId = std::atoi(val.c_str());
        else if (key == "Player4_Skin_ID") players_[3].skinModelId = std::atoi(val.c_str());
    }

    static std::string trim(const std::string& s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, (last - first + 1));
    }

    std::mutex mutex_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> udpThreadRunning_{false};
    std::atomic<bool> gotHookInstalled_{false};
    std::atomic<int64_t> mainThreadTicks_{0};
    std::atomic<int64_t> packetsProcessed_{0};
    std::atomic<int64_t> opcodesExecuted_{0};
    std::thread udpWorkerThread_;
    GtaSaNativeSymbols gtaSymbols_;
    BridgeConfig config_;
    PlayerState3D players_[kMaxCoopPlayers];
    MissionSyncState mission_;

    void* lastLocalPedPtr_ = nullptr;
    bool initialSyncTeleportDone_ = false;
    void* remotePedPtr_[kMaxCoopPlayers] = {nullptr};
    int remotePedHandle_[kMaxCoopPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int remotePedSpawnedModel_[kMaxCoopPlayers] = {0};
    int remoteBlipHandle_[kMaxCoopPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};

#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
    std::vector<PeerEndpoint> activePeers_;
#endif
};

static void Hooked_CWorld_Process() {
    if (g_origCWorldProcess) {
        g_origCWorldProcess();
    }
    CoopEngineBridge::instance().onMainGameThreadTick();
}

} // namespace coopandreas

// ============================================================================
// Auto-Constructor Hook & Official AML Entrypoints for GTA:SA (AML 1.2.4)
// ============================================================================
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
static uint8_t g_amlModInfoBuffer[256] = {0};
static bool g_amlModInfoInitialized = false;

extern "C" JNIEXPORT void* __aml_getmodinfo() {
    if (!g_amlModInfoInitialized) {
        using FnModInfoCtor = void (*)(void*, const char*, const char*, const char*, const char*);
        void* fn = dlsym(RTLD_DEFAULT, "_ZN7ModInfoC2EPKcS1_S1_S1_");
        if (fn) {
            reinterpret_cast<FnModInfoCtor>(fn)(
                g_amlModInfoBuffer,
                "net.coopandreas.bridge",
                "CoopAndreas Native Bridge",
                "2.3.0",
                "CoopAndreas Team"
            );
            g_amlModInfoInitialized = true;
        }
    }
    return g_amlModInfoInitialized ? g_amlModInfoBuffer : nullptr;
}

extern "C" JNIEXPORT void* GetModInfo() {
    return __aml_getmodinfo();
}

extern "C" JNIEXPORT void OnModPreLoad() {
    LOGI("AML OnModPreLoad: CoopAndreas Native Bridge v2.3");
}

extern "C" JNIEXPORT void OnModLoad() {
    LOGI("AML OnModLoad: CoopAndreas Native Bridge v2.3");
}
#endif

__attribute__((constructor))
static void CoopAndreasAutoConstructor() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
    FILE* cmd = fopen("/proc/self/cmdline", "r");
    if (!cmd) return;
    char pkg[128] = {0};
    size_t n = fread(pkg, 1, sizeof(pkg) - 1, cmd);
    fclose(cmd);
    if (n > 0 && strstr(pkg, "com.rockstargames.gtasa") != nullptr) {
        // Pin library in process memory so dlclose() never unmaps our thread
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&CoopAndreasAutoConstructor), &info) && info.dli_fname) {
            dlopen(info.dli_fname, RTLD_LAZY | RTLD_NODELETE);
        }
        std::string pkgStr(pkg);
        std::thread([pkgStr]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            auto& bridge = coopandreas::CoopEngineBridge::instance();
            bridge.initialize(true, "0.0.0.0", "GROVE-404", 1, 4);
            bridge.loadConfigFromIniFiles(
                "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/coop_config.ini",
                "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/SAMP/settings.ini"
            );
            bridge.installMainThreadGotHook();
            bridge.startNativeUdp6767Thread(coopandreas::kCoopAndreasUdpPort);
        }).detach();
    }
#endif
}

// ============================================================================
// Plain C API for Direct Native Testing & CLI Verification
// ============================================================================
extern "C" JNIEXPORT int coop_bridge_c_init(int isHost, const char* hostIp, const char* roomCode, int localSlot, int maxPlayers) {
    return coopandreas::CoopEngineBridge::instance().initialize(
        isHost != 0,
        hostIp ? hostIp : "0.0.0.0",
        roomCode ? roomCode : "GROVE-404",
        localSlot,
        maxPlayers
    ) ? 1 : 0;
}

extern "C" JNIEXPORT void coop_bridge_c_update_player(
    int slot, int skinId, float x, float y, float z, float heading,
    int health, int armor, int weaponId, int vehicleNetId, int vehicleModelId, int seatIndex, int isDriveBy
) {
    coopandreas::CoopEngineBridge::instance().updateLocalPlayer(
        slot, skinId, x, y, z, heading, health, armor, weaponId,
        vehicleNetId, vehicleModelId, seatIndex, isDriveBy != 0
    );
}

extern "C" JNIEXPORT int coop_bridge_c_execute_opcode(uint16_t opcode, int slot, float x, float y, float z) {
    return coopandreas::CoopEngineBridge::instance().executeCustomCoopOpcode(opcode, slot, x, y, z) ? 1 : 0;
}

extern "C" JNIEXPORT int coop_bridge_c_test_binary_packets(int slot) {
    auto& br = coopandreas::CoopEngineBridge::instance();
    auto hs = br.buildHandshakePacket(slot);
    if (!br.processIncomingBinaryPacket(reinterpret_cast<const uint8_t*>(&hs), sizeof(hs))) return 0;

    auto ped = br.buildPedSyncPacket(slot);
    if (!br.processIncomingBinaryPacket(reinterpret_cast<const uint8_t*>(&ped), sizeof(ped))) return 0;

    auto veh = br.buildVehicleSyncPacket(slot);
    if (!br.processIncomingBinaryPacket(reinterpret_cast<const uint8_t*>(&veh), sizeof(veh))) return 0;

    auto mis = br.buildMissionSyncPacket();
    if (!br.processIncomingBinaryPacket(reinterpret_cast<const uint8_t*>(&mis), sizeof(mis))) return 0;

    auto wf = br.buildWeaponFirePacket(slot, 30, 2510.0f, -1680.0f, 14.0f);
    if (!br.processIncomingBinaryPacket(reinterpret_cast<const uint8_t*>(&wf), sizeof(wf))) return 0;

    return 1;
}

extern "C" JNIEXPORT int coop_bridge_c_write_telemetry_bin(const char* path) {
    if (!path) return 0;
    return coopandreas::CoopEngineBridge::instance().writeTelemetryBinFile(path) ? 1 : 0;
}

extern "C" JNIEXPORT int coop_bridge_c_receive_packet(const char* packet) {
    if (!packet) return 0;
    return coopandreas::CoopEngineBridge::instance().parseAndApplyRemoteTelemetry(packet) ? 1 : 0;
}

extern "C" JNIEXPORT void coop_bridge_c_sync_mission(int onMission, const char* missionId, int cpIdx, float cpX, float cpY, float cpZ) {
    coopandreas::CoopEngineBridge::instance().syncMissionState(
        onMission, missionId ? missionId : "ls_02", cpIdx, cpX, cpY, cpZ
    );
}

extern "C" JNIEXPORT void coop_bridge_c_get_player_json(int slot, char* outBuf, int maxLen) {
    if (!outBuf || maxLen <= 0) return;
    std::string json = coopandreas::CoopEngineBridge::instance().getPlayerStateJson(slot);
    std::snprintf(outBuf, static_cast<size_t>(maxLen), "%s", json.c_str());
}

extern "C" JNIEXPORT void coop_bridge_c_build_packet(int slot, char* outBuf, int maxLen) {
    if (!outBuf || maxLen <= 0) return;
    std::string pkt = coopandreas::CoopEngineBridge::instance().serializeTelemetryPacket(slot);
    std::snprintf(outBuf, static_cast<size_t>(maxLen), "%s", pkt.c_str());
}

// ============================================================================
// JNI Entrypoints for Kotlin (com.example.bridge.CoopNativeBridge)
// ============================================================================
#if defined(__ANDROID__) || __has_include(<jni.h>)

static std::string jstringToStdString(JNIEnv* env, jstring jStr) {
    if (!jStr) return "";
    const char* chars = env->GetStringUTFChars(jStr, nullptr);
    if (!chars) return "";
    std::string result(chars);
    env->ReleaseStringUTFChars(jStr, chars);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeInitBridge(
    JNIEnv* env,
    jobject /* thiz */,
    jboolean isHost,
    jstring hostIp,
    jstring roomCode,
    jint localSlot,
    jint maxPlayers
) {
    bool ok = coopandreas::CoopEngineBridge::instance().initialize(
        isHost == JNI_TRUE,
        jstringToStdString(env, hostIp),
        jstringToStdString(env, roomCode),
        static_cast<int>(localSlot),
        static_cast<int>(maxPlayers)
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeLoadIniConfigs(
    JNIEnv* env,
    jobject /* thiz */,
    jstring coopIniPath,
    jstring sampIniPath
) {
    bool ok = coopandreas::CoopEngineBridge::instance().loadConfigFromIniFiles(
        jstringToStdString(env, coopIniPath),
        jstringToStdString(env, sampIniPath)
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeUpdateLocalPlayer(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jint slot,
    jint skinId,
    jfloat x,
    jfloat y,
    jfloat z,
    jfloat heading,
    jint health,
    jint armor,
    jint weaponId,
    jint vehicleNetId,
    jint vehicleModelId,
    jint seatIndex,
    jboolean isDriveBy
) {
    coopandreas::CoopEngineBridge::instance().updateLocalPlayer(
        static_cast<int>(slot),
        static_cast<int>(skinId),
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(z),
        static_cast<float>(heading),
        static_cast<int>(health),
        static_cast<int>(armor),
        static_cast<int>(weaponId),
        static_cast<int>(vehicleNetId),
        static_cast<int>(vehicleModelId),
        static_cast<int>(seatIndex),
        isDriveBy == JNI_TRUE
    );
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeExecuteCustomOpcode(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jint opcode,
    jint slot,
    jfloat x,
    jfloat y,
    jfloat z
) {
    bool ok = coopandreas::CoopEngineBridge::instance().executeCustomCoopOpcode(
        static_cast<uint16_t>(opcode),
        static_cast<int>(slot),
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(z)
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeWriteTelemetryBin(
    JNIEnv* env,
    jobject /* thiz */,
    jstring binFilePath
) {
    bool ok = coopandreas::CoopEngineBridge::instance().writeTelemetryBinFile(
        jstringToStdString(env, binFilePath)
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeBuildTelemetryPacket(
    JNIEnv* env,
    jobject /* thiz */,
    jint slot
) {
    std::string pkt = coopandreas::CoopEngineBridge::instance().serializeTelemetryPacket(static_cast<int>(slot));
    return env->NewStringUTF(pkt.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeReceiveRemotePacket(
    JNIEnv* env,
    jobject /* thiz */,
    jstring packetStr
) {
    bool ok = coopandreas::CoopEngineBridge::instance().parseAndApplyRemoteTelemetry(
        jstringToStdString(env, packetStr)
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeSyncMissionState(
    JNIEnv* env,
    jobject /* thiz */,
    jint onMissionFlag,
    jstring missionId,
    jint checkpointIndex,
    jfloat cpX,
    jfloat cpY,
    jfloat cpZ
) {
    coopandreas::CoopEngineBridge::instance().syncMissionState(
        static_cast<int>(onMissionFlag),
        jstringToStdString(env, missionId),
        static_cast<int>(checkpointIndex),
        static_cast<float>(cpX),
        static_cast<float>(cpY),
        static_cast<float>(cpZ)
    );
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeSetCameraIndependent(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jboolean independent
) {
    coopandreas::CoopEngineBridge::instance().setIndependentCameraMode(independent == JNI_TRUE);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeGetPlayerStateJson(
    JNIEnv* env,
    jobject /* thiz */,
    jint slot
) {
    std::string json = coopandreas::CoopEngineBridge::instance().getPlayerStateJson(static_cast<int>(slot));
    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeGetBridgeDiagnosticSummary(
    JNIEnv* env,
    jobject /* thiz */
) {
    std::string summary = coopandreas::CoopEngineBridge::instance().getBridgeSummary();
    return env->NewStringUTF(summary.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeShutdownBridge(
    JNIEnv* /* env */,
    jobject /* thiz */
) {
    coopandreas::CoopEngineBridge::instance().shutdown();
}

#endif
