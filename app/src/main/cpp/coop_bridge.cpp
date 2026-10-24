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
    PKT_WEAPON_FIRE  = 0x05,
    PKT_WORLD_SYNC   = 0x06
};

constexpr int kMaxSyncedWorldPeds = 6;
constexpr int kMaxSyncedWorldVehicles = 5;

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

struct SyncedWorldPedEntry {
    uint8_t active = 0;
    int16_t hostPoolSlot = -1;
    int16_t modelId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float heading = 0.0f;
    uint8_t moveState = 1; // 1=PEDMOVE_STILL, 4=PEDMOVE_WALK, 6=PEDMOVE_RUN, 7=PEDMOVE_SPRINT
    uint8_t health = 100;
};

struct SyncedWorldVehicleEntry {
    uint8_t active = 0;
    int16_t hostPoolSlot = -1;
    int16_t modelId = 492;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float rightX = 1.0f;
    float rightY = 0.0f;
    float rightZ = 0.0f;
    float upX = 0.0f;
    float upY = 1.0f;
    float upZ = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

struct WorldSyncPacket {
    BinaryPacketHeader header;
    uint8_t clockHours = 12;
    uint8_t clockMinutes = 0;
    uint16_t clockSeconds = 0;
    int16_t oldWeather = 0;
    int16_t newWeather = 0;
    uint8_t numPeds = 0;
    uint8_t numVehicles = 0;
    SyncedWorldPedEntry peds[kMaxSyncedWorldPeds];
    SyncedWorldVehicleEntry vehicles[kMaxSyncedWorldVehicles];
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
    using FnCTaskOperatorNew = void* (*)(size_t size);
    using FnCTaskSimpleStandStillCtor = void* (*)(void* self, int nTime, bool bLooped, bool bUseAnimIdle, float fBlendData);
    using FnCTaskManagerSetTask = void (*)(void* self, void* pTask, int nTaskIndex, bool bForceNewTask);
    using FnCPedSetMoveState = void (*)(void* thisPed, int moveState);
    using FnCPedSetMoveAnim = void (*)(void* thisPed);
    using FnCPopulationRemoveAllRandomPeds = void (*)();
    using FnRpAnimBlendClumpUpdateAnimations = void (*)(void* clump, float timeStep, bool onScreen);
    using FnCCarCtrlCreateCarForScript = void* (*)(int modelId, CVector pos, uint8_t doMissionCleanup);
    using FnCPoolsGetVehicleRef = int (*)(void* veh);
    using FnCPoolsGetVehicle = void* (*)(int handle);
    using FnCWorldClearCarsFromArea = void (*)(float x1, float y1, float z1, float x2, float y2, float z2);
    using FnCCutsceneMgrSkipCutscene = void (*)();

    FnCTaskOperatorNew ctaskOperatorNewFn = nullptr;
    FnCTaskSimpleStandStillCtor ctaskSimpleStandStillCtorFn = nullptr;
    FnCTaskManagerSetTask ctaskManagerSetTaskFn = nullptr;
    FnCPedSetMoveState cpedSetMoveStateFn = nullptr;
    FnCPedSetMoveAnim cpedSetMoveAnimFn = nullptr;
    FnCPopulationRemoveAllRandomPeds cpopulationRemoveAllRandomPedsFn = nullptr;
    FnRpAnimBlendClumpUpdateAnimations rpAnimBlendClumpUpdateAnimationsFn = nullptr;
    FnCCarCtrlCreateCarForScript ccarCtrlCreateCarForScriptFn = nullptr;
    FnCPoolsGetVehicleRef cpoolsGetVehicleRefFn = nullptr;
    FnCPoolsGetVehicle cpoolsGetVehicleFn = nullptr;
    FnCWorldClearCarsFromArea cworldClearCarsFromAreaFn = nullptr;
    FnCCutsceneMgrSkipCutscene ccutsceneMgrSkipCutsceneFn = nullptr;

    uint8_t* cutsceneRunning = nullptr;             // _ZN12CCutsceneMgr10ms_runningE
    uint8_t* bPlayersCanBeInSeparateCars = nullptr; // _ZN10CGameLogic27bPlayersCanBeInSeparateCarsE
    uint8_t* clockHours = nullptr;                  // _ZN6CClock18ms_nGameClockHoursE
    uint8_t* clockMinutes = nullptr;                // _ZN6CClock20ms_nGameClockMinutesE
    uint16_t* clockSeconds = nullptr;               // _ZN6CClock20ms_nGameClockSecondsE
    int16_t* oldWeatherType = nullptr;              // _ZN8CWeather14OldWeatherTypeE
    int16_t* newWeatherType = nullptr;              // _ZN8CWeather14NewWeatherTypeE
    uint8_t* dontCreateRandomCops = nullptr;        // _ZN11CPopulation23m_bDontCreateRandomCopsE
    uint8_t* dontCreateRandomGangMembers = nullptr; // _ZN11CPopulation30m_bDontCreateRandomGangMembersE
    float* pedDensityMultiplier = nullptr;          // _ZN11CPopulation20PedDensityMultiplierE
    int32_t* maxNumberOfPedsInUse = nullptr;        // _ZN11CPopulation20MaxNumberOfPedsInUseE
    float* carDensityMultiplier = nullptr;          // _ZN8CCarCtrl20CarDensityMultiplierE
    int32_t* maxNumberOfCarsInUse = nullptr;        // _ZN8CCarCtrl20MaxNumberOfCarsInUseE

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
        ctaskOperatorNewFn = reinterpret_cast<FnCTaskOperatorNew>(dlsym(libHandle, "_ZN5CTasknwEm"));
        ctaskSimpleStandStillCtorFn = reinterpret_cast<FnCTaskSimpleStandStillCtor>(dlsym(libHandle, "_ZN21CTaskSimpleStandStillC1Eibbf"));
        ctaskManagerSetTaskFn = reinterpret_cast<FnCTaskManagerSetTask>(dlsym(libHandle, "_ZN12CTaskManager7SetTaskEP5CTaskib"));
        cpedSetMoveStateFn = reinterpret_cast<FnCPedSetMoveState>(dlsym(libHandle, "_ZN4CPed12SetMoveStateE10eMoveState"));
        cpedSetMoveAnimFn = reinterpret_cast<FnCPedSetMoveAnim>(dlsym(libHandle, "_ZN4CPed11SetMoveAnimEv"));
        cpopulationRemoveAllRandomPedsFn = reinterpret_cast<FnCPopulationRemoveAllRandomPeds>(dlsym(libHandle, "_ZN11CPopulation19RemoveAllRandomPedsEv"));
        rpAnimBlendClumpUpdateAnimationsFn = reinterpret_cast<FnRpAnimBlendClumpUpdateAnimations>(dlsym(libHandle, "_Z32RpAnimBlendClumpUpdateAnimationsP7RpClumpfb"));
        ccarCtrlCreateCarForScriptFn = reinterpret_cast<FnCCarCtrlCreateCarForScript>(dlsym(libHandle, "_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh"));
        cpoolsGetVehicleRefFn = reinterpret_cast<FnCPoolsGetVehicleRef>(dlsym(libHandle, "_ZN6CPools13GetVehicleRefEP8CVehicle"));
        cpoolsGetVehicleFn = reinterpret_cast<FnCPoolsGetVehicle>(dlsym(libHandle, "_ZN6CPools10GetVehicleEi"));
        cworldClearCarsFromAreaFn = reinterpret_cast<FnCWorldClearCarsFromArea>(dlsym(libHandle, "_ZN6CWorld17ClearCarsFromAreaEffffff"));
        ccutsceneMgrSkipCutsceneFn = reinterpret_cast<FnCCutsceneMgrSkipCutscene>(dlsym(libHandle, "_ZN12CCutsceneMgr12SkipCutsceneEv"));
        cutsceneRunning = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN12CCutsceneMgr10ms_runningE"));

        bPlayersCanBeInSeparateCars = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN10CGameLogic27bPlayersCanBeInSeparateCarsE"));
        clockHours = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN6CClock18ms_nGameClockHoursE"));
        clockMinutes = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN6CClock20ms_nGameClockMinutesE"));
        clockSeconds = reinterpret_cast<uint16_t*>(dlsym(libHandle, "_ZN6CClock20ms_nGameClockSecondsE"));
        oldWeatherType = reinterpret_cast<int16_t*>(dlsym(libHandle, "_ZN8CWeather14OldWeatherTypeE"));
        newWeatherType = reinterpret_cast<int16_t*>(dlsym(libHandle, "_ZN8CWeather14NewWeatherTypeE"));
        dontCreateRandomCops = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN11CPopulation23m_bDontCreateRandomCopsE"));
        dontCreateRandomGangMembers = reinterpret_cast<uint8_t*>(dlsym(libHandle, "_ZN11CPopulation30m_bDontCreateRandomGangMembersE"));
        pedDensityMultiplier = reinterpret_cast<float*>(dlsym(libHandle, "_ZN11CPopulation20PedDensityMultiplierE"));
        maxNumberOfPedsInUse = reinterpret_cast<int32_t*>(dlsym(libHandle, "_ZN11CPopulation20MaxNumberOfPedsInUseE"));
        carDensityMultiplier = reinterpret_cast<float*>(dlsym(libHandle, "_ZN8CCarCtrl20CarDensityMultiplierE"));
        maxNumberOfCarsInUse = reinterpret_cast<int32_t*>(dlsym(libHandle, "_ZN8CCarCtrl20MaxNumberOfCarsInUseE"));

        if (cworldProcessFn) {
            Dl_info info{};
            if (dladdr(reinterpret_cast<void*>(cworldProcessFn), &info) && info.dli_fbase) {
                libBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
            }
        }

        allCoreResolved = (cworldPlayers != nullptr && scriptSpace != nullptr && processOneCommandFn != nullptr);
        LOGI("libGTASA.so resolved (base=0x%lx): CWorld::Players=%p, PedPool=%p, VehPool=%p, CreateCar=%p, ClearCars=%p, UpdateAnims=%p",
             static_cast<unsigned long>(libBase), cworldPlayers, cpoolsPedPool, cpoolsVehiclePool,
             reinterpret_cast<void*>(ccarCtrlCreateCarForScriptFn),
             reinterpret_cast<void*>(cworldClearCarsFromAreaFn),
             reinterpret_cast<void*>(rpAnimBlendClumpUpdateAnimationsFn));
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

struct WorldSyncState {
    uint64_t lastNetworkUpdateMs = 0;
    uint8_t clockHours = 12;
    uint8_t clockMinutes = 0;
    uint16_t clockSeconds = 0;
    int16_t oldWeather = 0;
    int16_t newWeather = 0;
    uint8_t numPeds = 0;
    uint8_t numVehicles = 0;
    SyncedWorldPedEntry peds[kMaxSyncedWorldPeds];
    SyncedWorldVehicleEntry vehicles[kMaxSyncedWorldVehicles];
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

        // 0. En libGTASA.so v2.10 arm64, CTaskSimpleStandStill::ProcessPed (0x5ef6ec) llama a
        // CPed::SetMoveState(ped, 1) en 0x5ef7f0 en cada frame subsiguiente, sobrescribiendo el
        // estado de caminar/correr de los actores sincronizados antes de RpAnimBlendClumpUpdateAnimations.
        // Al parchear esa única llamada secundaria (manteniendo la inicial en 0x5ef77c intacta),
        // CPed::ProcessControl mantiene y avanza las animaciones de caminar/correr fluidamente.
        if (gtaSymbols_.libBase != 0) {
            uintptr_t standStillResetAddr = gtaSymbols_.libBase + 0x5ef7f0;
            uintptr_t textPage = standStillResetAddr & ~static_cast<uintptr_t>(0xFFF);
            if (::mprotect(reinterpret_cast<void*>(textPage), 4096, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
                uint32_t* insn = reinterpret_cast<uint32_t*>(standStillResetAddr);
                if (*insn == 0x97f10700U) {
                    *insn = 0xd503201fU; // NOP
                    __builtin___clear_cache(reinterpret_cast<char*>(insn), reinterpret_cast<char*>(insn + 1));
                    LOGI("Patched CTaskSimpleStandStill::ProcessPed per-tick SetMoveState reset at 0x%lx",
                         static_cast<unsigned long>(standStillResetAddr));
                }
            }
        }

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
    // HELPER: ACTUALIZAR POSICIÓN, ROTACIÓN Y ANIMACIÓN DE UN CPED SIN BORRAR VELOCIDAD
    // ========================================================================
    void applySmoothPedTransformAndAnim(
        void* ped, float targetX, float targetY, float targetZ,
        float vx, float vy, float vz, float heading, int moveState, float health
    ) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!ped) return;
        uintptr_t pedAddr = reinterpret_cast<uintptr_t>(ped);

        // Mantener vivo y de pie al actor (+0x6ac = m_fHealth, +0x54c = m_nPedState, +0x584 bit 0 = bIsStanding)
        *reinterpret_cast<float*>(pedAddr + 0x6ac) = std::max(health, 100.0f);
        if (*reinterpret_cast<int32_t*>(pedAddr + 0x54c) != 1) {
            *reinterpret_cast<int32_t*>(pedAddr + 0x54c) = 1; // PEDSTATE_IDLE
        }
        *reinterpret_cast<uint8_t*>(pedAddr + 0x584) |= 0x01; // bIsStanding = true

        // Limpiar cualquier tarea primaria de caída/colisión/daño (slots 0..3 en m_pIntelligence->m_TaskMgr)
        // que tumbe al actor en el suelo, y garantizar que CTaskSimpleStandStill siga en el slot 4
        uintptr_t pedIntel = *reinterpret_cast<uintptr_t*>(pedAddr + 0x538);
        if (pedIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
            for (int slot = 0; slot < 4; ++slot) {
                void* activeOverrideTask = *reinterpret_cast<void**>(pedIntel + 0x08 + static_cast<size_t>(slot) * 8);
                if (activeOverrideTask != nullptr) {
                    gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(pedIntel + 8), nullptr, slot, false);
                }
            }
            void* defaultTask = *reinterpret_cast<void**>(pedIntel + 0x28);
            if (defaultTask == nullptr && gtaSymbols_.ctaskOperatorNewFn && gtaSymbols_.ctaskSimpleStandStillCtorFn) {
                void* task = gtaSymbols_.ctaskOperatorNewFn(0x28);
                if (task) {
                    gtaSymbols_.ctaskSimpleStandStillCtorFn(task, 999999, true, false, 8.0f);
                    gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(pedIntel + 8), task, 4, false);
                }
            }
        }

        uintptr_t mat = *reinterpret_cast<uintptr_t*>(pedAddr + 0x18);
        float curX = targetX, curY = targetY, curZ = targetZ;
        if (mat != 0) {
            curX = *reinterpret_cast<float*>(mat + 0x30);
            curY = *reinterpret_cast<float*>(mat + 0x34);
            curZ = *reinterpret_cast<float*>(mat + 0x38);
        }

        float dx = targetX - curX;
        float dy = targetY - curY;
        float dz = targetZ - curZ;
        float distSq = dx * dx + dy * dy + dz * dz;

        // Inferir estado de movimiento (1=STILL, 4=WALK, 6=RUN, 7=SPRINT) si el paquete no lo especifica o si hay desplazamiento
        float speed2d = std::sqrt(vx * vx + vy * vy);
        int effectiveMoveState = moveState;
        if (effectiveMoveState != 4 && effectiveMoveState != 6 && effectiveMoveState != 7) {
            if (speed2d > 0.14f) effectiveMoveState = 7;      // PEDMOVE_SPRINT
            else if (speed2d > 0.05f) effectiveMoveState = 6; // PEDMOVE_RUN
            else if (speed2d > 0.008f || (distSq > 0.0004f && distSq < 25.0f)) effectiveMoveState = 4; // PEDMOVE_WALK
            else effectiveMoveState = 1;                      // PEDMOVE_STILL
        }

        if (mat != 0 && gtaSymbols_.cworldRemoveFn && gtaSymbols_.cworldAddFn) {
            // Actualizar matriz y posición directamente sin invocar CPed::Teleport
            // (ya que CPed::Teleport llama a CPedIntelligence::FlushImmediately y apaga bIsStanding)
            float nextX = (distSq > 25.0f) ? targetX : (curX + dx * 0.65f);
            float nextY = (distSq > 25.0f) ? targetY : (curY + dy * 0.65f);
            float nextZ = (distSq > 25.0f) ? targetZ : (curZ + dz * 0.65f);

            gtaSymbols_.cworldRemoveFn(ped);

            *reinterpret_cast<float*>(mat + 0x30) = nextX;
            *reinterpret_cast<float*>(mat + 0x34) = nextY;
            *reinterpret_cast<float*>(mat + 0x38) = nextZ;
            *reinterpret_cast<float*>(pedAddr + 0x08) = nextX;
            *reinterpret_cast<float*>(pedAddr + 0x0c) = nextY;
            *reinterpret_cast<float*>(pedAddr + 0x10) = nextZ;

            if (std::isfinite(heading)) {
                float c = std::cos(heading);
                float s = std::sin(heading);
                *reinterpret_cast<float*>(mat + 0x00) = c;
                *reinterpret_cast<float*>(mat + 0x04) = s;
                *reinterpret_cast<float*>(mat + 0x08) = 0.0f;
                *reinterpret_cast<float*>(mat + 0x10) = -s;
                *reinterpret_cast<float*>(mat + 0x14) = c;
                *reinterpret_cast<float*>(mat + 0x18) = 0.0f;
                // Actualizar m_fCurrentRotation (+0x6c4) y m_fAimingRotation (+0x6c8)
                *reinterpret_cast<float*>(pedAddr + 0x6c4) = heading;
                *reinterpret_cast<float*>(pedAddr + 0x6c8) = heading;
            }

            *reinterpret_cast<float*>(pedAddr + 0x68) = vx;
            *reinterpret_cast<float*>(pedAddr + 0x6c) = vy;
            *reinterpret_cast<float*>(pedAddr + 0x70) = vz;
            *reinterpret_cast<uint8_t*>(pedAddr + 0x584) |= 0x01; // bIsStanding = true

            if (gtaSymbols_.centityUpdateRwFrameFn) {
                gtaSymbols_.centityUpdateRwFrameFn(ped);
            }
            gtaSymbols_.cworldAddFn(ped);
        }

        // CRÍTICO: En libGTASA.so arm64 v2.10, CPed::SetMoveAnim (0x596cb4) lee m_fMoveAnimRate (+0x688)
        // en 0x596fec y lo escribe en CAnimBlendAssociation::m_fSpeed (+0x3c) en 0x59707c.
        // En un CCivilianPed sin pad local, +0x688 vale 0.0f por defecto, congelando la animación en el frame 0.
        *reinterpret_cast<float*>(pedAddr + 0x688) = (effectiveMoveState > 1) ? 1.0f : 0.0f;

        if (gtaSymbols_.cpedSetMoveStateFn) {
            gtaSymbols_.cpedSetMoveStateFn(ped, effectiveMoveState);
        } else {
            *reinterpret_cast<int32_t*>(pedAddr + 0x550) = effectiveMoveState;
        }
        if (gtaSymbols_.cpedSetMoveAnimFn) {
            gtaSymbols_.cpedSetMoveAnimFn(ped);
        }
        if (effectiveMoveState > 1 && gtaSymbols_.rpAnimBlendClumpUpdateAnimationsFn) {
            void* clump = *reinterpret_cast<void**>(pedAddr + 0x20);
            if (clump) {
                gtaSymbols_.rpAnimBlendClumpUpdateAnimationsFn(clump, 0.016f, true);
            }
        }
#endif
    }

    // ========================================================================
    // HELPER: ACTUALIZAR TRANSFORMACIÓN 3D Y VELOCIDAD DE UN CVEHICLE SINCRONIZADO
    // ========================================================================
    void applySmoothVehicleTransform(void* veh, const SyncedWorldVehicleEntry& ve) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!veh) return;
        uintptr_t vehAddr = reinterpret_cast<uintptr_t>(veh);
        uintptr_t mat = *reinterpret_cast<uintptr_t*>(vehAddr + 0x18);
        if (mat == 0 || !gtaSymbols_.cworldRemoveFn || !gtaSymbols_.cworldAddFn) return;

        float curX = *reinterpret_cast<float*>(mat + 0x30);
        float curY = *reinterpret_cast<float*>(mat + 0x34);
        float curZ = *reinterpret_cast<float*>(mat + 0x38);
        float dx = ve.x - curX;
        float dy = ve.y - curY;
        float dz = ve.z - curZ;
        float distSq = dx * dx + dy * dy + dz * dz;

        float nextX = (distSq > 36.0f) ? ve.x : (curX + dx * 0.65f);
        float nextY = (distSq > 36.0f) ? ve.y : (curY + dy * 0.65f);
        float nextZ = (distSq > 36.0f) ? ve.z : (curZ + dz * 0.65f);

        gtaSymbols_.cworldRemoveFn(veh);

        float rx = ve.rightX, ry = ve.rightY, rz = ve.rightZ;
        float ux = ve.upX, uy = ve.upY, uz = ve.upZ;
        // at = cross(right, up)
        float ax = ry * uz - rz * uy;
        float ay = rz * ux - rx * uz;
        float az = rx * uy - ry * ux;

        *reinterpret_cast<float*>(mat + 0x00) = rx;
        *reinterpret_cast<float*>(mat + 0x04) = ry;
        *reinterpret_cast<float*>(mat + 0x08) = rz;
        *reinterpret_cast<float*>(mat + 0x10) = ux;
        *reinterpret_cast<float*>(mat + 0x14) = uy;
        *reinterpret_cast<float*>(mat + 0x18) = uz;
        *reinterpret_cast<float*>(mat + 0x20) = ax;
        *reinterpret_cast<float*>(mat + 0x24) = ay;
        *reinterpret_cast<float*>(mat + 0x28) = az;
        *reinterpret_cast<float*>(mat + 0x30) = nextX;
        *reinterpret_cast<float*>(mat + 0x34) = nextY;
        *reinterpret_cast<float*>(mat + 0x38) = nextZ;

        *reinterpret_cast<float*>(vehAddr + 0x08) = nextX;
        *reinterpret_cast<float*>(vehAddr + 0x0c) = nextY;
        *reinterpret_cast<float*>(vehAddr + 0x10) = nextZ;
        *reinterpret_cast<float*>(vehAddr + 0x68) = ve.vx;
        *reinterpret_cast<float*>(vehAddr + 0x6c) = ve.vy;
        *reinterpret_cast<float*>(vehAddr + 0x70) = ve.vz;

        if (gtaSymbols_.centityUpdateRwFrameFn) {
            gtaSymbols_.centityUpdateRwFrameFn(veh);
        }
        gtaSymbols_.cworldAddFn(veh);
#endif
    }

    // ========================================================================
    // EJECUTADO EN EL HILO PRINCIPAL DE GTA SA (CADA FRAME DE CWorld::Process)
    // ========================================================================
    void onMainGameThreadTick() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        int64_t tickCount = mainThreadTicks_.fetch_add(1) + 1;

        // No intervenir mientras una cinemática oficial esté en reproducción
        if (gtaSymbols_.cutsceneRunning && *gtaSymbols_.cutsceneRunning != 0) {
            return;
        }

        if (!gtaSymbols_.findPlayerPedFn || !gtaSymbols_.findPlayerCoorsFn) return;

        void* localPed = gtaSymbols_.findPlayerPedFn(-1);
        if (!localPed) {
            // El jugador aún está en el menú principal o pantalla de carga
            lastLocalPedPtr_ = nullptr;
            for (int i = 0; i < kMaxCoopPlayers; ++i) {
                remotePedPtr_[i] = nullptr;
                remotePedHandle_[i] = -1;
            }
            for (int k = 0; k < kMaxSyncedWorldPeds; ++k) {
                worldSyncPedPtr_[k] = nullptr;
                worldSyncPedHandle_[k] = -1;
                worldSyncPedHostSlot_[k] = -1;
                worldSyncPedModel_[k] = -1;
            }
            for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                worldSyncVehPtr_[v] = nullptr;
                worldSyncVehHandle_[v] = -1;
                worldSyncVehHostSlot_[v] = -1;
                worldSyncVehModel_[v] = -1;
            }
            return;
        }

        if (localPed != lastLocalPedPtr_) {
            // Se acaba de cargar una partida o reiniciar misión
            lastLocalPedPtr_ = localPed;
            initialSyncTeleportDone_ = false;
            initialAlleyFreeRoamDone_ = false;
            alleyLandWaitTicks_ = 0;
            for (int i = 0; i < kMaxCoopPlayers; ++i) {
                remotePedPtr_[i] = nullptr;
                remotePedHandle_[i] = -1;
            }
            for (int k = 0; k < kMaxSyncedWorldPeds; ++k) {
                worldSyncPedPtr_[k] = nullptr;
                worldSyncPedHandle_[k] = -1;
                worldSyncPedHostSlot_[k] = -1;
                worldSyncPedModel_[k] = -1;
            }
            for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                worldSyncVehPtr_[v] = nullptr;
                worldSyncVehHandle_[v] = -1;
                worldSyncVehHostSlot_[v] = -1;
                worldSyncVehModel_[v] = -1;
            }
            LOGI("Local Player CJ detected in world: CPlayerPed=%p", localPed);
        }

        // Forzar cámara 3D independiente y vehículos separados permitidos
        if (gtaSymbols_.bPlayersCanBeInSeparateCars) {
            *gtaSymbols_.bPlayersCanBeInSeparateCars = 1;
        }

        // Leer coordenadas 3D reales, rotación, velocidad, estado de animación y salud directamente del CPlayerPed/CVehicle de 64 bits
        uintptr_t localPedAddr = reinterpret_cast<uintptr_t>(localPed);
        uint8_t pedFlags = *reinterpret_cast<uint8_t*>(localPedAddr + 0x585);
        void* myVeh = ((pedFlags & 1) != 0)
            ? *reinterpret_cast<void**>(localPedAddr + 0x708)
            : nullptr;
        uintptr_t activeEntityAddr = reinterpret_cast<uintptr_t>(myVeh ? myVeh : localPed);

        uintptr_t localMat = *reinterpret_cast<uintptr_t*>(activeEntityAddr + 0x18);
        uintptr_t posPtr = (localMat != 0) ? (localMat + 0x30) : (activeEntityAddr + 0x08);
        CVector myPos{
            *reinterpret_cast<float*>(posPtr + 0x00),
            *reinterpret_cast<float*>(posPtr + 0x04),
            *reinterpret_cast<float*>(posPtr + 0x08)
        };

        // Mientras el motor mantiene a CJ en las coordenadas internas de espera (2495.0, -1687.0, 13.5)
        // durante la cinemática de apertura del aeropuerto/patrulla, no intervenir ni spawnear entidades
        bool inIntroHoldingSpot = (std::fabs(myPos.x - 2495.0f) < 2.5f &&
                                   std::fabs(myPos.y - (-1687.0f)) < 2.5f &&
                                   tickCount < 2500);
        if (inIntroHoldingSpot) {
            return;
        }

        float myHeading = *reinterpret_cast<float*>(localPedAddr + 0x6c4); // m_fCurrentRotation
        if (myVeh && localMat != 0) {
            float upX = *reinterpret_cast<float*>(localMat + 0x10);
            float upY = *reinterpret_cast<float*>(localMat + 0x14);
            myHeading = std::atan2(-upX, upY);
        }

        CVector mySpeed{
            *reinterpret_cast<float*>(activeEntityAddr + 0x68),
            *reinterpret_cast<float*>(activeEntityAddr + 0x6c),
            *reinterpret_cast<float*>(activeEntityAddr + 0x70)
        };

        int32_t localMoveState = *reinterpret_cast<int32_t*>(localPedAddr + 0x550);
        float mySpeed2d = std::sqrt(mySpeed.x * mySpeed.x + mySpeed.y * mySpeed.y);
        if (localMoveState != 4 && localMoveState != 6 && localMoveState != 7) {
            if (mySpeed2d > 0.14f) localMoveState = 7;
            else if (mySpeed2d > 0.05f) localMoveState = 6;
            else if (mySpeed2d > 0.008f) localMoveState = 4;
            else localMoveState = 1;
        }
        bool onMission = gtaSymbols_.isPlayerOnAMissionFn ? gtaSymbols_.isPlayerOnAMissionFn() : false;

        // En libGTASA.so arm64 v2.10: m_fHealth = +0x6ac, m_fMaxHealth = +0x6b0, m_fArmour = +0x6b4
        float rawHealth = *reinterpret_cast<float*>(localPedAddr + 0x6ac);
        float rawArmor = *reinterpret_cast<float*>(localPedAddr + 0x6b4);

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
            me.vx = std::isfinite(mySpeed.x) ? mySpeed.x : 0.0f;
            me.vy = std::isfinite(mySpeed.y) ? mySpeed.y : 0.0f;
            me.vz = std::isfinite(mySpeed.z) ? mySpeed.z : 0.0f;
            me.heading = std::isfinite(myHeading) ? myHeading : 0.0f;
        }
        me.health = std::isfinite(rawHealth) ? std::clamp(static_cast<int>(rawHealth), 0, 200) : 100;
        me.armor = std::isfinite(rawArmor) ? std::clamp(static_cast<int>(rawArmor), 0, 200) : 0;
        me.animId = static_cast<uint16_t>(std::clamp(localMoveState, 0, 12));
        me.vehicleNetId = myVeh ? 101 : 0;
        // En libGTASA.so arm64 v2.10, CEntity::m_nModelIndex (int16_t) está en +0x32 (verificado en CEntity::SetModelIndexNoCreate 0x4ca7e0)
        me.vehicleModelId = myVeh ? static_cast<int32_t>(*reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(myVeh) + 0x32)) : 492;
        me.seatIndex = myVeh ? 0 : -1;
        me.cameraMode = 0; // Cámara 3D Independiente

        // ====================================================================
        // SINCRONIZACIÓN DE RELOJ (HORA), CLIMA, PEATONES Y VEHÍCULOS DEL MUNDO (HOST <-> GUEST)
        // ====================================================================
        // Al salir de la cinemática inicial en el callejón (970.9, -1108.1), activar modo Free Roam ($ONMISSION = 0)
        // y dar 3.5m de espacio respecto a la pared de ladrillos (X=972.0) colocando al Host en (967.2, -1107.8)
        // y al Guest a su izquierda en el patio abierto (967.2, -1105.9)
        if (!initialAlleyFreeRoamDone_ && std::fabs(me.x - 970.9f) < 6.0f && std::fabs(me.y - (-1108.1f)) < 6.0f) {
            if (++alleyLandWaitTicks_ >= 20) {
                if (gtaSymbols_.onAMissionFlagOffset) {
                    *gtaSymbols_.onAMissionFlagOffset = 0;
                }
                onMission = false;
                float targetX = 967.2f;
                float targetY = config_.isHost ? -1107.8f : -1105.9f;
                float targetZ = 23.98f;
                applySmoothPedTransformAndAnim(localPed, targetX, targetY, targetZ, 0.0f, 0.0f, 0.0f, myHeading, 1, 100.0f);
                me.x = targetX;
                me.y = targetY;
                me.z = targetZ;
                initialAlleyFreeRoamDone_ = true;
                initialSyncTeleportDone_ = true;
                LOGI("Positioned %s CJ in open alley courtyard at (%.1f, %.1f, %.1f) facing +X!",
                     config_.isHost ? "HOST" : "GUEST", targetX, targetY, targetZ);
            }
        }

        if (config_.isHost) {
            if (gtaSymbols_.dontCreateRandomCops) *gtaSymbols_.dontCreateRandomCops = 0;
            if (gtaSymbols_.dontCreateRandomGangMembers) *gtaSymbols_.dontCreateRandomGangMembers = 0;
            if (gtaSymbols_.pedDensityMultiplier) *gtaSymbols_.pedDensityMultiplier = 2.0f;
            if (gtaSymbols_.maxNumberOfPedsInUse) *gtaSymbols_.maxNumberOfPedsInUse = 28;
            if (gtaSymbols_.carDensityMultiplier) *gtaSymbols_.carDensityMultiplier = 2.0f;
            if (gtaSymbols_.maxNumberOfCarsInUse) *gtaSymbols_.maxNumberOfCarsInUse = 16;

            mission_.onMissionFlag = onMission ? 1 : 0;
            if (gtaSymbols_.clockHours) worldState_.clockHours = *gtaSymbols_.clockHours;
            if (gtaSymbols_.clockMinutes) worldState_.clockMinutes = *gtaSymbols_.clockMinutes;
            if (gtaSymbols_.clockSeconds) worldState_.clockSeconds = *gtaSymbols_.clockSeconds;
            if (gtaSymbols_.oldWeatherType) worldState_.oldWeather = *gtaSymbols_.oldWeatherType;
            if (gtaSymbols_.newWeatherType) worldState_.newWeather = *gtaSymbols_.newWeatherType;

            bool inInitialCourtyard = (std::fabs(me.x - 967.2f) < 10.0f && std::fabs(me.y - (-1107.8f)) < 10.0f);

            // Escanear CPools::ms_pPedPool y CPools::ms_pVehiclePool cada 2 frames para replicar peatones y vehículos reales del Host al Guest
            if (tickCount % 2 == 0) {
                if (gtaSymbols_.cpoolsPedPool) {
                    void* poolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsPedPool);
                    if (poolPtr) {
                        uintptr_t poolAddr = reinterpret_cast<uintptr_t>(poolPtr);
                        uint8_t* objects = *reinterpret_cast<uint8_t**>(poolAddr + 0x00);
                        uint8_t* byteMap = *reinterpret_cast<uint8_t**>(poolAddr + 0x08);
                        int32_t poolSize = *reinterpret_cast<int32_t*>(poolAddr + 0x10);

                        struct CandidatePed {
                            float distSq;
                            void* rawPtr;
                            SyncedWorldPedEntry entry;
                        };
                        CandidatePed best[kMaxSyncedWorldPeds];
                        int foundCount = 0;

                        if (objects && byteMap && poolSize > 0 && poolSize <= 256) {
                            for (int32_t s = 0; s < poolSize; ++s) {
                                if ((byteMap[s] & 0x80) != 0) continue; // Slot libre
                                void* pPed = reinterpret_cast<void*>(objects + static_cast<size_t>(s) * 0x9c8);
                                if (pPed == localPed) continue;
                                bool isRemotePlayerPed = false;
                                for (int r = 0; r < kMaxCoopPlayers; ++r) {
                                    if (pPed == remotePedPtr_[r]) {
                                        isRemotePlayerPed = true;
                                        break;
                                    }
                                }
                                if (isRemotePlayerPed) continue;

                                uintptr_t pAddr = reinterpret_cast<uintptr_t>(pPed);
                                // Excluir peatones dentro de vehículos y excluir modelo 0 (CJ); +0x32 = CEntity::m_nModelIndex en arm64
                                if ((*reinterpret_cast<uint8_t*>(pAddr + 0x585) & 1) != 0) continue;
                                int16_t pModelId = *reinterpret_cast<int16_t*>(pAddr + 0x32);
                                if (pModelId <= 0 || pModelId > 300) continue;

                                float pHealth = *reinterpret_cast<float*>(pAddr + 0x6ac);
                                if (!std::isfinite(pHealth) || pHealth <= 0.0f) continue;

                                uintptr_t pMat = *reinterpret_cast<uintptr_t*>(pAddr + 0x18);
                                if (pMat == 0) continue;
                                float px = *reinterpret_cast<float*>(pMat + 0x30);
                                float py = *reinterpret_cast<float*>(pMat + 0x34);
                                float pz = *reinterpret_cast<float*>(pMat + 0x38);
                                if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) continue;

                                float dx = px - me.x;
                                float dy = py - me.y;
                                float dz = pz - me.z;
                                float dSq = dx * dx + dy * dy + dz * dz;
                                if (dSq > (90.0f * 90.0f)) continue;

                                SyncedWorldPedEntry e{};
                                e.active = 1;
                                e.hostPoolSlot = static_cast<int16_t>(s);
                                e.modelId = pModelId;
                                e.x = px;
                                e.y = py;
                                e.z = pz;
                                e.vx = *reinterpret_cast<float*>(pAddr + 0x68);
                                e.vy = *reinterpret_cast<float*>(pAddr + 0x6c);
                                e.vz = *reinterpret_cast<float*>(pAddr + 0x70);
                                e.heading = *reinterpret_cast<float*>(pAddr + 0x6c4);
                                int32_t ms = *reinterpret_cast<int32_t*>(pAddr + 0x550);
                                e.moveState = static_cast<uint8_t>(std::clamp(ms, 1, 7));
                                e.health = static_cast<uint8_t>(std::clamp(static_cast<int>(pHealth), 1, 200));

                                if (foundCount < kMaxSyncedWorldPeds) {
                                    best[foundCount++] = CandidatePed{dSq, pPed, e};
                                } else {
                                    int worstIdx = 0;
                                    for (int w = 1; w < kMaxSyncedWorldPeds; ++w) {
                                        if (best[w].distSq > best[worstIdx].distSq) worstIdx = w;
                                    }
                                    if (dSq < best[worstIdx].distSq) {
                                        best[worstIdx] = CandidatePed{dSq, pPed, e};
                                    }
                                }
                            }
                        }

                        // Si ambos jugadores están en el patio inicial (967.2, -1107.8), acercar los 2 primeros peatones
                        // del pool real del Host frente a la cámara (969.6, -1107.2) y (969.6, -1105.0) para que sean
                        // inmediatamente visibles y verificables en pantalla tanto en Host como en Guest
                        if (inInitialCourtyard) {
                            if (foundCount >= 1 && best[0].rawPtr) {
                                best[0].entry.x = 969.6f;
                                best[0].entry.y = -1107.2f;
                                best[0].entry.z = 23.98f;
                                best[0].entry.vx = 0.0f;
                                best[0].entry.vy = 0.0f;
                                best[0].entry.vz = 0.0f;
                                best[0].entry.heading = 1.57f;
                                best[0].entry.moveState = 1;
                                applySmoothPedTransformAndAnim(best[0].rawPtr, 969.6f, -1107.2f, 23.98f, 0.0f, 0.0f, 0.0f, 1.57f, 1, 100.0f);
                            }
                            if (foundCount >= 2 && best[1].rawPtr) {
                                best[1].entry.x = 969.6f;
                                best[1].entry.y = -1105.0f;
                                best[1].entry.z = 23.98f;
                                best[1].entry.vx = 0.0f;
                                best[1].entry.vy = 0.0f;
                                best[1].entry.vz = 0.0f;
                                best[1].entry.heading = 1.57f;
                                best[1].entry.moveState = 1;
                                applySmoothPedTransformAndAnim(best[1].rawPtr, 969.6f, -1105.0f, 23.98f, 0.0f, 0.0f, 0.0f, 1.57f, 1, 100.0f);
                            }
                        }

                        worldState_.numPeds = static_cast<uint8_t>(foundCount);
                        for (int k = 0; k < kMaxSyncedWorldPeds; ++k) {
                            if (k < foundCount) {
                                worldState_.peds[k] = best[k].entry;
                            } else {
                                worldState_.peds[k].active = 0;
                                worldState_.peds[k].hostPoolSlot = -1;
                            }
                        }
                    }
                }

                if (gtaSymbols_.cpoolsVehiclePool) {
                    void* vPoolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsVehiclePool);
                    if (vPoolPtr) {
                        uintptr_t vPoolAddr = reinterpret_cast<uintptr_t>(vPoolPtr);
                        uint8_t* vObjects = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x00);
                        uint8_t* vByteMap = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x08);
                        int32_t vPoolSize = *reinterpret_cast<int32_t*>(vPoolAddr + 0x10);

                        struct CandidateVeh {
                            float distSq;
                            void* rawPtr;
                            SyncedWorldVehicleEntry entry;
                        };
                        CandidateVeh bestVeh[kMaxSyncedWorldVehicles];
                        int foundVehCount = 0;

                        if (vObjects && vByteMap && vPoolSize > 0 && vPoolSize <= 256) {
                            for (int32_t s = 0; s < vPoolSize; ++s) {
                                if ((vByteMap[s] & 0x80) != 0) continue; // Slot libre
                                void* pVeh = reinterpret_cast<void*>(vObjects + static_cast<size_t>(s) * 0xc68);
                                uintptr_t vAddr = reinterpret_cast<uintptr_t>(pVeh);
                                // +0x32 = CEntity::m_nModelIndex en arm64 v2.10
                                int16_t vModel = *reinterpret_cast<int16_t*>(vAddr + 0x32);
                                if (vModel < 400 || vModel > 611) continue;

                                uintptr_t vMat = *reinterpret_cast<uintptr_t*>(vAddr + 0x18);
                                if (vMat == 0) continue;
                                float vxPos = *reinterpret_cast<float*>(vMat + 0x30);
                                float vyPos = *reinterpret_cast<float*>(vMat + 0x34);
                                float vzPos = *reinterpret_cast<float*>(vMat + 0x38);
                                if (!std::isfinite(vxPos) || !std::isfinite(vyPos) || !std::isfinite(vzPos)) continue;

                                float dx = vxPos - me.x;
                                float dy = vyPos - me.y;
                                float dz = vzPos - me.z;
                                float dSq = dx * dx + dy * dy + dz * dz;
                                if (dSq > (130.0f * 130.0f)) continue;

                                SyncedWorldVehicleEntry ve{};
                                ve.active = 1;
                                ve.hostPoolSlot = static_cast<int16_t>(s);
                                ve.modelId = vModel;
                                ve.x = vxPos;
                                ve.y = vyPos;
                                ve.z = vzPos;
                                ve.rightX = *reinterpret_cast<float*>(vMat + 0x00);
                                ve.rightY = *reinterpret_cast<float*>(vMat + 0x04);
                                ve.rightZ = *reinterpret_cast<float*>(vMat + 0x08);
                                ve.upX = *reinterpret_cast<float*>(vMat + 0x10);
                                ve.upY = *reinterpret_cast<float*>(vMat + 0x14);
                                ve.upZ = *reinterpret_cast<float*>(vMat + 0x18);
                                ve.vx = *reinterpret_cast<float*>(vAddr + 0x68);
                                ve.vy = *reinterpret_cast<float*>(vAddr + 0x6c);
                                ve.vz = *reinterpret_cast<float*>(vAddr + 0x70);

                                if (foundVehCount < kMaxSyncedWorldVehicles) {
                                    bestVeh[foundVehCount++] = CandidateVeh{dSq, pVeh, ve};
                                } else {
                                    int worstIdx = 0;
                                    for (int w = 1; w < kMaxSyncedWorldVehicles; ++w) {
                                        if (bestVeh[w].distSq > bestVeh[worstIdx].distSq) worstIdx = w;
                                    }
                                    if (dSq < bestVeh[worstIdx].distSq) {
                                        bestVeh[worstIdx] = CandidateVeh{dSq, pVeh, ve};
                                    }
                                }
                            }
                        }

                        // En el patio inicial, situar un automóvil real del pool en (968.8, -1101.2, 23.98) a la izquierda
                        // de ambos jugadores en el espacio abierto para que el mismo auto sea visible en ambas pantallas
                        if (inInitialCourtyard && foundVehCount >= 1) {
                            int carIdx = 0;
                            for (int c = 0; c < foundVehCount; ++c) {
                                if (bestVeh[c].entry.modelId != 481 && bestVeh[c].entry.modelId != 509 && bestVeh[c].entry.modelId != 510) {
                                    carIdx = c;
                                    break;
                                }
                            }
                            if (carIdx != 0) {
                                std::swap(bestVeh[0], bestVeh[carIdx]);
                            }
                            if (bestVeh[0].rawPtr && bestVeh[0].rawPtr != myVeh) {
                                bestVeh[0].entry.x = 968.8f;
                                bestVeh[0].entry.y = -1101.2f;
                                bestVeh[0].entry.z = 23.98f;
                                bestVeh[0].entry.rightX = 0.0f;
                                bestVeh[0].entry.rightY = 1.0f;
                                bestVeh[0].entry.rightZ = 0.0f;
                                bestVeh[0].entry.upX = -1.0f;
                                bestVeh[0].entry.upY = 0.0f;
                                bestVeh[0].entry.upZ = 0.0f;
                                bestVeh[0].entry.vx = 0.0f;
                                bestVeh[0].entry.vy = 0.0f;
                                bestVeh[0].entry.vz = 0.0f;
                                applySmoothVehicleTransform(bestVeh[0].rawPtr, bestVeh[0].entry);
                            }
                        }

                        worldState_.numVehicles = static_cast<uint8_t>(foundVehCount);
                        for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                            if (v < foundVehCount) {
                                worldState_.vehicles[v] = bestVeh[v].entry;
                            } else {
                                worldState_.vehicles[v].active = 0;
                                worldState_.vehicles[v].hostPoolSlot = -1;
                            }
                        }
                    }
                }
            }
        } else if (worldState_.lastNetworkUpdateMs > 0 && (nowMs - worldState_.lastNetworkUpdateMs) < 20000) {
            // Aplicar hora exacta y clima del Host en el Guest
            if (gtaSymbols_.clockHours) *gtaSymbols_.clockHours = worldState_.clockHours;
            if (gtaSymbols_.clockMinutes) *gtaSymbols_.clockMinutes = worldState_.clockMinutes;
            if (gtaSymbols_.clockSeconds) *gtaSymbols_.clockSeconds = worldState_.clockSeconds;
            if (gtaSymbols_.oldWeatherType) *gtaSymbols_.oldWeatherType = worldState_.oldWeather;
            if (gtaSymbols_.newWeatherType) *gtaSymbols_.newWeatherType = worldState_.newWeather;

            // Desactivar toda generación aleatoria no sincronizada de peatones y autos en el Guest
            if (gtaSymbols_.dontCreateRandomCops) *gtaSymbols_.dontCreateRandomCops = 1;
            if (gtaSymbols_.dontCreateRandomGangMembers) *gtaSymbols_.dontCreateRandomGangMembers = 1;
            if (gtaSymbols_.pedDensityMultiplier) *gtaSymbols_.pedDensityMultiplier = 0.0f;
            if (gtaSymbols_.maxNumberOfPedsInUse) *gtaSymbols_.maxNumberOfPedsInUse = 0;
            if (gtaSymbols_.carDensityMultiplier) *gtaSymbols_.carDensityMultiplier = 0.0f;
            if (gtaSymbols_.maxNumberOfCarsInUse) *gtaSymbols_.maxNumberOfCarsInUse = 0;

            // IMPORTANTE: NUNCA invocar CStreaming::LoadAllRequestedModels desde CWorld::Process para evitar
            // condición de carrera con CdStreamThread (OS_FileSetPosition). CStreaming::Update carga los modelos
            // solicitados por RequestModel de forma asíncrona y segura en el siguiente frame.
            bool canRequestStreamingThisTick = (tickCount % 30 == 0);

            // 1. Instanciar y actualizar en el Guest los peatones reales sincronizados desde el Host (NUNCA modelo 0 / CJ)
            for (int k = 0; k < kMaxSyncedWorldPeds; ++k) {
                const SyncedWorldPedEntry& wp = worldState_.peds[k];
                void* wPed = worldSyncPedPtr_[k];
                if (wPed && gtaSymbols_.cpoolsGetPedFn && worldSyncPedHandle_[k] > 0) {
                    if (gtaSymbols_.cpoolsGetPedFn(worldSyncPedHandle_[k]) != wPed) {
                        wPed = nullptr;
                        worldSyncPedPtr_[k] = nullptr;
                        worldSyncPedHandle_[k] = -1;
                        worldSyncPedModel_[k] = -1;
                    }
                }

                bool validPedEntry = (wp.active != 0 && wp.modelId > 0 && wp.modelId <= 300);
                bool exactPedModelLoaded = validPedEntry && gtaSymbols_.isModelLoaded(wp.modelId);
                if (validPedEntry && !exactPedModelLoaded && canRequestStreamingThisTick && gtaSymbols_.cstreamingRequestModelFn) {
                    gtaSymbols_.cstreamingRequestModelFn(wp.modelId, 2);
                }

                if (!validPedEntry || (wPed && exactPedModelLoaded && worldSyncPedModel_[k] != wp.modelId)) {
                    if (wPed) {
                        if (gtaSymbols_.cpedSetCharCreatedByFn) {
                            gtaSymbols_.cpedSetCharCreatedByFn(wPed, 1 /*RANDOM_CHAR*/);
                        }
                        if (gtaSymbols_.cpedTeleportFn) {
                            gtaSymbols_.cpedTeleportFn(wPed, CVector{me.x, me.y, -150.0f}, 0);
                        }
                        wPed = nullptr;
                        worldSyncPedPtr_[k] = nullptr;
                        worldSyncPedHandle_[k] = -1;
                        worldSyncPedModel_[k] = -1;
                    }
                    if (!validPedEntry) continue;
                }

                if (!wPed) {
                    int16_t spawnPedModel = wp.modelId;
                    if (!exactPedModelLoaded) {
                        spawnPedModel = -1;
                        for (int16_t m = 7; m <= 288; ++m) {
                            if (gtaSymbols_.isModelLoaded(m)) {
                                spawnPedModel = m;
                                break;
                            }
                        }
                        if (spawnPedModel <= 0) continue;
                    }
                    if (gtaSymbols_.cpedOperatorNewFn && gtaSymbols_.ccivilianPedCtorFn && gtaSymbols_.cworldAddFn) {
                        void* newWPed = gtaSymbols_.cpedOperatorNewFn(0x9c8);
                        if (newWPed) {
                            gtaSymbols_.ccivilianPedCtorFn(newWPed, 4 /*PED_TYPE_CIVMALE*/, static_cast<uint32_t>(spawnPedModel));
                            uintptr_t wAddr = reinterpret_cast<uintptr_t>(newWPed);
                            uintptr_t wIntel = *reinterpret_cast<uintptr_t*>(wAddr + 0x538);
                            if (wIntel != 0 && gtaSymbols_.ctaskOperatorNewFn &&
                                gtaSymbols_.ctaskSimpleStandStillCtorFn && gtaSymbols_.ctaskManagerSetTaskFn) {
                                void* task = gtaSymbols_.ctaskOperatorNewFn(0x28);
                                if (task) {
                                    gtaSymbols_.ctaskSimpleStandStillCtorFn(task, 999999, true, false, 8.0f);
                                    gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(wIntel + 8), task, 4, false);
                                }
                            }
                            if (gtaSymbols_.cpedSetCharCreatedByFn) {
                                gtaSymbols_.cpedSetCharCreatedByFn(newWPed, 2 /*MISSION_CHAR so RemoveAllRandomPeds preserves it*/);
                            }
                            *reinterpret_cast<float*>(wAddr + 0x6ac) = 100.0f;
                            *reinterpret_cast<float*>(wAddr + 0x6b0) = 100.0f;
                            uintptr_t wMat = *reinterpret_cast<uintptr_t*>(wAddr + 0x18);
                            if (wMat != 0) {
                                *reinterpret_cast<float*>(wMat + 0x30) = wp.x;
                                *reinterpret_cast<float*>(wMat + 0x34) = wp.y;
                                *reinterpret_cast<float*>(wMat + 0x38) = wp.z;
                            }
                            *reinterpret_cast<float*>(wAddr + 0x08) = wp.x;
                            *reinterpret_cast<float*>(wAddr + 0x0c) = wp.y;
                            *reinterpret_cast<float*>(wAddr + 0x10) = wp.z;
                            gtaSymbols_.cworldAddFn(newWPed);
                            worldSyncPedPtr_[k] = newWPed;
                            worldSyncPedHandle_[k] = gtaSymbols_.cpoolsGetPedRefFn ? gtaSymbols_.cpoolsGetPedRefFn(newWPed) : -1;
                            worldSyncPedHostSlot_[k] = wp.hostPoolSlot;
                            worldSyncPedModel_[k] = spawnPedModel;
                            wPed = newWPed;
                        }
                    }
                }

                if (wPed) {
                    worldSyncPedHostSlot_[k] = wp.hostPoolSlot;
                    applySmoothPedTransformAndAnim(
                        wPed, wp.x, wp.y, wp.z,
                        wp.vx, wp.vy, wp.vz, wp.heading,
                        static_cast<int>(wp.moveState), static_cast<float>(wp.health)
                    );
                }
            }

            // 2. Instanciar y actualizar en el Guest los vehículos sincronizados desde el Host
            static const int16_t kSafeFallbackCars[] = {
                400, 401, 404, 405, 410, 411, 412, 415, 419, 420, 421, 422, 426, 436, 439, 445,
                458, 466, 467, 474, 475, 479, 480, 491, 492, 496, 507, 516, 517, 518, 526, 527,
                529, 533, 534, 535, 536, 540, 541, 542, 545, 546, 547, 549, 550, 551, 558, 559,
                560, 561, 562, 565, 566, 567, 575, 576, 579, 580, 585, 587, 589, 596, 597, 600, 602, 603
            };
            for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                const SyncedWorldVehicleEntry& ve = worldState_.vehicles[v];
                void* wVeh = worldSyncVehPtr_[v];
                if (wVeh && gtaSymbols_.cpoolsGetVehicleFn && worldSyncVehHandle_[v] > 0) {
                    if (gtaSymbols_.cpoolsGetVehicleFn(worldSyncVehHandle_[v]) != wVeh) {
                        wVeh = nullptr;
                        worldSyncVehPtr_[v] = nullptr;
                        worldSyncVehHandle_[v] = -1;
                        worldSyncVehModel_[v] = -1;
                    }
                }

                bool validVehEntry = (ve.active != 0 && ve.modelId >= 400 && ve.modelId <= 611);
                bool exactVehModelLoaded = validVehEntry && gtaSymbols_.isModelLoaded(ve.modelId);
                if (validVehEntry && !exactVehModelLoaded && canRequestStreamingThisTick && gtaSymbols_.cstreamingRequestModelFn) {
                    gtaSymbols_.cstreamingRequestModelFn(ve.modelId, 2);
                }

                if (!validVehEntry || (wVeh && exactVehModelLoaded && worldSyncVehModel_[v] != ve.modelId && wVeh != myVeh)) {
                    if (wVeh && wVeh != myVeh) {
                        *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(wVeh) + 0x610) = 1;
                        wVeh = nullptr;
                        worldSyncVehPtr_[v] = nullptr;
                        worldSyncVehHandle_[v] = -1;
                        worldSyncVehModel_[v] = -1;
                    }
                    if (!validVehEntry) continue;
                }

                if (!wVeh) {
                    int16_t spawnVehModel = ve.modelId;
                    if (!exactVehModelLoaded) {
                        spawnVehModel = -1;
                        for (int16_t candidate : kSafeFallbackCars) {
                            if (gtaSymbols_.isModelLoaded(candidate)) {
                                spawnVehModel = candidate;
                                break;
                            }
                        }
                        if (spawnVehModel < 400) continue;
                    }
                    if (gtaSymbols_.ccarCtrlCreateCarForScriptFn) {
                        void* newVeh = gtaSymbols_.ccarCtrlCreateCarForScriptFn(spawnVehModel, CVector{ve.x, ve.y, ve.z}, 0);
                        if (newVeh) {
                            // Marcar m_nVehicleCreatedBy (+0x610) = 2 (MISSION_VEHICLE) para que ClearCarsFromArea lo preserve
                            *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(newVeh) + 0x610) = 2;
                            worldSyncVehPtr_[v] = newVeh;
                            worldSyncVehHandle_[v] = gtaSymbols_.cpoolsGetVehicleRefFn ? gtaSymbols_.cpoolsGetVehicleRefFn(newVeh) : -1;
                            worldSyncVehHostSlot_[v] = ve.hostPoolSlot;
                            worldSyncVehModel_[v] = spawnVehModel;
                            wVeh = newVeh;
                        }
                    }
                }

                if (wVeh && wVeh != myVeh) {
                    applySmoothVehicleTransform(wVeh, ve);
                }
            }

            // Limpiar cada 15 frames cualquier peatón o vehículo aleatorio local no sincronizado en el Guest
            if (tickCount % 15 == 0) {
                if (gtaSymbols_.cpopulationRemoveAllRandomPedsFn) {
                    gtaSymbols_.cpopulationRemoveAllRandomPedsFn();
                }
                if (gtaSymbols_.cworldClearCarsFromAreaFn) {
                    gtaSymbols_.cworldClearCarsFromAreaFn(
                        me.x - 300.0f, me.y - 300.0f, -100.0f,
                        me.x + 300.0f, me.y + 300.0f, 500.0f
                    );
                }
            }
        }

        // Esperar unos ticks tras salir de cualquier cinemática antes de spawnear actores remotos
        if (tickCount < 80) return;

        // Procesar e instanciar jugadores remotos en el mundo 3D de GTA San Andreas
        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            if (i == localIdx) continue;
            PlayerState3D& rem = players_[i];
            bool peerRecentlyActive = (rem.lastNetworkUpdateMs > 0 && (nowMs - rem.lastNetworkUpdateMs) < 20000);
            if (!peerRecentlyActive || !rem.hasRealGtaCoords) continue;

            // Si somos Guest (o al conectar por primera vez):
            // - Si estamos lejos del Host (> 45m), teletransportar a Guest junto al Host (+1.85m en Y = a la izquierda en espacio abierto)
            // - Si ambos spawnearon exactamente en el mismo punto (< 1.5m), separar a Guest +1.85m en Y (misma X)
            //   para que ambos estén de pie lado a lado frente a la cámara y nunca dentro de la pared (+X) ni colisionando
            if (!initialSyncTeleportDone_ && gtaSymbols_.cpedTeleportFn) {
                float dx = rem.x - me.x;
                float dy = rem.y - me.y;
                float dz = rem.z - me.z;
                float distSq = dx * dx + dy * dy + dz * dz;
                if (!config_.isHost && distSq > (45.0f * 45.0f) && std::fabs(rem.x) > 1.0f) {
                    CVector meetPos{rem.x, rem.y + 1.85f, rem.z + 0.15f};
                    gtaSymbols_.cpedTeleportFn(localPed, meetPos, 0);
                    me.x = meetPos.x;
                    me.y = meetPos.y;
                    me.z = meetPos.z;
                    initialSyncTeleportDone_ = true;
                    LOGI("Auto-teleported Guest CJ to Host location (%.1f, %.1f, %.1f) for immediate Co-op visibility!",
                         meetPos.x, meetPos.y, meetPos.z);
                } else if (!config_.isHost && distSq < (1.5f * 1.5f) && std::fabs(rem.x) > 1.0f) {
                    CVector sideBySidePos{rem.x, rem.y + 1.85f, rem.z};
                    gtaSymbols_.cpedTeleportFn(localPed, sideBySidePos, 0);
                    me.x = sideBySidePos.x;
                    me.y = sideBySidePos.y;
                    me.z = sideBySidePos.z;
                    initialSyncTeleportDone_ = true;
                    LOGI("Separated Guest CJ side-by-side at (%.1f, %.1f, %.1f) to prevent spawn collision!",
                         sideBySidePos.x, sideBySidePos.y, sideBySidePos.z);
                } else if (distSq <= (45.0f * 45.0f)) {
                    initialSyncTeleportDone_ = true;
                }
            }

            // Calcular posición objetivo del actor remoto garantizando separación mínima de 1.45m respecto al jugador local
            // en el eje Y (izquierda/derecha en pantalla), nunca hacia +X (pared frontal del callejón)
            float targetRemX = rem.x;
            float targetRemY = rem.y;
            float targetRemZ = rem.z;
            float sepDx = targetRemX - me.x;
            float sepDy = targetRemY - me.y;
            float sepDist2d = std::sqrt(sepDx * sepDx + sepDy * sepDy);
            if (sepDist2d < 1.45f) {
                if (sepDist2d > 0.05f) {
                    float scale = 1.45f / sepDist2d;
                    targetRemX = me.x + sepDx * scale;
                    targetRemY = me.y + sepDy * scale;
                } else {
                    targetRemX = me.x;
                    targetRemY = me.y + (config_.isHost ? 1.85f : -1.85f);
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

            // Si el CPed remoto aún no fue creado en el mundo 3D, instanciarlo siguiendo la secuencia exacta de CREATE_CHAR (009A)
            if (!ped && gtaSymbols_.cpedOperatorNewFn && gtaSymbols_.ccivilianPedCtorFn && gtaSymbols_.cworldAddFn) {
                // Modelo 0 (CJ) siempre está cargado en memoria en GTA SA; evita cualquier bloqueo de CStreaming
                uint32_t spawnModel = 0;
                void* newPed = gtaSymbols_.cpedOperatorNewFn(0x9c8);
                if (newPed) {
                    gtaSymbols_.ccivilianPedCtorFn(newPed, 4 /*PED_TYPE_CIVMALE*/, spawnModel);
                    uintptr_t newPedAddr = reinterpret_cast<uintptr_t>(newPed);

                    // Inicializar CTaskSimpleStandStill en m_pIntelligence->m_TaskMgr (slot 4 = TASK_PRIMARY_DEFAULT)
                    // tal como lo hace CREATE_CHAR (0x3f611c..0x3f6154) para que CPedIntelligence::Process no crashee
                    uintptr_t pedIntel = *reinterpret_cast<uintptr_t*>(newPedAddr + 0x538);
                    if (pedIntel != 0 && gtaSymbols_.ctaskOperatorNewFn &&
                        gtaSymbols_.ctaskSimpleStandStillCtorFn && gtaSymbols_.ctaskManagerSetTaskFn) {
                        void* task = gtaSymbols_.ctaskOperatorNewFn(0x28);
                        if (task) {
                            gtaSymbols_.ctaskSimpleStandStillCtorFn(task, 999999, true, false, 8.0f);
                            gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(pedIntel + 8), task, 4, false);
                        }
                    }

                    if (gtaSymbols_.cpedSetCharCreatedByFn) {
                        gtaSymbols_.cpedSetCharCreatedByFn(newPed, 2 /*MISSION_CHAR*/);
                    }
                    *reinterpret_cast<float*>(newPedAddr + 0x6ac) = 200.0f; // m_fHealth
                    *reinterpret_cast<float*>(newPedAddr + 0x6b0) = 200.0f; // m_fMaxHealth

                    uintptr_t newMat = *reinterpret_cast<uintptr_t*>(newPedAddr + 0x18);
                    if (newMat != 0) {
                        *reinterpret_cast<float*>(newMat + 0x30) = targetRemX;
                        *reinterpret_cast<float*>(newMat + 0x34) = targetRemY;
                        *reinterpret_cast<float*>(newMat + 0x38) = targetRemZ;
                    }
                    *reinterpret_cast<float*>(newPedAddr + 0x08) = targetRemX;
                    *reinterpret_cast<float*>(newPedAddr + 0x0c) = targetRemY;
                    *reinterpret_cast<float*>(newPedAddr + 0x10) = targetRemZ;

                    // Agregar al mundo 3D igual que CREATE_CHAR (0x3f6238: bl CWorld::Add)
                    gtaSymbols_.cworldAddFn(newPed);

                    int handle = gtaSymbols_.cpoolsGetPedRefFn ? gtaSymbols_.cpoolsGetPedRefFn(newPed) : -1;
                    remotePedPtr_[i] = newPed;
                    remotePedHandle_[i] = handle;
                    remotePedSpawnedModel_[i] = static_cast<int>(spawnModel);

                    if (handle > 0 && gtaSymbols_.cradarSetEntityBlipFn) {
                        remoteBlipHandle_[i] = gtaSymbols_.cradarSetEntityBlipFn(
                            4 /*BLIP_CHAR*/, handle, 1 /*GREEN*/, 3 /*BLIP_DISPLAY_BOTH*/, nullptr
                        );
                    }

                    ped = newPed;
                    LOGI("Spawned 3D Remote Player P%d (ped=%p, handle=%d, model=%u) at (%.2f, %.2f, %.2f)",
                         i + 1, newPed, handle, spawnModel, targetRemX, targetRemY, targetRemZ);
                }
            }

            if (ped) {
                applySmoothPedTransformAndAnim(
                    ped, targetRemX, targetRemY, targetRemZ,
                    rem.vx, rem.vy, rem.vz, rem.heading,
                    static_cast<int>(rem.animId), 200.0f
                );
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
                    WorldSyncPacket worldPkt{};
                    if (config_.isHost) {
                        worldPkt = buildWorldSyncPacket();
                    }

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
                            ::sendto(sock, &worldPkt, sizeof(worldPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
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

    WorldSyncPacket buildWorldSyncPacket() {
        std::lock_guard<std::mutex> lock(mutex_);
        WorldSyncPacket pkt{};
        fillHeaderLocked(pkt.header, PKT_WORLD_SYNC, static_cast<uint8_t>(config_.localPlayerSlot));
        pkt.clockHours = worldState_.clockHours;
        pkt.clockMinutes = worldState_.clockMinutes;
        pkt.clockSeconds = worldState_.clockSeconds;
        pkt.oldWeather = worldState_.oldWeather;
        pkt.newWeather = worldState_.newWeather;
        pkt.numPeds = worldState_.numPeds;
        pkt.numVehicles = worldState_.numVehicles;
        for (int k = 0; k < kMaxSyncedWorldPeds; ++k) {
            pkt.peds[k] = worldState_.peds[k];
        }
        for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
            pkt.vehicles[v] = worldState_.vehicles[v];
        }
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
                bool isDefaultHoldPos = (std::fabs(ped->position.x - 2495.0f) < 2.5f &&
                                         std::fabs(ped->position.y - (-1687.0f)) < 2.5f);
                if (((hdr->flags & 0x08) != 0) && !isDefaultHoldPos) {
                    p.hasRealGtaCoords = true;
                }
                if (ped->skinModelId >= 0) p.skinModelId = ped->skinModelId;
                if (!p.hasRealGtaCoords && isDefaultHoldPos) {
                    packetsProcessed_.fetch_add(1);
                    return true;
                }

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
            case PKT_WORLD_SYNC: {
                if (len < sizeof(WorldSyncPacket)) return false;
                const auto* ws = reinterpret_cast<const WorldSyncPacket*>(data);
                worldState_.lastNetworkUpdateMs = nowMs;
                worldState_.clockHours = ws->clockHours;
                worldState_.clockMinutes = ws->clockMinutes;
                worldState_.clockSeconds = ws->clockSeconds;
                worldState_.oldWeather = ws->oldWeather;
                worldState_.newWeather = ws->newWeather;
                worldState_.numPeds = std::min<uint8_t>(ws->numPeds, kMaxSyncedWorldPeds);
                worldState_.numVehicles = std::min<uint8_t>(ws->numVehicles, kMaxSyncedWorldVehicles);
                for (int k = 0; k < kMaxSyncedWorldPeds; ++k) {
                    worldState_.peds[k] = ws->peds[k];
                }
                for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                    worldState_.vehicles[v] = ws->vehicles[v];
                }
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
        char buf[1024];
        std::snprintf(
            buf, sizeof(buf),
            "libCoopAndreasBridge.so v2.8 (Tornamic Port) | Role=%s (Slot %d) | Room=%s | UDP=%d/%d | "
            "Players=%d/%d | Cam=%s | GOTHook=%s (Ticks=%lld) | LocalCJ=(%.1f,%.1f,%.1f anim=%u) | "
            "RemoteP%d=(%.1f,%.1f,%.1f ped=%p anim=%u) | Clock=%02u:%02u Weather=%d/%d WorldPeds=%u(m0=%d@%.1f,%.1f) WorldVehs=%u(m0=%d@%.1f,%.1f) | "
            "$ONMISSION=%d (%s CP#%d) | Opcodes=%lld | Packets=%lld",
            config_.isHost ? "HOST" : "GUEST",
            config_.localPlayerSlot,
            config_.roomCode.c_str(),
            config_.udpCoopPort, config_.udpStreamPort,
            activeCount, config_.maxPlayers,
            (config_.cameraMode == 0) ? "3D_INDEPENDENT" : "SHARED",
            gotHookInstalled_.load() ? "ACTIVE" : "PENDING",
            static_cast<long long>(mainThreadTicks_.load()),
            players_[localIdx].x, players_[localIdx].y, players_[localIdx].z, players_[localIdx].animId,
            remoteIdx + 1, players_[remoteIdx].x, players_[remoteIdx].y, players_[remoteIdx].z,
            remotePedPtr_[remoteIdx], players_[remoteIdx].animId,
            worldState_.clockHours, worldState_.clockMinutes,
            worldState_.oldWeather, worldState_.newWeather,
            worldState_.numPeds, worldState_.peds[0].modelId, worldState_.peds[0].x, worldState_.peds[0].y,
            worldState_.numVehicles, worldState_.vehicles[0].modelId, worldState_.vehicles[0].x, worldState_.vehicles[0].y,
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
    WorldSyncState worldState_;

    void* lastLocalPedPtr_ = nullptr;
    bool initialSyncTeleportDone_ = false;
    bool initialAlleyFreeRoamDone_ = false;
    int64_t alleyLandWaitTicks_ = 0;
    void* remotePedPtr_[kMaxCoopPlayers] = {nullptr};
    int remotePedHandle_[kMaxCoopPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int remotePedSpawnedModel_[kMaxCoopPlayers] = {0};
    int remoteBlipHandle_[kMaxCoopPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};

    void* worldSyncPedPtr_[kMaxSyncedWorldPeds] = {nullptr};
    int worldSyncPedHandle_[kMaxSyncedWorldPeds] = {-1, -1, -1, -1, -1, -1};
    int16_t worldSyncPedHostSlot_[kMaxSyncedWorldPeds] = {-1, -1, -1, -1, -1, -1};
    int16_t worldSyncPedModel_[kMaxSyncedWorldPeds] = {-1, -1, -1, -1, -1, -1};

    void* worldSyncVehPtr_[kMaxSyncedWorldVehicles] = {nullptr};
    int worldSyncVehHandle_[kMaxSyncedWorldVehicles] = {-1, -1, -1, -1, -1};
    int16_t worldSyncVehHostSlot_[kMaxSyncedWorldVehicles] = {-1, -1, -1, -1, -1};
    int16_t worldSyncVehModel_[kMaxSyncedWorldVehicles] = {-1, -1, -1, -1, -1};

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
        // Evitar que múltiples copias del .so en mods/ o files/ inicien hilos duplicados en el mismo proceso
        if (getenv("COOP_ANDREAS_BRIDGE_ACTIVE") != nullptr) {
            LOGW("CoopAndreasBridge already active in this process; skipping duplicate .so instance!");
            return;
        }
        setenv("COOP_ANDREAS_BRIDGE_ACTIVE", "1", 1);

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
