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
constexpr int kMaxCoopPlayers = 4;
constexpr int kCoopAndreasUdpPort = 6767;           // Puerto oficial Tornamic/CoopAndreas
constexpr int kDefaultControlTcpPort = 8989;        // Puerto TCP Lobby App
constexpr int kDefaultTelemetryVoiceUdpPort = 8990; // Puerto UDP App Telemetría/Voz
constexpr int kDefaultDiscoveryUdpPort = 8988;      // Puerto UDP Broadcast LAN
constexpr int kTickRateHz = 30;                     // 30 Hz sync loop (33ms por tick)

// Tipos de Paquetes Binarios (Protocolo Fusionado SA:COOP + Tornamic 4P + Project Kaizo)
enum PacketType : uint8_t {
    PKT_HANDSHAKE        = 0x01,
    PKT_PED_SYNC         = 0x02,
    PKT_VEHICLE_SYNC     = 0x03,
    PKT_MISSION_SYNC     = 0x04,
    PKT_WEAPON_FIRE      = 0x05,
    PKT_CUTSCENE_SYNC    = 0x06,
    PKT_WORLD_CHEAT_SYNC = 0x07,
    PKT_WORLD_SYNC       = 0x08
};

enum CheatSyncCategory : uint8_t {
    CHEAT_LOCAL_ONLY   = 0, // HESOYAM, LXGIWYL, KJKSZPJ, UZUMYMW, AEZAKMI (solo jugador local)
    CHEAT_WORLD_SHARED = 1, // Clima, tráfico, disturbios, velocidad del reloj (sincronizado 4P)
    CHEAT_VEHICLE_SPAWN = 2 // Vehículo spawneado con ownerSlot y vehicleNetId compartido
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
    uint8_t wantedLevel = 0;       // CWanted individual (0..6 estrellas) sin pisar el local de otros
    uint8_t isCrouching = 0;
    uint8_t isJumping = 0;
    uint8_t isAiming = 0;
    uint8_t independentCamera = 1; // 1 = Cámara 3D independiente en cada teléfono
};

struct VehicleSyncPacket {
    BinaryPacketHeader header;
    int32_t vehicleNetId = 101;
    int32_t vehicleModelId = 492; // 492 = Greenwood
    int32_t seatIndex = 0;        // -1 = a pie, 0 = conductor, 1..3 = pasajeros, 4..6 = Vehicle Surf (techo/chasis)
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
    uint8_t isAttachedOnRoof = 0; // 1 = Project Kaizo Vehicle Surf (Opcode 0464 attach_to_car)
    uint8_t ownerSlot = 1;        // Autoridad de entidad (1..4)
    uint8_t sirenOrHorn = 0;
};

struct CutsceneSyncPacket {
    BinaryPacketHeader header;
    uint8_t cutsceneActive = 0;
    uint8_t skipVotesMask = 0;     // Bits 0..3: votos de los slots 1..4 para saltar escena
    uint8_t activePlayersMask = 1; // Bits 0..3: jugadores presentes en la sesión
    char missionId[16] = "ls_02";
};

struct WorldCheatSyncPacket {
    BinaryPacketHeader header;
    uint32_t cheatHash = 0;
    uint8_t cheatCategory = CHEAT_WORLD_SHARED;
    uint8_t ownerSlot = 1;
    int16_t spawnedVehicleModelId = 0;
    char cheatCode[24] = "";
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
    uint8_t ownerSlot = 1; // Handoff de autoridad (1..4) dentro de 150m
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
    uint8_t ownerSlot = 1; // Handoff de autoridad (1..4) dentro de 150m
    int16_t hostPoolSlot = -1;
    int16_t modelId = 492;
    int16_t driverModelId = -1; // Modelo del conductor NPC (ej. policía 280/284 en moto 523)
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
    uint32_t packedColors = 0; // +0x574..+0x577: m_nPrimaryColor, m_nSecondaryColor, m_nTertiaryColor, m_nQuaternaryColor
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
    using FnCRadarChangeBlipColour = void (*)(int blipHandle, uint32_t colour);
    using FnCRadarSetBlipFriendly = void (*)(int blipHandle, uint8_t friendly);
    using FnCRadarChangeBlipScale = void (*)(int blipHandle, int scale);
    using FnCRadarClearBlip = void (*)(int blipHandle);
    FnCRadarChangeBlipColour cradarChangeBlipColourFn = nullptr;
    FnCRadarSetBlipFriendly cradarSetBlipFriendlyFn = nullptr;
    FnCRadarChangeBlipScale cradarChangeBlipScaleFn = nullptr;
    FnCRadarClearBlip cradarClearBlipFn = nullptr;
    FnC3dMarkersPlaceMarkerCone c3dMarkersPlaceMarkerConeFn = nullptr;
    using FnCTaskOperatorNew = void* (*)(size_t size);
    using FnCTaskSimpleStandStillCtor = void* (*)(void* self, int nTime, bool bLooped, bool bUseAnimIdle, float fBlendData);
    using FnCTaskManagerSetTask = void (*)(void* self, void* pTask, int nTaskIndex, bool bForceNewTask);
    using FnCPedSetMoveState = void (*)(void* thisPed, int moveState);
    using FnCPedSetMoveAnim = void (*)(void* thisPed);
    using FnCPopulationRemoveAllRandomPeds = void (*)();
    using FnRpAnimBlendClumpUpdateAnimations = void (*)(void* clump, float timeStep, bool onScreen);
    using FnCCarCtrlCreateCarForScript = void* (*)(int modelId, CVector pos, uint8_t doMissionCleanup);
    using FnCVehicleSetUpDriver = void* (*)(void* thisVeh, int pedType, bool b2, bool b3);
    using FnCTaskSetPedInAsDriverCtor = void* (*)(void* self, void* veh, void* util);
    using FnCTaskSetPedInAsPassengerCtor = void* (*)(void* self, void* veh, int targetDoor, void* util);
    using FnCTaskSetPedOutCtor = void* (*)(void* self, void* veh, int targetDoor, bool switchOffEngine);
    using FnCTaskProcessPed = bool (*)(void* self, void* ped);
    using FnCTaskDtor = void (*)(void* self);
    using FnComputePassengerDoor = int (*)(const void* veh, int passengerSlot);
    using FnSetPedInCarDirect = void (*)(void* ped, void* veh, int seatNumber, bool bAsDriver);
    using FnSetPedPositionInCar = void (*)(void* thisPed);
    using FnFindActiveTaskByType = void* (*)(const void* thisTaskMgr, int taskType);
    using FnCPoolsGetVehicleRef = int (*)(void* veh);
    using FnCPoolsGetVehicle = void* (*)(int handle);
    using FnCWorldClearCarsFromArea = void (*)(float x1, float y1, float z1, float x2, float y2, float z2);
    using FnCCutsceneMgrSkipCutscene = void (*)();
    using FnCCheatVoid = void (*)();
    using FnCCheatVehicle = void (*)(int modelId);

    FnCTaskOperatorNew ctaskOperatorNewFn = nullptr;
    FnCTaskSimpleStandStillCtor ctaskSimpleStandStillCtorFn = nullptr;
    FnCTaskManagerSetTask ctaskManagerSetTaskFn = nullptr;
    FnCPedSetMoveState cpedSetMoveStateFn = nullptr;
    FnCPedSetMoveAnim cpedSetMoveAnimFn = nullptr;
    FnCPopulationRemoveAllRandomPeds cpopulationRemoveAllRandomPedsFn = nullptr;
    FnRpAnimBlendClumpUpdateAnimations rpAnimBlendClumpUpdateAnimationsFn = nullptr;
    FnCCarCtrlCreateCarForScript ccarCtrlCreateCarForScriptFn = nullptr;
    FnCVehicleSetUpDriver cvehicleSetUpDriverFn = nullptr;
    FnCTaskSetPedInAsDriverCtor ctaskSetPedInAsDriverCtorFn = nullptr;
    FnCTaskProcessPed ctaskSetPedInAsDriverProcessFn = nullptr;
    FnCTaskDtor ctaskSetPedInAsDriverDtorFn = nullptr;
    FnCTaskSetPedInAsPassengerCtor ctaskSetPedInAsPassengerCtorFn = nullptr;
    FnCTaskProcessPed ctaskSetPedInAsPassengerProcessFn = nullptr;
    FnCTaskDtor ctaskSetPedInAsPassengerDtorFn = nullptr;
    FnCTaskSetPedOutCtor ctaskSetPedOutCtorFn = nullptr;
    FnCTaskProcessPed ctaskSetPedOutProcessFn = nullptr;
    FnCTaskDtor ctaskSetPedOutDtorFn = nullptr;
    FnComputePassengerDoor ccarEnterExitComputePassengerDoorFn = nullptr;
    FnSetPedInCarDirect ccarEnterExitSetPedInCarDirectFn = nullptr;
    FnSetPedPositionInCar cpedSetPedPositionInCarFn = nullptr;
    FnFindActiveTaskByType ctaskManagerFindActiveTaskByTypeFn = nullptr;
    FnCPoolsGetVehicleRef cpoolsGetVehicleRefFn = nullptr;
    FnCPoolsGetVehicle cpoolsGetVehicleFn = nullptr;
    FnCWorldClearCarsFromArea cworldClearCarsFromAreaFn = nullptr;
    FnCCutsceneMgrSkipCutscene ccutsceneMgrSkipCutsceneFn = nullptr;
    FnCCheatVoid ccheatHealthCheatFn = nullptr;
    FnCCheatVoid ccheatWeaponCheat1Fn = nullptr;
    FnCCheatVoid ccheatWeaponCheat2Fn = nullptr;
    FnCCheatVoid ccheatWeaponCheat3Fn = nullptr;
    FnCCheatVoid ccheatJetpackCheatFn = nullptr;
    FnCCheatVoid ccheatNotWantedCheatFn = nullptr;
    FnCCheatVoid ccheatWantedCheatFn = nullptr;
    FnCCheatVoid ccheatSunnyCheatFn = nullptr;
    FnCCheatVoid ccheatRainCheatFn = nullptr;
    FnCCheatVoid ccheatRiotCheatFn = nullptr;
    FnCCheatVehicle ccheatVehicleCheatFn = nullptr;

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
        cradarChangeBlipColourFn = reinterpret_cast<FnCRadarChangeBlipColour>(dlsym(libHandle, "_ZN6CRadar16ChangeBlipColourEij"));
        cradarSetBlipFriendlyFn = reinterpret_cast<FnCRadarSetBlipFriendly>(dlsym(libHandle, "_ZN6CRadar15SetBlipFriendlyEih"));
        cradarChangeBlipScaleFn = reinterpret_cast<FnCRadarChangeBlipScale>(dlsym(libHandle, "_ZN6CRadar15ChangeBlipScaleEii"));
        cradarClearBlipFn = reinterpret_cast<FnCRadarClearBlip>(dlsym(libHandle, "_ZN6CRadar9ClearBlipEi"));
        c3dMarkersPlaceMarkerConeFn = reinterpret_cast<FnC3dMarkersPlaceMarkerCone>(dlsym(libHandle, "_ZN10C3dMarkers15PlaceMarkerConeEyR7CVectorfhhhhtfsh"));
        ctaskOperatorNewFn = reinterpret_cast<FnCTaskOperatorNew>(dlsym(libHandle, "_ZN5CTasknwEm"));
        ctaskSimpleStandStillCtorFn = reinterpret_cast<FnCTaskSimpleStandStillCtor>(dlsym(libHandle, "_ZN21CTaskSimpleStandStillC1Eibbf"));
        ctaskManagerSetTaskFn = reinterpret_cast<FnCTaskManagerSetTask>(dlsym(libHandle, "_ZN12CTaskManager7SetTaskEP5CTaskib"));
        cpedSetMoveStateFn = reinterpret_cast<FnCPedSetMoveState>(dlsym(libHandle, "_ZN4CPed12SetMoveStateE10eMoveState"));
        cpedSetMoveAnimFn = reinterpret_cast<FnCPedSetMoveAnim>(dlsym(libHandle, "_ZN4CPed11SetMoveAnimEv"));
        cpopulationRemoveAllRandomPedsFn = reinterpret_cast<FnCPopulationRemoveAllRandomPeds>(dlsym(libHandle, "_ZN11CPopulation19RemoveAllRandomPedsEv"));
        rpAnimBlendClumpUpdateAnimationsFn = reinterpret_cast<FnRpAnimBlendClumpUpdateAnimations>(dlsym(libHandle, "_Z32RpAnimBlendClumpUpdateAnimationsP7RpClumpfb"));
        ccarCtrlCreateCarForScriptFn = reinterpret_cast<FnCCarCtrlCreateCarForScript>(dlsym(libHandle, "_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh"));
        cvehicleSetUpDriverFn = reinterpret_cast<FnCVehicleSetUpDriver>(dlsym(libHandle, "_ZN8CVehicle11SetUpDriverEibb"));
        ctaskSetPedInAsDriverCtorFn = reinterpret_cast<FnCTaskSetPedInAsDriverCtor>(dlsym(libHandle, "_ZN30CTaskSimpleCarSetPedInAsDriverC1EP8CVehicleP28CTaskUtilityLineUpPedWithCar"));
        ctaskSetPedInAsDriverProcessFn = reinterpret_cast<FnCTaskProcessPed>(dlsym(libHandle, "_ZN30CTaskSimpleCarSetPedInAsDriver10ProcessPedEP4CPed"));
        ctaskSetPedInAsDriverDtorFn = reinterpret_cast<FnCTaskDtor>(dlsym(libHandle, "_ZN30CTaskSimpleCarSetPedInAsDriverD1Ev"));
        ctaskSetPedInAsPassengerCtorFn = reinterpret_cast<FnCTaskSetPedInAsPassengerCtor>(dlsym(libHandle, "_ZN33CTaskSimpleCarSetPedInAsPassengerC1EP8CVehicleiP28CTaskUtilityLineUpPedWithCar"));
        ctaskSetPedInAsPassengerProcessFn = reinterpret_cast<FnCTaskProcessPed>(dlsym(libHandle, "_ZN33CTaskSimpleCarSetPedInAsPassenger10ProcessPedEP4CPed"));
        ctaskSetPedInAsPassengerDtorFn = reinterpret_cast<FnCTaskDtor>(dlsym(libHandle, "_ZN33CTaskSimpleCarSetPedInAsPassengerD1Ev"));
        ctaskSetPedOutCtorFn = reinterpret_cast<FnCTaskSetPedOutCtor>(dlsym(libHandle, "_ZN23CTaskSimpleCarSetPedOutC1EP8CVehicleib"));
        ctaskSetPedOutProcessFn = reinterpret_cast<FnCTaskProcessPed>(dlsym(libHandle, "_ZN23CTaskSimpleCarSetPedOut10ProcessPedEP4CPed"));
        ctaskSetPedOutDtorFn = reinterpret_cast<FnCTaskDtor>(dlsym(libHandle, "_ZN23CTaskSimpleCarSetPedOutD1Ev"));
        ccarEnterExitComputePassengerDoorFn = reinterpret_cast<FnComputePassengerDoor>(dlsym(libHandle, "_ZN13CCarEnterExit35ComputeTargetDoorToEnterAsPassengerERK8CVehiclei"));
        ccarEnterExitSetPedInCarDirectFn = reinterpret_cast<FnSetPedInCarDirect>(dlsym(libHandle, "_ZN13CCarEnterExit17SetPedInCarDirectEP4CPedP8CVehicleib"));
        cpedSetPedPositionInCarFn = reinterpret_cast<FnSetPedPositionInCar>(dlsym(libHandle, "_ZN4CPed19SetPedPositionInCarEv"));
        ctaskManagerFindActiveTaskByTypeFn = reinterpret_cast<FnFindActiveTaskByType>(dlsym(libHandle, "_ZNK12CTaskManager20FindActiveTaskByTypeEi"));
        cpoolsGetVehicleRefFn = reinterpret_cast<FnCPoolsGetVehicleRef>(dlsym(libHandle, "_ZN6CPools13GetVehicleRefEP8CVehicle"));
        cpoolsGetVehicleFn = reinterpret_cast<FnCPoolsGetVehicle>(dlsym(libHandle, "_ZN6CPools10GetVehicleEi"));
        cworldClearCarsFromAreaFn = reinterpret_cast<FnCWorldClearCarsFromArea>(dlsym(libHandle, "_ZN6CWorld17ClearCarsFromAreaEffffff"));
        ccutsceneMgrSkipCutsceneFn = reinterpret_cast<FnCCutsceneMgrSkipCutscene>(dlsym(libHandle, "_ZN12CCutsceneMgr12SkipCutsceneEv"));
        ccheatHealthCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat11HealthCheatEv"));
        ccheatWeaponCheat1Fn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat12WeaponCheat1Ev"));
        ccheatWeaponCheat2Fn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat12WeaponCheat2Ev"));
        ccheatWeaponCheat3Fn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat12WeaponCheat3Ev"));
        ccheatJetpackCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat12JetpackCheatEv"));
        ccheatNotWantedCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat14NotWantedCheatEv"));
        ccheatWantedCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat11WantedCheatEv"));
        ccheatSunnyCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat10SunnyCheatEv"));
        ccheatRainCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat9RainCheatEv"));
        ccheatRiotCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat9RiotCheatEv"));
        ccheatVehicleCheatFn = reinterpret_cast<FnCCheatVehicle>(dlsym(libHandle, "_ZN6CCheat12VehicleCheatEi"));
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
    int32_t seatIndex = -1; // -1 = a pie, 0 = conductor, 1..3 = pasajeros, 4..6 = Vehicle Surf
    float steerAngle = 0.0f;
    float gasPedal = 0.0f;
    float brakePedal = 0.0f;
    bool isDriveByActive = false;
    bool isAttachedOnRoof = false;
    uint8_t ownerSlot = 1;

    CVector lastAimOrigin{2495.0f, -1687.0f, 14.0f};
    CVector lastAimTarget{2505.0f, -1680.0f, 14.0f};
    uint32_t shotsFiredCount = 0;

    int32_t cameraMode = 0;
    uint8_t markerColor = 0xFF;
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
    int32_t localMarkerColor = -1;
    int32_t localSkinModelId = 0;
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
        float vx, float vy, float vz, float heading, int moveState, float health,
        uint64_t lastNetworkUpdateMs = 0
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

        // a) Predicción dead-reckoning suave entre paquetes de 60 Hz
        uint64_t nowMs = currentTimeMillis();
        float dtSec = (lastNetworkUpdateMs > 0 && nowMs > lastNetworkUpdateMs)
            ? std::clamp(static_cast<float>(nowMs - lastNetworkUpdateMs) * 0.001f, 0.0f, 0.05f)
            : 0.0f;
        float predX = targetX + vx * dtSec * 50.0f;
        float predY = targetY + vy * dtSec * 50.0f;
        float predZ = targetZ;

        uintptr_t mat = *reinterpret_cast<uintptr_t*>(pedAddr + 0x18);
        float curX = predX, curY = predY, curZ = predZ;
        if (mat != 0) {
            curX = *reinterpret_cast<float*>(mat + 0x30);
            curY = *reinterpret_cast<float*>(mat + 0x34);
            curZ = *reinterpret_cast<float*>(mat + 0x38);
        }

        float dx = predX - curX;
        float dy = predY - curY;
        float dz = predZ - curZ;
        float distSq = dx * dx + dy * dy + dz * dz;

        float speed2d = std::sqrt(vx * vx + vy * vy);
        int effectiveMoveState = moveState;
        if (effectiveMoveState != 4 && effectiveMoveState != 6 && effectiveMoveState != 7) {
            if (speed2d > 0.14f) effectiveMoveState = 7;       // PEDMOVE_SPRINT
            else if (speed2d > 0.05f) effectiveMoveState = 6;  // PEDMOVE_RUN
            else if (speed2d > 0.015f) effectiveMoveState = 4; // PEDMOVE_WALK
            else effectiveMoveState = 1;                       // PEDMOVE_STILL
        }

        if (mat != 0) {
            float alphaXY = (distSq > 25.0f) ? 1.0f : ((effectiveMoveState >= 4 && distSq < 6.0f) ? 0.38f : 0.50f);
            float alphaZ = (distSq > 25.0f) ? 1.0f : ((std::fabs(dz) < 0.45f && std::fabs(vz) < 0.05f) ? 0.15f : 0.45f);
            float nextX = curX + dx * alphaXY;
            float nextY = curY + dy * alphaXY;
            float nextZ = curZ + dz * alphaZ;

            // b) ANTI-SALTO EN Z: solo reinsertar en sectores de CWorld si distSq >= 4.0f o cada 15 ticks
            // para evitar que CWorld::Add dispare impulsos verticales de colisión en cada frame
            bool needWorldSectorRelink = (distSq >= 4.0f) || ((mainThreadTicks_.load() % 15) == 0);
            if (needWorldSectorRelink && gtaSymbols_.cworldRemoveFn) {
                gtaSymbols_.cworldRemoveFn(ped);
            }

            *reinterpret_cast<float*>(mat + 0x30) = nextX;
            *reinterpret_cast<float*>(mat + 0x34) = nextY;
            *reinterpret_cast<float*>(mat + 0x38) = nextZ;
            *reinterpret_cast<float*>(pedAddr + 0x08) = nextX;
            *reinterpret_cast<float*>(pedAddr + 0x0c) = nextY;
            *reinterpret_cast<float*>(pedAddr + 0x10) = nextZ;

            // c) Interpolación de heading por el camino angular más corto
            if (std::isfinite(heading)) {
                float curH = *reinterpret_cast<float*>(pedAddr + 0x6c4);
                float diffH = std::isfinite(curH) ? std::atan2(std::sin(heading - curH), std::cos(heading - curH)) : 0.0f;
                float smoothH = (distSq > 25.0f || !std::isfinite(curH)) ? heading : (curH + diffH * 0.40f);
                float c = std::cos(smoothH);
                float s = std::sin(smoothH);
                *reinterpret_cast<float*>(mat + 0x00) = c;
                *reinterpret_cast<float*>(mat + 0x04) = s;
                *reinterpret_cast<float*>(mat + 0x08) = 0.0f;
                *reinterpret_cast<float*>(mat + 0x10) = -s;
                *reinterpret_cast<float*>(mat + 0x14) = c;
                *reinterpret_cast<float*>(mat + 0x18) = 0.0f;
                *reinterpret_cast<float*>(pedAddr + 0x6c4) = smoothH;
                *reinterpret_cast<float*>(pedAddr + 0x6c8) = smoothH;
            }

            // Desactivar rebote vertical y velocidad residual física (m_vecMoveSpeed +0x68..+0x70 y m_vecTurnSpeed +0x74..+0x7c)
            *reinterpret_cast<float*>(pedAddr + 0x68) = 0.0f;
            *reinterpret_cast<float*>(pedAddr + 0x6c) = 0.0f;
            *reinterpret_cast<float*>(pedAddr + 0x70) = 0.0f;
            *reinterpret_cast<float*>(pedAddr + 0x74) = 0.0f;
            *reinterpret_cast<float*>(pedAddr + 0x78) = 0.0f;
            *reinterpret_cast<float*>(pedAddr + 0x7c) = 0.0f;
            *reinterpret_cast<uint8_t*>(pedAddr + 0x584) |= 0x01; // bIsStanding = true

            if (gtaSymbols_.centityUpdateRwFrameFn) {
                gtaSymbols_.centityUpdateRwFrameFn(ped);
            }
            if (needWorldSectorRelink && gtaSymbols_.cworldAddFn) {
                gtaSymbols_.cworldAddFn(ped);
            }
        }

        // Sincronizar m_fMoveAnimRate (+0x688) proporcional a speed2d para que los pasos coincidan 1:1 con el desplazamiento
        float animRate = (effectiveMoveState >= 4 && speed2d > 0.005f)
            ? std::clamp(speed2d * 12.0f, 0.85f, 1.35f)
            : 1.0f;
        *reinterpret_cast<float*>(pedAddr + 0x688) = animRate;

        if (effectiveMoveState == 1 && pedIntel != 0) {
            uintptr_t defaultTaskAddr = *reinterpret_cast<uintptr_t*>(pedIntel + 0x28);
            if (defaultTaskAddr != 0 && *reinterpret_cast<int32_t*>(pedAddr + 0x6a4) != 1) {
                *reinterpret_cast<uint8_t*>(defaultTaskAddr + 0x1c) = 0;
            }
        }

        int32_t curMoveState = *reinterpret_cast<int32_t*>(pedAddr + 0x550);
        int32_t curAnimMoveState = *reinterpret_cast<int32_t*>(pedAddr + 0x6a4);
        if (curMoveState != effectiveMoveState || curAnimMoveState != effectiveMoveState) {
            if (gtaSymbols_.cpedSetMoveStateFn) {
                gtaSymbols_.cpedSetMoveStateFn(ped, effectiveMoveState);
            } else {
                *reinterpret_cast<int32_t*>(pedAddr + 0x550) = effectiveMoveState;
            }
            if (gtaSymbols_.cpedSetMoveAnimFn) {
                gtaSymbols_.cpedSetMoveAnimFn(ped);
            }
        }
        // NOTA: NO invocar RpAnimBlendClumpUpdateAnimations aquí; CWorld::Process ya avanza todas las animaciones
        // una vez por frame con el delta-time real del motor. Invocarla dos veces duplicaba la velocidad y trababa el ciclo.
#endif
    }

    // ========================================================================
    // HELPER: ACTUALIZAR TRANSFORMACIÓN 3D, COLOR Y OCUPANTES DE UN CVEHICLE SINCRONIZADO
    // ========================================================================
    void applySmoothVehicleTransform(void* veh, const SyncedWorldVehicleEntry& ve) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!veh) return;
        uintptr_t vehAddr = reinterpret_cast<uintptr_t>(veh);
        uintptr_t mat = *reinterpret_cast<uintptr_t*>(vehAddr + 0x18);
        if (mat == 0 || !gtaSymbols_.cworldRemoveFn || !gtaSymbols_.cworldAddFn) return;

        if (ve.packedColors != 0) {
            *reinterpret_cast<uint32_t*>(vehAddr + 0x574) = ve.packedColors;
        }

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
        *reinterpret_cast<float*>(vehAddr + 0x68) = 0.0f;
        *reinterpret_cast<float*>(vehAddr + 0x6c) = 0.0f;
        *reinterpret_cast<float*>(vehAddr + 0x70) = 0.0f;

        if (gtaSymbols_.centityUpdateRwFrameFn) {
            gtaSymbols_.centityUpdateRwFrameFn(veh);
        }
        gtaSymbols_.cworldAddFn(veh);

        // Fijar la posición 3D exacta del conductor (+0x5a0) y de todos los pasajeros (+0x5a8..+0x5c0)
        // a sus asientos dentro del vehículo en cada frame usando CPed::SetPedPositionInCar (0x59dfd8).
        if (gtaSymbols_.cpedSetPedPositionInCarFn) {
            for (int seatOffset = 0; seatOffset < 4; ++seatOffset) {
                void* occupant = *reinterpret_cast<void**>(vehAddr + 0x5a0 + static_cast<size_t>(seatOffset) * 8);
                if (occupant != nullptr) {
                    uintptr_t occAddr = reinterpret_cast<uintptr_t>(occupant);
                    if ((*reinterpret_cast<uint8_t*>(occAddr + 0x585) & 1) != 0 &&
                        *reinterpret_cast<void**>(occAddr + 0x708) == veh) {
                        gtaSymbols_.cpedSetPedPositionInCarFn(occupant);
                        if (gtaSymbols_.centityUpdateRwFrameFn) {
                            gtaSymbols_.centityUpdateRwFrameFn(occupant);
                        }
                    }
                }
            }
        }
#endif
    }

    void warpPedOutOfVehicle(void* ped) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!ped) return;
        uintptr_t pedAddr = reinterpret_cast<uintptr_t>(ped);
        if ((*reinterpret_cast<uint8_t*>(pedAddr + 0x585) & 1) == 0) return;
        uintptr_t pedIntel = *reinterpret_cast<uintptr_t*>(pedAddr + 0x538);
        if (pedIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
            gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(pedIntel + 8), nullptr, 3, false);
        }
        void* curVeh = *reinterpret_cast<void**>(pedAddr + 0x708);
        if (curVeh && gtaSymbols_.ctaskSetPedOutCtorFn && gtaSymbols_.ctaskSetPedOutProcessFn && gtaSymbols_.ctaskSetPedOutDtorFn) {
            alignas(16) uint8_t taskBuf[128] = {0};
            gtaSymbols_.ctaskSetPedOutCtorFn(taskBuf, curVeh, 10, false);
            taskBuf[0x1f] = 1; // m_bWarpingOutOfCar = true
            gtaSymbols_.ctaskSetPedOutProcessFn(taskBuf, ped);
            gtaSymbols_.ctaskSetPedOutDtorFn(taskBuf);
        }
#endif
    }

    void evictNpcPedFromWorld(void* ped, void* localPed) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!ped || ped == localPed) return;
        for (int r = 0; r < kMaxCoopPlayers; ++r) {
            if (ped == remotePedPtr_[r]) return;
        }
        uintptr_t pedAddr = reinterpret_cast<uintptr_t>(ped);
        if ((*reinterpret_cast<uint8_t*>(pedAddr + 0x585) & 1) != 0) {
            warpPedOutOfVehicle(ped);
        }
        if (gtaSymbols_.cpedSetCharCreatedByFn) {
            gtaSymbols_.cpedSetCharCreatedByFn(ped, 1 /*RANDOM_CHAR*/);
        }
        *reinterpret_cast<float*>(pedAddr + 0x6ac) = 0.0f; // m_fHealth = 0 para excluirlo del escaneo y liberar pool
        uintptr_t mat = *reinterpret_cast<uintptr_t*>(pedAddr + 0x18);
        if (mat != 0 && gtaSymbols_.cworldRemoveFn && gtaSymbols_.cworldAddFn) {
            gtaSymbols_.cworldRemoveFn(ped);
            *reinterpret_cast<float*>(mat + 0x30) = 0.0f;
            *reinterpret_cast<float*>(mat + 0x34) = 0.0f;
            *reinterpret_cast<float*>(mat + 0x38) = -500.0f;
            *reinterpret_cast<float*>(pedAddr + 0x08) = 0.0f;
            *reinterpret_cast<float*>(pedAddr + 0x0c) = 0.0f;
            *reinterpret_cast<float*>(pedAddr + 0x10) = -500.0f;
            if (gtaSymbols_.centityUpdateRwFrameFn) gtaSymbols_.centityUpdateRwFrameFn(ped);
            gtaSymbols_.cworldAddFn(ped);
        }
        if (gtaSymbols_.cpedTeleportFn) {
            gtaSymbols_.cpedTeleportFn(ped, CVector{0.0f, 0.0f, -500.0f}, 0);
        }
#endif
    }

    void warpPedIntoVehicleDriver(void* ped, void* veh, void* localPed, bool isCoopPlayer = false) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!ped || !veh) return;
        uintptr_t pedAddr = reinterpret_cast<uintptr_t>(ped);
        uintptr_t vehAddr = reinterpret_cast<uintptr_t>(veh);
        void* curDrv = *reinterpret_cast<void**>(vehAddr + 0x5a0);
        if (curDrv == ped && (*reinterpret_cast<uint8_t*>(pedAddr + 0x585) & 1) != 0 &&
            *reinterpret_cast<void**>(pedAddr + 0x708) == veh) {
            return;
        }
        if (curDrv && curDrv != ped && curDrv != localPed) {
            bool isRemPlayer = false;
            for (int r = 0; r < kMaxCoopPlayers; ++r) {
                if (curDrv == remotePedPtr_[r]) isRemPlayer = true;
            }
            if (isRemPlayer) {
                warpPedOutOfVehicle(curDrv);
            } else {
                evictNpcPedFromWorld(curDrv, localPed);
            }
        }
        if ((*reinterpret_cast<uint8_t*>(pedAddr + 0x585) & 1) != 0) {
            warpPedOutOfVehicle(ped);
        }
        if (isCoopPlayer && gtaSymbols_.ccarEnterExitSetPedInCarDirectFn) {
            gtaSymbols_.ccarEnterExitSetPedInCarDirectFn(ped, veh, 0, true);
            return;
        }
        if (gtaSymbols_.ctaskSetPedInAsDriverCtorFn && gtaSymbols_.ctaskSetPedInAsDriverProcessFn && gtaSymbols_.ctaskSetPedInAsDriverDtorFn) {
            alignas(16) uint8_t taskBuf[128] = {0};
            gtaSymbols_.ctaskSetPedInAsDriverCtorFn(taskBuf, veh, nullptr);
            taskBuf[0x30] = 1; // m_bWarpingInToCar = true
            gtaSymbols_.ctaskSetPedInAsDriverProcessFn(taskBuf, ped);
            gtaSymbols_.ctaskSetPedInAsDriverDtorFn(taskBuf);
            if (gtaSymbols_.cpedSetPedPositionInCarFn) {
                gtaSymbols_.cpedSetPedPositionInCarFn(ped);
            }
        }
#endif
    }

    void warpPedIntoVehiclePassenger(void* ped, void* veh, int passengerSlot, void* localPed = nullptr, bool isCoopPlayer = true) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!ped || !veh) return;
        uintptr_t pedAddr = reinterpret_cast<uintptr_t>(ped);
        uintptr_t vehAddr = reinterpret_cast<uintptr_t>(veh);
        int clampedSlot = std::clamp(passengerSlot, 0, 2);
        void* curPass = *reinterpret_cast<void**>(vehAddr + 0x5a8 + static_cast<size_t>(clampedSlot) * 8);
        if (curPass == ped && (*reinterpret_cast<uint8_t*>(pedAddr + 0x585) & 1) != 0 &&
            *reinterpret_cast<void**>(pedAddr + 0x708) == veh) {
            return;
        }
        if (curPass && curPass != ped && curPass != localPed) {
            bool isRemPlayer = false;
            for (int r = 0; r < kMaxCoopPlayers; ++r) {
                if (curPass == remotePedPtr_[r]) isRemPlayer = true;
            }
            if (isRemPlayer) {
                warpPedOutOfVehicle(curPass);
            } else {
                evictNpcPedFromWorld(curPass, localPed);
            }
        }
        if ((*reinterpret_cast<uint8_t*>(pedAddr + 0x585) & 1) != 0) {
            warpPedOutOfVehicle(ped);
        }
        int door = gtaSymbols_.ccarEnterExitComputePassengerDoorFn
            ? gtaSymbols_.ccarEnterExitComputePassengerDoorFn(veh, clampedSlot)
            : 8;
        if (isCoopPlayer && gtaSymbols_.ccarEnterExitSetPedInCarDirectFn) {
            gtaSymbols_.ccarEnterExitSetPedInCarDirectFn(ped, veh, door, false);
            return;
        }
        if (gtaSymbols_.ctaskSetPedInAsPassengerCtorFn && gtaSymbols_.ctaskSetPedInAsPassengerProcessFn && gtaSymbols_.ctaskSetPedInAsPassengerDtorFn) {
            alignas(16) uint8_t taskBuf[128] = {0};
            gtaSymbols_.ctaskSetPedInAsPassengerCtorFn(taskBuf, veh, door, nullptr);
            taskBuf[0x38] = 1; // m_bWarpingInToCar = true
            gtaSymbols_.ctaskSetPedInAsPassengerProcessFn(taskBuf, ped);
            gtaSymbols_.ctaskSetPedInAsPassengerDtorFn(taskBuf);
            if (gtaSymbols_.cpedSetPedPositionInCarFn) {
                gtaSymbols_.cpedSetPedPositionInCarFn(ped);
            }
        }
#endif
    }

    void evictVehicleFromWorld(void* veh, void* localPed) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!veh) return;
        uintptr_t vehAddr = reinterpret_cast<uintptr_t>(veh);
        for (int seatOffset = 0; seatOffset < 4; ++seatOffset) {
            void* occ = *reinterpret_cast<void**>(vehAddr + 0x5a0 + static_cast<size_t>(seatOffset) * 8);
            if (occ && occ != localPed) {
                bool isRemPlayer = false;
                for (int r = 0; r < kMaxCoopPlayers; ++r) {
                    if (occ == remotePedPtr_[r]) isRemPlayer = true;
                }
                if (isRemPlayer) {
                    warpPedOutOfVehicle(occ);
                } else {
                    evictNpcPedFromWorld(occ, localPed);
                }
            }
        }
        uintptr_t mat = *reinterpret_cast<uintptr_t*>(vehAddr + 0x18);
        if (mat != 0 && gtaSymbols_.cworldRemoveFn && gtaSymbols_.cworldAddFn) {
            gtaSymbols_.cworldRemoveFn(veh);
            *reinterpret_cast<float*>(mat + 0x30) = 0.0f;
            *reinterpret_cast<float*>(mat + 0x34) = 0.0f;
            *reinterpret_cast<float*>(mat + 0x38) = -500.0f;
            *reinterpret_cast<float*>(vehAddr + 0x08) = 0.0f;
            *reinterpret_cast<float*>(vehAddr + 0x0c) = 0.0f;
            *reinterpret_cast<float*>(vehAddr + 0x10) = -500.0f;
            if (gtaSymbols_.centityUpdateRwFrameFn) gtaSymbols_.centityUpdateRwFrameFn(veh);
            gtaSymbols_.cworldAddFn(veh);
        }
        *reinterpret_cast<uint8_t*>(vehAddr + 0x610) = 1; // RANDOM_VEHICLE
        if (gtaSymbols_.cworldClearCarsFromAreaFn) {
            gtaSymbols_.cworldClearCarsFromAreaFn(-200.0f, -200.0f, -600.0f, 200.0f, 200.0f, -400.0f);
        }
#endif
    }

    // ========================================================================
    // EJECUTADO EN EL HILO PRINCIPAL DE GTA SA (CADA FRAME DE CWorld::Process)
    // ========================================================================
    void onMainGameThreadTick() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        int64_t tickCount = mainThreadTicks_.fetch_add(1) + 1;

        if (!gtaSymbols_.findPlayerPedFn || !gtaSymbols_.findPlayerCoorsFn) return;

        // Cinemáticas cooperativas de 4 jugadores (PKT_CUTSCENE_SYNC): formación escalonada y salto por votación
        if (gtaSymbols_.cutsceneRunning && *gtaSymbols_.cutsceneRunning != 0) {
            void* cutPed = gtaSymbols_.findPlayerPedFn(-1);
            if (cutPed) {
                uintptr_t cAddr = reinterpret_cast<uintptr_t>(cutPed);
                uintptr_t cMat = *reinterpret_cast<uintptr_t*>(cAddr + 0x18);
                if (cMat != 0) {
                    float cx = *reinterpret_cast<float*>(cMat + 0x30);
                    float cy = *reinterpret_cast<float*>(cMat + 0x34);
                    float cz = *reinterpret_cast<float*>(cMat + 0x38);
                    float ch = *reinterpret_cast<float*>(cAddr + 0x6c4);
                    for (int i = 1; i < kMaxCoopPlayers; ++i) {
                        if (!players_[i].isActive || !remotePedPtr_[i]) continue;
                        int slot = i + 1;
                        float dx = -0.75f * static_cast<float>(slot - 1);
                        float dy = (slot % 2 == 0) ? 0.65f : -0.65f;
                        applySmoothPedTransformAndAnim(remotePedPtr_[i], cx + dx, cy + dy, cz, 0.0f, 0.0f, 0.0f, ch, 1, 200.0f);
                    }
                }
            }
            if (cutsceneSkipVotesMask_ != 0 && gtaSymbols_.ccutsceneMgrSkipCutsceneFn) {
                uint8_t activeMask = 0x01;
                for (int i = 1; i < kMaxCoopPlayers; ++i) {
                    if (players_[i].isActive && players_[i].lastNetworkUpdateMs > 0) activeMask |= static_cast<uint8_t>(1U << i);
                }
                if ((cutsceneSkipVotesMask_ & activeMask) == activeMask) {
                    gtaSymbols_.ccutsceneMgrSkipCutsceneFn();
                    cutsceneSkipVotesMask_ = 0;
                }
            }
            return;
        }

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
                worldSyncVehDriverPtr_[v] = nullptr;
                worldSyncVehDriverHandle_[v] = -1;
                worldSyncVehHadDriver_[v] = false;
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
                worldSyncVehDriverPtr_[v] = nullptr;
                worldSyncVehDriverHandle_[v] = -1;
                worldSyncVehHadDriver_[v] = false;
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

        // Detectar si el jugador local a pie tiene una tarea activa de entrada/robo de vehículo (700/701/712/713 o slot 3)
        bool localTryingToEnterCar = false;
        if (!myVeh) {
            uintptr_t localIntel = *reinterpret_cast<uintptr_t*>(localPedAddr + 0x538);
            if (localIntel != 0) {
                void* taskMgr = reinterpret_cast<void*>(localIntel + 0x08);
                void* primaryTask = *reinterpret_cast<void**>(localIntel + 0x08 + 3 * 8);
                if (primaryTask != nullptr) {
                    localTryingToEnterCar = true;
                } else if (gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn) {
                    if (gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(taskMgr, 701) ||
                        gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(taskMgr, 700) ||
                        gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(taskMgr, 713) ||
                        gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(taskMgr, 712)) {
                        localTryingToEnterCar = true;
                    }
                }
            }
            if (localTryingToEnterCar) {
                for (int r = 0; r < kMaxCoopPlayers; ++r) {
                    if (r == localIdx || !players_[r].isActive || players_[r].seatIndex != 0) continue;
                    void* partnerVeh = nullptr;
                    if (remotePedPtr_[r]) {
                        uintptr_t rPedAddr = reinterpret_cast<uintptr_t>(remotePedPtr_[r]);
                        if ((*reinterpret_cast<uint8_t*>(rPedAddr + 0x585) & 1) != 0) {
                            partnerVeh = *reinterpret_cast<void**>(rPedAddr + 0x708);
                        }
                    }
                    if (!partnerVeh && players_[r].vehicleNetId >= 1000) {
                        int wantedSlot = players_[r].vehicleNetId - 1000;
                        if (!config_.isHost) {
                            for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                                if (worldSyncVehPtr_[v] && worldSyncVehHostSlot_[v] == wantedSlot) {
                                    partnerVeh = worldSyncVehPtr_[v];
                                    break;
                                }
                            }
                        } else if (gtaSymbols_.cpoolsVehiclePool) {
                            void* vPoolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsVehiclePool);
                            if (vPoolPtr) {
                                uintptr_t vPoolAddr = reinterpret_cast<uintptr_t>(vPoolPtr);
                                uint8_t* vObjects = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x00);
                                uint8_t* vByteMap = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x08);
                                int32_t vPoolSize = *reinterpret_cast<int32_t*>(vPoolAddr + 0x10);
                                if (vObjects && vByteMap && wantedSlot < vPoolSize && (vByteMap[wantedSlot] & 0x80) == 0) {
                                    partnerVeh = reinterpret_cast<void*>(vObjects + static_cast<size_t>(wantedSlot) * 0xc68);
                                }
                            }
                        }
                    }
                    if (!partnerVeh) continue;
                    uintptr_t pvMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(partnerVeh) + 0x18);
                    if (pvMat == 0) continue;
                    float dvx = *reinterpret_cast<float*>(pvMat + 0x30) - myPos.x;
                    float dvy = *reinterpret_cast<float*>(pvMat + 0x34) - myPos.y;
                    if ((dvx * dvx + dvy * dvy) < (8.0f * 8.0f)) {
                        warpPedIntoVehiclePassenger(localPed, partnerVeh, 0, localPed, true);
                        myVeh = partnerVeh;
                        break;
                    }
                }
            }
        }

        int32_t resolvedVehNetId = 0;
        if (myVeh) {
            uintptr_t myVehAddr = reinterpret_cast<uintptr_t>(myVeh);
            void* curDrv = *reinterpret_cast<void**>(myVehAddr + 0x5a0);
            me.seatIndex = (curDrv == localPed) ? 0 : 1;
            me.isAttachedOnRoof = false;
            me.vehicleModelId = static_cast<int32_t>(*reinterpret_cast<int16_t*>(myVehAddr + 0x32));
            if (config_.isHost && gtaSymbols_.cpoolsVehiclePool) {
                void* vPoolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsVehiclePool);
                if (vPoolPtr) {
                    uint8_t* vObjects = *reinterpret_cast<uint8_t**>(reinterpret_cast<uintptr_t>(vPoolPtr) + 0x00);
                    if (vObjects && myVehAddr >= reinterpret_cast<uintptr_t>(vObjects)) {
                        size_t slotIdx = (myVehAddr - reinterpret_cast<uintptr_t>(vObjects)) / 0xc68;
                        if (slotIdx < 256) resolvedVehNetId = 1000 + static_cast<int32_t>(slotIdx);
                    }
                }
            } else {
                for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                    if (worldSyncVehPtr_[v] == myVeh && worldSyncVehHostSlot_[v] >= 0) {
                        resolvedVehNetId = 1000 + static_cast<int32_t>(worldSyncVehHostSlot_[v]);
                        break;
                    }
                }
            }
            if (resolvedVehNetId == 0) resolvedVehNetId = 101;
        } else if (!me.isAttachedOnRoof) {
            me.seatIndex = -1;
        }
        me.vehicleNetId = myVeh ? resolvedVehNetId : (me.isAttachedOnRoof ? me.vehicleNetId : 0);
        me.cameraMode = 0; // Cámara 3D Independiente
        me.markerColor = (config_.localMarkerColor >= 0 && config_.localMarkerColor <= 6)
            ? static_cast<uint8_t>(config_.localMarkerColor)
            : ((localIdx == 0) ? 1u : 0u);

        // Project Kaizo Anti-Softlock pendiente en el hilo principal
        if (pendingAntiSoftlock_.exchange(false)) {
            float unstuckZ = myPos.z + 1.2f;
            applySmoothPedTransformAndAnim(localPed, myPos.x, myPos.y, unstuckZ, 0.0f, 0.0f, 0.0f, myHeading, 1, 200.0f);
            me.z = unstuckZ;
            me.cameraMode = 0;
        }

        // Respawn Táctico de Project Kaizo si el jugador local muere y hay punto táctico activo
        if (hasCustomRespawnPos_ && rawHealth <= 0.0f) {
            applySmoothPedTransformAndAnim(localPed, customRespawnPos_.x, customRespawnPos_.y, customRespawnPos_.z, 0.0f, 0.0f, 0.0f, myHeading, 1, 100.0f);
            *reinterpret_cast<float*>(localPedAddr + 0x6b4) = 100.0f;
        }

        // Publicar la Skin elegida del jugador local hacia los compañeros remotos.
        // REGLA ANTI-CRASH: NUNCA llamar a cpedSetModelIndexFn sobre localPed (FindPlayerPed(-1) / CPlayerPed)
        // porque cambiar el modelo del jugador local corrompe CClothes y crashea GTA SA.
        if (config_.localSkinModelId > 0) {
            me.skinModelId = config_.localSkinModelId;
        }

        // ====================================================================
        // EJECUCIÓN DE TRUCOS RÁPIDOS DESDE LA APP (coop_cheat_cmd.txt) EN CWorld::Process
        // ====================================================================
        if (pendingVehicleSpawnModel_ > 0 && gtaSymbols_.isModelLoaded(pendingVehicleSpawnModel_)) {
            if (gtaSymbols_.ccarCtrlCreateCarForScriptFn) {
                float rad = myHeading * 3.14159265f / 180.0f;
                CVector spawnPos{myPos.x - std::sin(rad) * 4.5f, myPos.y + std::cos(rad) * 4.5f, myPos.z + 0.6f};
                gtaSymbols_.ccarCtrlCreateCarForScriptFn(pendingVehicleSpawnModel_, spawnPos, 0);
            }
            pendingVehicleSpawnModel_ = 0;
        }
        if ((tickCount % 15) == 0 || !pendingRemoteCheatCmd_.empty()) {
            std::string cheatCmd;
            if (!pendingRemoteCheatCmd_.empty()) {
                cheatCmd = pendingRemoteCheatCmd_;
                pendingRemoteCheatCmd_.clear();
            } else {
                const char* cheatPaths[] = {
                    "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/coop_cheat_cmd.txt",
                    "/sdcard/Android_unprotected/data/com.rockstargames.gtasa/files/coop_cheat_cmd.txt"
                };
                for (const char* cp : cheatPaths) {
                    FILE* cf = fopen(cp, "r");
                    if (cf) {
                        char lineBuf[64] = {0};
                        if (fgets(lineBuf, sizeof(lineBuf), cf)) {
                            cheatCmd = trim(std::string(lineBuf));
                        }
                        fclose(cf);
                        ::remove(cp);
                        if (!cheatCmd.empty()) break;
                    }
                }
            }
            if (!cheatCmd.empty()) {
                for (char& c : cheatCmd) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (cheatCmd == "HESOYAM") {
                    if (gtaSymbols_.ccheatHealthCheatFn) {
                        gtaSymbols_.ccheatHealthCheatFn();
                    }
                    *reinterpret_cast<float*>(localPedAddr + 0x6ac) = 200.0f;
                    *reinterpret_cast<float*>(localPedAddr + 0x6b0) = 200.0f;
                    *reinterpret_cast<float*>(localPedAddr + 0x6b4) = 200.0f;
                    if (myVeh != nullptr) {
                        *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(myVeh) + 0x5c0) = 1000.0f;
                    }
                    me.health = 200;
                    me.armor = 200;
                } else if (cheatCmd == "LXGIWYL") {
                    if (gtaSymbols_.ccheatWeaponCheat1Fn) gtaSymbols_.ccheatWeaponCheat1Fn();
                } else if (cheatCmd == "KJKSZPJ") {
                    if (gtaSymbols_.ccheatWeaponCheat2Fn) gtaSymbols_.ccheatWeaponCheat2Fn();
                } else if (cheatCmd == "UZUMYMW") {
                    if (gtaSymbols_.ccheatWeaponCheat3Fn) gtaSymbols_.ccheatWeaponCheat3Fn();
                } else if (cheatCmd == "ROCKETMAN") {
                    if (gtaSymbols_.ccheatJetpackCheatFn) gtaSymbols_.ccheatJetpackCheatFn();
                } else if (cheatCmd == "AEZAKMI") {
                    if (gtaSymbols_.ccheatNotWantedCheatFn) gtaSymbols_.ccheatNotWantedCheatFn();
                } else if (cheatCmd == "OSRBLHH") {
                    if (gtaSymbols_.ccheatWantedCheatFn) gtaSymbols_.ccheatWantedCheatFn();
                } else if (cheatCmd == "AIWPRTON" || cheatCmd == "JUMPJET" || cheatCmd == "OHDUDE" ||
                           cheatCmd == "PDNEJOH" || cheatCmd == "FOURWHEELFUN") {
                    int vehModel = (cheatCmd == "AIWPRTON") ? 432 :
                                   (cheatCmd == "JUMPJET") ? 520 :
                                   (cheatCmd == "OHDUDE") ? 425 :
                                   (cheatCmd == "PDNEJOH") ? 494 : 471;
                    if (gtaSymbols_.ccheatVehicleCheatFn) {
                        gtaSymbols_.ccheatVehicleCheatFn(vehModel);
                    } else if (gtaSymbols_.cstreamingRequestModelFn) {
                        gtaSymbols_.cstreamingRequestModelFn(vehModel, 2);
                        if (gtaSymbols_.cstreamingLoadAllRequestedModelsFn) {
                            gtaSymbols_.cstreamingLoadAllRequestedModelsFn(false);
                        }
                        pendingVehicleSpawnModel_ = vehModel;
                    }
                } else if (cheatCmd == "AFZLLQLL") {
                    if (gtaSymbols_.ccheatSunnyCheatFn) gtaSymbols_.ccheatSunnyCheatFn();
                    if (gtaSymbols_.oldWeatherType) *gtaSymbols_.oldWeatherType = 0;
                    if (gtaSymbols_.newWeatherType) *gtaSymbols_.newWeatherType = 0;
                    lastWorldCheatSynced_ = cheatCmd;
                } else if (cheatCmd == "AUIFRVQS") {
                    if (gtaSymbols_.ccheatRainCheatFn) gtaSymbols_.ccheatRainCheatFn();
                    if (gtaSymbols_.oldWeatherType) *gtaSymbols_.oldWeatherType = 8;
                    if (gtaSymbols_.newWeatherType) *gtaSymbols_.newWeatherType = 8;
                    lastWorldCheatSynced_ = cheatCmd;
                } else if (cheatCmd == "IOJUFZN") {
                    if (gtaSymbols_.ccheatRiotCheatFn) gtaSymbols_.ccheatRiotCheatFn();
                    lastWorldCheatSynced_ = cheatCmd;
                }
                lastAppliedCheatCmd_ = cheatCmd;
                LOGI("Applied in-app cheat on CWorld::Process thread: %s", cheatCmd.c_str());
            }
        }

        // ====================================================================
        // SINCRONIZACIÓN DE RELOJ (HORA), CLIMA, PEATONES Y VEHÍCULOS DEL MUNDO (HOST <-> GUEST)
        // ====================================================================
        if (!initialAlleyFreeRoamDone_ && std::fabs(me.x - 970.9f) < 2.5f && std::fabs(me.y - (-1108.1f)) < 2.5f) {
            if (++alleyLandWaitTicks_ >= 20) {
                if (gtaSymbols_.onAMissionFlagOffset) {
                    *gtaSymbols_.onAMissionFlagOffset = 0;
                }
                onMission = false;
                float targetX = 967.2f;
                float targetY = config_.isHost ? -1107.8f : -1105.9f;
                float targetZ = 23.98f;
                if (gtaSymbols_.cpedTeleportFn) {
                    gtaSymbols_.cpedTeleportFn(localPed, CVector{targetX, targetY, targetZ}, 0);
                }
                me.x = targetX;
                me.y = targetY;
                me.z = targetZ;
                initialAlleyFreeRoamDone_ = true;
                if (config_.isHost) {
                    initialSyncTeleportDone_ = true;
                }
                LOGI("Positioned %s CJ in open alley courtyard at (%.1f, %.1f, %.1f) facing +X!",
                     config_.isHost ? "HOST" : "GUEST", targetX, targetY, targetZ);
            }
        }

        if (config_.isHost) {
            if (gtaSymbols_.dontCreateRandomCops) *gtaSymbols_.dontCreateRandomCops = 0;
            if (gtaSymbols_.dontCreateRandomGangMembers) *gtaSymbols_.dontCreateRandomGangMembers = 0;
            if (gtaSymbols_.pedDensityMultiplier && *gtaSymbols_.pedDensityMultiplier < 0.5f) {
                *gtaSymbols_.pedDensityMultiplier = 1.0f;
            }
            if (gtaSymbols_.maxNumberOfPedsInUse && *gtaSymbols_.maxNumberOfPedsInUse < 12) {
                *gtaSymbols_.maxNumberOfPedsInUse = 25;
            }
            if (gtaSymbols_.carDensityMultiplier && *gtaSymbols_.carDensityMultiplier < 0.5f) {
                *gtaSymbols_.carDensityMultiplier = 1.0f;
            }
            if (gtaSymbols_.maxNumberOfCarsInUse && *gtaSymbols_.maxNumberOfCarsInUse < 8) {
                *gtaSymbols_.maxNumberOfCarsInUse = 12;
            }

            mission_.onMissionFlag = onMission ? 1 : 0;
            if (gtaSymbols_.clockHours) worldState_.clockHours = *gtaSymbols_.clockHours;
            if (gtaSymbols_.clockMinutes) worldState_.clockMinutes = *gtaSymbols_.clockMinutes;
            if (gtaSymbols_.clockSeconds) worldState_.clockSeconds = *gtaSymbols_.clockSeconds;
            if (gtaSymbols_.oldWeatherType) worldState_.oldWeather = *gtaSymbols_.oldWeatherType;
            if (gtaSymbols_.newWeatherType) worldState_.newWeather = *gtaSymbols_.newWeatherType;

            // Escanear CPools::ms_pPedPool y CPools::ms_pVehiclePool en cada frame con histéresis de slot para evitar parpadeos
            {
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
                                // Excluir personajes de misión (m_nCreatedBy == 2 en +0x548) y skins de historia/misión (265..299: Smoke, Sweet, Ryder, etc.)
                                // para evitar duplicar personajes de misión cuando ambos jugadores entran a la misma misión por separado
                                uint8_t pedCreatedBy = *reinterpret_cast<uint8_t*>(pAddr + 0x548);
                                if (pedCreatedBy == 2) continue;
                                int16_t pModelId = *reinterpret_cast<int16_t*>(pAddr + 0x32);
                                if (pModelId <= 0 || pModelId >= 265) continue;

                                float pHealth = *reinterpret_cast<float*>(pAddr + 0x6ac);
                                if (!std::isfinite(pHealth) || pHealth <= 0.0f) continue;

                                uintptr_t pMat = *reinterpret_cast<uintptr_t*>(pAddr + 0x18);
                                if (pMat == 0) continue;
                                float px = *reinterpret_cast<float*>(pMat + 0x30);
                                float py = *reinterpret_cast<float*>(pMat + 0x34);
                                float pz = *reinterpret_cast<float*>(pMat + 0x38);
                                if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) continue;

                                float dSq = 999999.0f;
                                bool behindMovingPlayer = false;
                                for (int pIdx = 0; pIdx < kMaxCoopPlayers; ++pIdx) {
                                    if (!players_[pIdx].isActive || !players_[pIdx].hasRealGtaCoords) continue;
                                    float pVx = players_[pIdx].vx;
                                    float pVy = players_[pIdx].vy;
                                    float pSpdSq = pVx * pVx + pVy * pVy;
                                    float lookX = players_[pIdx].x + pVx * 38.0f;
                                    float lookY = players_[pIdx].y + pVy * 38.0f;
                                    float dx = px - lookX;
                                    float dy = py - lookY;
                                    float dz = pz - players_[pIdx].z;
                                    float candDSq = dx * dx + dy * dy + dz * dz;
                                    if (pSpdSq > 0.0025f) {
                                        float relX = px - players_[pIdx].x;
                                        float relY = py - players_[pIdx].y;
                                        if (relX * pVx + relY * pVy < 0.0f) {
                                            candDSq *= 6.0f;
                                            behindMovingPlayer = true;
                                        }
                                    }
                                    dSq = std::min(dSq, candDSq);
                                }
                                if (dSq > (130.0f * 130.0f)) continue;

                                // Bono de histéresis (0.25x distSq) para peatones ya sincronizados que no hayan quedado atrás
                                if (!behindMovingPlayer) {
                                    for (int prevK = 0; prevK < kMaxSyncedWorldPeds; ++prevK) {
                                        if (worldState_.peds[prevK].active && worldState_.peds[prevK].hostPoolSlot == static_cast<int16_t>(s)) {
                                            dSq *= 0.25f;
                                            break;
                                        }
                                    }
                                }

                                SyncedWorldPedEntry e{};
                                e.active = 1;
                                e.ownerSlot = findClosestOwnerSlotLocked(px, py, pz);
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
                                if (e.moveState >= 4 && (e.vx * e.vx + e.vy * e.vy) < (0.008f * 0.008f)) {
                                    float animSpd = (e.moveState == 7) ? 0.11f : ((e.moveState == 6) ? 0.075f : 0.026f);
                                    e.vx = -std::sin(e.heading) * animSpd;
                                    e.vy =  std::cos(e.heading) * animSpd;
                                }
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

                        // Ordenar por hostPoolSlot para evitar que el reordenamiento por distancia intercambie slots en el Guest
                        for (int a = 0; a < foundCount - 1; ++a) {
                            for (int b = a + 1; b < foundCount; ++b) {
                                if (best[b].entry.hostPoolSlot < best[a].entry.hostPoolSlot) {
                                    std::swap(best[a], best[b]);
                                }
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

                                // Excluir vehículos destruidos/explotados (+0x634 = CVehicle::m_fHealth en arm64 v2.10)
                                float vHealth = *reinterpret_cast<float*>(vAddr + 0x634);
                                if (!std::isfinite(vHealth) || vHealth <= 50.0f) continue;

                                uintptr_t vMat = *reinterpret_cast<uintptr_t*>(vAddr + 0x18);
                                if (vMat == 0) continue;
                                float vxPos = *reinterpret_cast<float*>(vMat + 0x30);
                                float vyPos = *reinterpret_cast<float*>(vMat + 0x34);
                                float vzPos = *reinterpret_cast<float*>(vMat + 0x38);
                                if (!std::isfinite(vxPos) || !std::isfinite(vyPos) || !std::isfinite(vzPos)) continue;

                                float dSq = 999999.0f;
                                bool vehBehindMovingPlayer = false;
                                for (int pIdx = 0; pIdx < kMaxCoopPlayers; ++pIdx) {
                                    if (!players_[pIdx].isActive || !players_[pIdx].hasRealGtaCoords) continue;
                                    float pVx = players_[pIdx].vx;
                                    float pVy = players_[pIdx].vy;
                                    float pSpdSq = pVx * pVx + pVy * pVy;
                                    float lookX = players_[pIdx].x + pVx * 42.0f;
                                    float lookY = players_[pIdx].y + pVy * 42.0f;
                                    float dx = vxPos - lookX;
                                    float dy = vyPos - lookY;
                                    float dz = vzPos - players_[pIdx].z;
                                    float candDSq = dx * dx + dy * dy + dz * dz;
                                    if (pSpdSq > 0.0025f) {
                                        float relX = vxPos - players_[pIdx].x;
                                        float relY = vyPos - players_[pIdx].y;
                                        if (relX * pVx + relY * pVy < 0.0f) {
                                            candDSq *= 6.0f;
                                            vehBehindMovingPlayer = true;
                                        }
                                    }
                                    dSq = std::min(dSq, candDSq);
                                }
                                bool isCoopOccupiedVeh = (pVeh == myVeh);
                                if (isCoopOccupiedVeh) dSq = 0.0f; // Prioridad absoluta al vehículo ocupado por el jugador local
                                for (int r = 0; r < kMaxCoopPlayers; ++r) {
                                    if (players_[r].isActive && players_[r].vehicleNetId == (1000 + s)) {
                                        dSq = 0.0f; // Prioridad absoluta a vehículos ocupados por compañeros remotos
                                        isCoopOccupiedVeh = true;
                                    }
                                }
                                if (dSq > (160.0f * 160.0f)) continue;

                                // Bono de histéresis (0.25x distSq) para vehículos ya sincronizados que no hayan quedado atrás
                                // para evitar que el intercambio de slots destruya y recree el auto con otro color
                                if (!vehBehindMovingPlayer || isCoopOccupiedVeh) {
                                    for (int prevV = 0; prevV < kMaxSyncedWorldVehicles; ++prevV) {
                                        if (worldState_.vehicles[prevV].active && worldState_.vehicles[prevV].hostPoolSlot == static_cast<int16_t>(s)) {
                                            dSq *= 0.25f;
                                            break;
                                        }
                                    }
                                }

                                // +0x5a0 = CVehicle::m_pDriver en arm64 v2.10 (verificado en CVehicle::SetDriver 0x6a8250)
                                void* pDriver = *reinterpret_cast<void**>(vAddr + 0x5a0);
                                int16_t drvModel = -1;
                                float dxLoc = vxPos - myPos.x;
                                float dyLoc = vyPos - myPos.y;
                                bool hostCarjackingThisVeh = (localTryingToEnterCar && (dxLoc * dxLoc + dyLoc * dyLoc) < (7.5f * 7.5f));
                                if (pDriver && pDriver != localPed && !hostCarjackingThisVeh) {
                                    bool isRemPlayer = false;
                                    for (int r = 0; r < kMaxCoopPlayers; ++r) {
                                        if (pDriver == remotePedPtr_[r] ||
                                            (players_[r].isActive && players_[r].seatIndex == 0 && players_[r].vehicleNetId == (1000 + s))) {
                                            isRemPlayer = true;
                                        }
                                    }
                                    uintptr_t drvAddr = reinterpret_cast<uintptr_t>(pDriver);
                                    bool drvStillInVeh = ((*reinterpret_cast<uint8_t*>(drvAddr + 0x585) & 1) != 0);
                                    if (!isRemPlayer && drvStillInVeh) {
                                        drvModel = *reinterpret_cast<int16_t*>(drvAddr + 0x32);
                                        if (drvModel <= 0) drvModel = 7;
                                    }
                                }

                                SyncedWorldVehicleEntry ve{};
                                ve.active = 1;
                                ve.ownerSlot = findClosestOwnerSlotLocked(vxPos, vyPos, vzPos);
                                ve.hostPoolSlot = static_cast<int16_t>(s);
                                ve.modelId = vModel;
                                ve.driverModelId = drvModel;
                                ve.packedColors = *reinterpret_cast<uint32_t*>(vAddr + 0x574);
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

                        // Ordenar por hostPoolSlot para mantener correspondencia 1:1 estable en el Guest
                        for (int a = 0; a < foundVehCount - 1; ++a) {
                            for (int b = a + 1; b < foundVehCount; ++b) {
                                if (bestVeh[b].entry.hostPoolSlot < bestVeh[a].entry.hostPoolSlot) {
                                    std::swap(bestVeh[a], bestVeh[b]);
                                }
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

            // Calcular distancia del Guest respecto al Host (Slot 1 = índice 0)
            float hostDx = players_[0].x - me.x;
            float hostDy = players_[0].y - me.y;
            float distToHostSq = (players_[0].hasRealGtaCoords)
                ? (hostDx * hostDx + hostDy * hostDy)
                : 0.0f;
            bool guestFarFromHost = (distToHostSq > (65.0f * 65.0f));

            // Si el Guest está cerca del Host (< 65m), suprimir generación aleatoria local para mostrar sólo entidades sincronizadas.
            // Si el Guest se aleja (> 65m), habilitar población local normal de peatones alrededor del Guest para que la ciudad esté viva.
            if (gtaSymbols_.dontCreateRandomCops) *gtaSymbols_.dontCreateRandomCops = 1;
            if (gtaSymbols_.dontCreateRandomGangMembers) *gtaSymbols_.dontCreateRandomGangMembers = 1;
            if (guestFarFromHost) {
                if (gtaSymbols_.pedDensityMultiplier) *gtaSymbols_.pedDensityMultiplier = 1.2f;
                if (gtaSymbols_.maxNumberOfPedsInUse) *gtaSymbols_.maxNumberOfPedsInUse = 15;
                if (gtaSymbols_.carDensityMultiplier) *gtaSymbols_.carDensityMultiplier = 0.0f;
                if (gtaSymbols_.maxNumberOfCarsInUse) *gtaSymbols_.maxNumberOfCarsInUse = 0;
            } else {
                if (gtaSymbols_.pedDensityMultiplier) *gtaSymbols_.pedDensityMultiplier = 0.0f;
                if (gtaSymbols_.maxNumberOfPedsInUse) *gtaSymbols_.maxNumberOfPedsInUse = 0;
                if (gtaSymbols_.carDensityMultiplier) *gtaSymbols_.carDensityMultiplier = 0.0f;
                if (gtaSymbols_.maxNumberOfCarsInUse) *gtaSymbols_.maxNumberOfCarsInUse = 0;
            }

            // IMPORTANTE: NUNCA invocar CStreaming::LoadAllRequestedModels desde CWorld::Process para evitar
            // condición de carrera con CdStreamThread (OS_FileSetPosition). CStreaming::Update carga los modelos
            // solicitados por RequestModel de forma asíncrona y segura en el siguiente frame.
            bool canRequestStreamingThisTick = (tickCount % 5 == 0);
            float mySpeed2dSq = me.vx * me.vx + me.vy * me.vy;

            // 1. Instanciar y actualizar en el Guest los peatones reales sincronizados desde el Host (NUNCA modelo 0 / CJ ni personajes de misión 265..299)
            for (int k = 0; k < kMaxSyncedWorldPeds; ++k) {
                const SyncedWorldPedEntry& wp = worldState_.peds[k];
                bool validPedEntry = (wp.active != 0 && wp.modelId > 0 && wp.modelId < 265);
                if (validPedEntry && gtaSymbols_.cpoolsPedPool) {
                    void* poolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsPedPool);
                    if (poolPtr) {
                        uintptr_t poolAddr = reinterpret_cast<uintptr_t>(poolPtr);
                        uint8_t* objects = *reinterpret_cast<uint8_t**>(poolAddr + 0x00);
                        uint8_t* byteMap = *reinterpret_cast<uint8_t**>(poolAddr + 0x08);
                        int32_t poolSize = *reinterpret_cast<int32_t*>(poolAddr + 0x10);
                        if (objects && byteMap && poolSize > 0 && poolSize <= 256) {
                            for (int32_t s = 0; s < poolSize; ++s) {
                                if ((byteMap[s] & 0x80) != 0) continue;
                                void* candPed = reinterpret_cast<void*>(objects + static_cast<size_t>(s) * 0x9c8);
                                if (candPed == localPed) continue;
                                bool isOurManagedPed = false;
                                for (int r = 0; r < kMaxCoopPlayers; ++r) {
                                    if (candPed == remotePedPtr_[r]) isOurManagedPed = true;
                                }
                                for (int sp = 0; sp < kMaxSyncedWorldPeds; ++sp) {
                                    if (candPed == worldSyncPedPtr_[sp]) isOurManagedPed = true;
                                }
                                for (int sv = 0; sv < kMaxSyncedWorldVehicles; ++sv) {
                                    if (candPed == worldSyncVehDriverPtr_[sv]) isOurManagedPed = true;
                                }
                                if (isOurManagedPed) continue;
                                uintptr_t cAddr = reinterpret_cast<uintptr_t>(candPed);
                                int16_t cModel = *reinterpret_cast<int16_t*>(cAddr + 0x32);
                                uint8_t cCreatedBy = *reinterpret_cast<uint8_t*>(cAddr + 0x548);
                                uintptr_t cMat = *reinterpret_cast<uintptr_t*>(cAddr + 0x18);
                                if (cMat == 0) continue;
                                float cdx = *reinterpret_cast<float*>(cMat + 0x30) - wp.x;
                                float cdy = *reinterpret_cast<float*>(cMat + 0x34) - wp.y;
                                float cdSq = cdx * cdx + cdy * cdy;
                                // Si una misión local ya instanció este mismo personaje o un MISSION_CHAR en esta ubicación, no duplicarlo
                                if ((cModel == wp.modelId && cdSq < (35.0f * 35.0f)) ||
                                    (cCreatedBy == 2 && cdSq < (14.0f * 14.0f))) {
                                    validPedEntry = false;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (validPedEntry && (worldSyncPedPtr_[k] == nullptr || worldSyncPedHostSlot_[k] != wp.hostPoolSlot)) {
                    for (int otherK = k + 1; otherK < kMaxSyncedWorldPeds; ++otherK) {
                        if (worldSyncPedPtr_[otherK] != nullptr &&
                            (worldSyncPedHostSlot_[otherK] == wp.hostPoolSlot ||
                             (worldSyncPedModel_[otherK] == wp.modelId &&
                              worldState_.peds[otherK].hostPoolSlot != worldSyncPedHostSlot_[otherK]))) {
                            std::swap(worldSyncPedPtr_[k], worldSyncPedPtr_[otherK]);
                            std::swap(worldSyncPedHandle_[k], worldSyncPedHandle_[otherK]);
                            std::swap(worldSyncPedHostSlot_[k], worldSyncPedHostSlot_[otherK]);
                            std::swap(worldSyncPedModel_[k], worldSyncPedModel_[otherK]);
                            break;
                        }
                    }
                }
                void* wPed = worldSyncPedPtr_[k];
                if (wPed && gtaSymbols_.cpoolsGetPedFn && worldSyncPedHandle_[k] > 0) {
                    if (gtaSymbols_.cpoolsGetPedFn(worldSyncPedHandle_[k]) != wPed) {
                        wPed = nullptr;
                        worldSyncPedPtr_[k] = nullptr;
                        worldSyncPedHandle_[k] = -1;
                        worldSyncPedModel_[k] = -1;
                    }
                }

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
                            gtaSymbols_.cpedTeleportFn(wPed, CVector{0.0f, 0.0f, -500.0f}, 0);
                        }
                        wPed = nullptr;
                        worldSyncPedPtr_[k] = nullptr;
                        worldSyncPedHandle_[k] = -1;
                        worldSyncPedModel_[k] = -1;
                    }
                    if (!validPedEntry) continue;
                }

                if (!wPed) {
                    if (!exactPedModelLoaded) continue;
                    // Evitar pop-in brusco de peatones nuevos justo delante del capó cuando conducimos un vehículo
                    float pdx = wp.x - me.x;
                    float pdy = wp.y - me.y;
                    float pDistSq = pdx * pdx + pdy * pdy;
                    if (myVeh != nullptr && mySpeed2dSq > (0.04f * 0.04f)) {
                        float dotFwd = pdx * me.vx + pdy * me.vy;
                        if ((dotFwd > 0.0f && pDistSq < (32.0f * 32.0f)) || pDistSq < (14.0f * 14.0f)) {
                            continue;
                        }
                    }
                    int16_t spawnPedModel = wp.modelId;
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
                    if (wp.moveState >= 4 && (nowMs - worldState_.lastNetworkUpdateMs) < 120) {
                        worldState_.peds[k].x += wp.vx * 0.5f;
                        worldState_.peds[k].y += wp.vy * 0.5f;
                    }
                    applySmoothPedTransformAndAnim(
                        wPed, worldState_.peds[k].x, worldState_.peds[k].y, wp.z,
                        wp.vx, wp.vy, wp.vz, wp.heading,
                        static_cast<int>(wp.moveState), static_cast<float>(wp.health)
                    );
                }
            }

            // 2. Instanciar y actualizar en el Guest los vehículos sincronizados desde el Host
            //    (Sincronizando colores exactos +0x574 y conductor NPC único por vehículo sin duplicación al robarlo)
            for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                const SyncedWorldVehicleEntry& ve = worldState_.vehicles[v];
                bool validVehEntry = (ve.active != 0 && ve.modelId >= 400 && ve.modelId <= 611);
                if (validVehEntry && (worldSyncVehPtr_[v] == nullptr || worldSyncVehHostSlot_[v] != ve.hostPoolSlot)) {
                    for (int otherV = v + 1; otherV < kMaxSyncedWorldVehicles; ++otherV) {
                        if (worldSyncVehPtr_[otherV] != nullptr &&
                            worldSyncVehHostSlot_[otherV] == ve.hostPoolSlot) {
                            std::swap(worldSyncVehPtr_[v], worldSyncVehPtr_[otherV]);
                            std::swap(worldSyncVehHandle_[v], worldSyncVehHandle_[otherV]);
                            std::swap(worldSyncVehHostSlot_[v], worldSyncVehHostSlot_[otherV]);
                            std::swap(worldSyncVehModel_[v], worldSyncVehModel_[otherV]);
                            std::swap(worldSyncVehDriverPtr_[v], worldSyncVehDriverPtr_[otherV]);
                            std::swap(worldSyncVehDriverHandle_[v], worldSyncVehDriverHandle_[otherV]);
                            std::swap(worldSyncVehHadDriver_[v], worldSyncVehHadDriver_[otherV]);
                            break;
                        }
                    }
                }
                void* wVeh = worldSyncVehPtr_[v];
                if (wVeh && gtaSymbols_.cpoolsGetVehicleFn && worldSyncVehHandle_[v] > 0) {
                    if (gtaSymbols_.cpoolsGetVehicleFn(worldSyncVehHandle_[v]) != wVeh) {
                        wVeh = nullptr;
                        worldSyncVehPtr_[v] = nullptr;
                        worldSyncVehHandle_[v] = -1;
                        worldSyncVehModel_[v] = -1;
                        worldSyncVehDriverPtr_[v] = nullptr;
                        worldSyncVehDriverHandle_[v] = -1;
                        worldSyncVehHadDriver_[v] = false;
                    }
                }

                bool exactVehModelLoaded = validVehEntry && gtaSymbols_.isModelLoaded(ve.modelId);
                if (validVehEntry && !exactVehModelLoaded && canRequestStreamingThisTick && gtaSymbols_.cstreamingRequestModelFn) {
                    gtaSymbols_.cstreamingRequestModelFn(ve.modelId, 2);
                }

                if (!validVehEntry || (wVeh && worldSyncVehModel_[v] != ve.modelId && wVeh != myVeh)) {
                    if (wVeh && wVeh != myVeh) {
                        if (worldSyncVehDriverPtr_[v] && gtaSymbols_.cpoolsGetPedFn && worldSyncVehDriverHandle_[v] > 0 &&
                            gtaSymbols_.cpoolsGetPedFn(worldSyncVehDriverHandle_[v]) == worldSyncVehDriverPtr_[v]) {
                            evictNpcPedFromWorld(worldSyncVehDriverPtr_[v], localPed);
                        }
                        evictVehicleFromWorld(wVeh, localPed);
                        wVeh = nullptr;
                        worldSyncVehPtr_[v] = nullptr;
                        worldSyncVehHandle_[v] = -1;
                        worldSyncVehModel_[v] = -1;
                        worldSyncVehDriverPtr_[v] = nullptr;
                        worldSyncVehDriverHandle_[v] = -1;
                        worldSyncVehHadDriver_[v] = false;
                    }
                    if (!validVehEntry) continue;
                }

                if (!wVeh) {
                    if (!exactVehModelLoaded) continue;
                    bool occupiedByCoopPartner = false;
                    for (int r = 0; r < kMaxCoopPlayers; ++r) {
                        if (r != localIdx && players_[r].isActive && players_[r].vehicleNetId == (1000 + ve.hostPoolSlot)) {
                            occupiedByCoopPartner = true;
                            break;
                        }
                    }
                    if (!occupiedByCoopPartner) {
                        // Evitar pop-in brusco de vehículos nuevos justo delante del auto que conducimos
                        float vdx = ve.x - me.x;
                        float vdy = ve.y - me.y;
                        float vDistSq = vdx * vdx + vdy * vdy;
                        if (myVeh != nullptr && mySpeed2dSq > (0.04f * 0.04f)) {
                            float dotFwd = vdx * me.vx + vdy * me.vy;
                            if ((dotFwd > 0.0f && vDistSq < (35.0f * 35.0f)) || vDistSq < (16.0f * 16.0f)) {
                                continue;
                            }
                        }
                        // Evitar duplicar vehículos de misión si el script local del Guest ya creó el mismo vehículo cerca
                        if (gtaSymbols_.cpoolsVehiclePool) {
                            void* vPoolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsVehiclePool);
                            if (vPoolPtr) {
                                uintptr_t vPoolAddr = reinterpret_cast<uintptr_t>(vPoolPtr);
                                uint8_t* vObjects = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x00);
                                uint8_t* vByteMap = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x08);
                                int32_t vPoolSize = *reinterpret_cast<int32_t*>(vPoolAddr + 0x10);
                                bool nativeDuplicateFound = false;
                                if (vObjects && vByteMap && vPoolSize > 0 && vPoolSize <= 256) {
                                    for (int32_t s = 0; s < vPoolSize; ++s) {
                                        if ((vByteMap[s] & 0x80) != 0) continue;
                                        void* candVeh = reinterpret_cast<void*>(vObjects + static_cast<size_t>(s) * 0xc68);
                                        bool isManagedVeh = false;
                                        for (int sv = 0; sv < kMaxSyncedWorldVehicles; ++sv) {
                                            if (candVeh == worldSyncVehPtr_[sv]) isManagedVeh = true;
                                        }
                                        if (isManagedVeh) continue;
                                        uintptr_t cvAddr = reinterpret_cast<uintptr_t>(candVeh);
                                        int16_t cvModel = *reinterpret_cast<int16_t*>(cvAddr + 0x32);
                                        uintptr_t cvMat = *reinterpret_cast<uintptr_t*>(cvAddr + 0x18);
                                        if (cvMat != 0 && cvModel == ve.modelId) {
                                            float cdx = *reinterpret_cast<float*>(cvMat + 0x30) - ve.x;
                                            float cdy = *reinterpret_cast<float*>(cvMat + 0x34) - ve.y;
                                            if ((cdx * cdx + cdy * cdy) < (22.0f * 22.0f)) {
                                                nativeDuplicateFound = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (nativeDuplicateFound) continue;
                            }
                        }
                    }
                    int16_t spawnVehModel = ve.modelId;
                    if (gtaSymbols_.ccarCtrlCreateCarForScriptFn) {
                        void* newVeh = gtaSymbols_.ccarCtrlCreateCarForScriptFn(spawnVehModel, CVector{ve.x, ve.y, ve.z}, 0);
                        if (newVeh) {
                            uintptr_t newVehAddr = reinterpret_cast<uintptr_t>(newVeh);
                            *reinterpret_cast<float*>(newVehAddr + 0x634) = 1000.0f; // m_fHealth
                            *reinterpret_cast<uint32_t*>(newVehAddr + 0x574) = ve.packedColors; // Colores exactos del Host
                            *reinterpret_cast<uint8_t*>(newVehAddr + 0x610) = 2; // MISSION_VEHICLE
                            worldSyncVehPtr_[v] = newVeh;
                            worldSyncVehHandle_[v] = gtaSymbols_.cpoolsGetVehicleRefFn ? gtaSymbols_.cpoolsGetVehicleRefFn(newVeh) : -1;
                            worldSyncVehHostSlot_[v] = ve.hostPoolSlot;
                            worldSyncVehModel_[v] = spawnVehModel;
                            worldSyncVehDriverPtr_[v] = nullptr;
                            worldSyncVehDriverHandle_[v] = -1;
                            worldSyncVehHadDriver_[v] = false;
                            wVeh = newVeh;
                        }
                    }
                }

                if (wVeh) {
                    if (worldSyncVehHostSlot_[v] != ve.hostPoolSlot) {
                        if (worldSyncVehDriverPtr_[v] && gtaSymbols_.cpoolsGetPedFn && worldSyncVehDriverHandle_[v] > 0 &&
                            gtaSymbols_.cpoolsGetPedFn(worldSyncVehDriverHandle_[v]) == worldSyncVehDriverPtr_[v]) {
                            evictNpcPedFromWorld(worldSyncVehDriverPtr_[v], localPed);
                        }
                        worldSyncVehDriverPtr_[v] = nullptr;
                        worldSyncVehDriverHandle_[v] = -1;
                        worldSyncVehHadDriver_[v] = false;
                    }
                    worldSyncVehHostSlot_[v] = ve.hostPoolSlot;
                    uintptr_t wVehAddr = reinterpret_cast<uintptr_t>(wVeh);
                    *reinterpret_cast<float*>(wVehAddr + 0x634) = 1000.0f; // m_fHealth
                    *reinterpret_cast<uint32_t*>(wVehAddr + 0x574) = ve.packedColors; // Mantener color idéntico al Host

                    uintptr_t wvMat = *reinterpret_cast<uintptr_t*>(wVehAddr + 0x18);
                    float wvx = wvMat ? *reinterpret_cast<float*>(wvMat + 0x30) : ve.x;
                    float wvy = wvMat ? *reinterpret_cast<float*>(wvMat + 0x34) : ve.y;
                    float dxLoc = wvx - myPos.x;
                    float dyLoc = wvy - myPos.y;
                    bool localCarjackingThisVeh = (localTryingToEnterCar && (dxLoc * dxLoc + dyLoc * dyLoc) < (8.5f * 8.5f));

                    // Garantizar que todo vehículo NPC tenga a lo sumo 1 conductor instanciado (nunca re-spawnear tras ser bajado)
                    if (wVeh != myVeh) {
                        bool remDrivingThisVeh = false;
                        for (int r = 0; r < kMaxCoopPlayers; ++r) {
                            if (r != localIdx && players_[r].isActive && players_[r].seatIndex == 0 &&
                                players_[r].vehicleNetId == (1000 + ve.hostPoolSlot)) {
                                remDrivingThisVeh = true;
                                break;
                            }
                        }
                        void* curDrv = *reinterpret_cast<void**>(wVehAddr + 0x5a0);
                        if (!remDrivingThisVeh && !localCarjackingThisVeh && !worldSyncVehHadDriver_[v] &&
                            ve.driverModelId > 0 && curDrv == nullptr) {
                            if (canRequestStreamingThisTick && gtaSymbols_.cstreamingRequestModelFn &&
                                !gtaSymbols_.isModelLoaded(ve.driverModelId)) {
                                gtaSymbols_.cstreamingRequestModelFn(ve.driverModelId, 2);
                            }
                            int16_t drvModelToSpawn = gtaSymbols_.isModelLoaded(ve.driverModelId) ? ve.driverModelId : -1;
                            if (drvModelToSpawn < 0) {
                                for (int16_t m = 7; m <= 288; ++m) {
                                    if (gtaSymbols_.isModelLoaded(m)) {
                                        drvModelToSpawn = m;
                                        break;
                                    }
                                }
                            }
                            if (drvModelToSpawn > 0 && gtaSymbols_.cpedOperatorNewFn &&
                                gtaSymbols_.ccivilianPedCtorFn && gtaSymbols_.cworldAddFn) {
                                void* drv = gtaSymbols_.cpedOperatorNewFn(0x9c8);
                                if (drv) {
                                    gtaSymbols_.ccivilianPedCtorFn(drv, 4 /*PED_TYPE_CIVMALE*/, static_cast<uint32_t>(drvModelToSpawn));
                                    if (gtaSymbols_.cpedSetCharCreatedByFn) {
                                        gtaSymbols_.cpedSetCharCreatedByFn(drv, 2 /*MISSION_CHAR*/);
                                    }
                                    uintptr_t drvAddr = reinterpret_cast<uintptr_t>(drv);
                                    *reinterpret_cast<float*>(drvAddr + 0x6ac) = 100.0f;
                                    *reinterpret_cast<float*>(drvAddr + 0x6b0) = 100.0f;
                                    gtaSymbols_.cworldAddFn(drv);
                                    warpPedIntoVehicleDriver(drv, wVeh, localPed, false);
                                    worldSyncVehDriverPtr_[v] = drv;
                                    worldSyncVehDriverHandle_[v] = gtaSymbols_.cpoolsGetPedRefFn ? gtaSymbols_.cpoolsGetPedRefFn(drv) : -1;
                                    worldSyncVehHadDriver_[v] = true;
                                }
                            }
                        } else if ((ve.driverModelId <= 0 || remDrivingThisVeh) && curDrv != nullptr && curDrv != localPed) {
                            bool isRemPlayer = false;
                            for (int r = 0; r < kMaxCoopPlayers; ++r) {
                                if (curDrv == remotePedPtr_[r]) isRemPlayer = true;
                            }
                            if (!isRemPlayer) {
                                evictNpcPedFromWorld(curDrv, localPed);
                                worldSyncVehDriverPtr_[v] = nullptr;
                                worldSyncVehDriverHandle_[v] = -1;
                            }
                        } else if (localCarjackingThisVeh && worldSyncVehHadDriver_[v] && curDrv == nullptr) {
                            // El jugador local (Guest) acaba de bajar al conductor de este vehículo para subir:
                            // reclamar inmediatamente el vehículo ante el Host para que el Host retire a su conductor NPC
                            me.seatIndex = 0;
                            me.vehicleNetId = 1000 + static_cast<int32_t>(ve.hostPoolSlot);
                            me.vehicleModelId = ve.modelId;
                        }
                    }

                    // Si el vehículo es nuestro pero somos pasajeros (me.seatIndex > 0) o no estamos en él (ni robándolo),
                    // aplicar la posición sincronizada del Host/conductor
                    if ((wVeh != myVeh && !localCarjackingThisVeh) || me.seatIndex > 0) {
                        applySmoothVehicleTransform(wVeh, ve);
                    }
                }
            }

            // Limpiar cada 15 frames vehículos aleatorios no sincronizados y la zona subterránea de descarte
            if (tickCount % 15 == 0) {
                if (!guestFarFromHost && gtaSymbols_.cpopulationRemoveAllRandomPedsFn) {
                    gtaSymbols_.cpopulationRemoveAllRandomPedsFn();
                }
                if (gtaSymbols_.cworldClearCarsFromAreaFn) {
                    gtaSymbols_.cworldClearCarsFromAreaFn(
                        me.x - 300.0f, me.y - 300.0f, -100.0f,
                        me.x + 300.0f, me.y + 300.0f, 500.0f
                    );
                    gtaSymbols_.cworldClearCarsFromAreaFn(
                        -200.0f, -200.0f, -600.0f,
                        200.0f, 200.0f, -400.0f
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

            // Usar las coordenadas exactas del jugador remoto sin burbuja de repulsión artificial
            float targetRemX = rem.x;
            float targetRemY = rem.y;
            float targetRemZ = rem.z;

            // Verificar si el CPed remoto sigue válido en CPools::ms_pPedPool
            void* ped = remotePedPtr_[i];
            if (ped && gtaSymbols_.cpoolsGetPedFn && remotePedHandle_[i] > 0) {
                if (gtaSymbols_.cpoolsGetPedFn(remotePedHandle_[i]) != ped) {
                    if (remoteBlipHandle_[i] > 0 && gtaSymbols_.cradarClearBlipFn) {
                        gtaSymbols_.cradarClearBlipFn(remoteBlipHandle_[i]);
                    }
                    ped = nullptr;
                    remotePedPtr_[i] = nullptr;
                    remotePedHandle_[i] = -1;
                    remoteBlipHandle_[i] = -1;
                    remoteBlipColor_[i] = -1;
                }
            }

            uint32_t desiredSlotColor = (rem.markerColor <= 6)
                ? static_cast<uint32_t>(rem.markerColor)
                : ((i == 0) ? 1u : 0u);

            // Si el CPed remoto aún no fue creado en el mundo 3D, instanciarlo siguiendo la secuencia exacta de CREATE_CHAR (009A)
            if (!ped && gtaSymbols_.cpedOperatorNewFn && gtaSymbols_.ccivilianPedCtorFn && gtaSymbols_.cworldAddFn) {
                uint32_t spawnModel = 0;
                int32_t desiredRemModel = resolveSafePedModelId(rem.skinModelId);
                if (desiredRemModel > 0) {
                    if (gtaSymbols_.isModelLoaded(desiredRemModel)) {
                        spawnModel = static_cast<uint32_t>(desiredRemModel);
                    } else if (gtaSymbols_.cstreamingRequestModelFn) {
                        gtaSymbols_.cstreamingRequestModelFn(desiredRemModel, 2);
                    }
                }
                void* newPed = gtaSymbols_.cpedOperatorNewFn(0x9c8);
                if (newPed) {
                    gtaSymbols_.ccivilianPedCtorFn(newPed, 4 /*PED_TYPE_CIVMALE*/, spawnModel);
                    remotePedSpawnedModel_[i] = static_cast<int32_t>(spawnModel);
                    uintptr_t newPedAddr = reinterpret_cast<uintptr_t>(newPed);

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

                    gtaSymbols_.cworldAddFn(newPed);

                    int handle = gtaSymbols_.cpoolsGetPedRefFn ? gtaSymbols_.cpoolsGetPedRefFn(newPed) : -1;
                    remotePedPtr_[i] = newPed;
                    remotePedHandle_[i] = handle;
                    remotePedSpawnedModel_[i] = static_cast<int>(spawnModel);

                    if (handle > 0 && gtaSymbols_.cradarSetEntityBlipFn) {
                        // En GTA SA arm64 v2.10: cuando friendly = 0, tanto CRadar::DrawEntityBlip (minimapa)
                        // como CRadar::Draw3dMarkers (cono 3D sobre la cabeza) usan exactamente desiredSlotColor (0..6)
                        uint32_t slotColor = desiredSlotColor;
                        uint8_t isFriendly = 0u;
                        remoteBlipHandle_[i] = gtaSymbols_.cradarSetEntityBlipFn(
                            2 /*BLIP_CHAR*/, handle, slotColor, 3 /*BLIP_DISPLAY_BOTH*/, nullptr
                        );
                        if (remoteBlipHandle_[i] > 0) {
                            if (gtaSymbols_.cradarChangeBlipColourFn) {
                                gtaSymbols_.cradarChangeBlipColourFn(remoteBlipHandle_[i], slotColor);
                            }
                            if (gtaSymbols_.cradarSetBlipFriendlyFn) {
                                gtaSymbols_.cradarSetBlipFriendlyFn(remoteBlipHandle_[i], isFriendly);
                            }
                            if (gtaSymbols_.cradarChangeBlipScaleFn) {
                                gtaSymbols_.cradarChangeBlipScaleFn(remoteBlipHandle_[i], 3);
                            }
                            remoteBlipColor_[i] = static_cast<int>(slotColor);
                        }
                    }

                    ped = newPed;
                    LOGI("Spawned 3D Remote Player P%d (ped=%p, handle=%d, blip=%d, reqSkin=%d, safeModel=%d, spawnedModel=%d, modelLoaded=%d) at (%.2f, %.2f, %.2f)",
                         i + 1, newPed, handle, remoteBlipHandle_[i], rem.skinModelId, desiredRemModel, remotePedSpawnedModel_[i],
                         (desiredRemModel > 0 && gtaSymbols_.isModelLoaded(desiredRemModel)) ? 1 : 0, targetRemX, targetRemY, targetRemZ);
                }
            }

            if (ped) {
                if (remoteBlipHandle_[i] > 0 && remoteBlipColor_[i] != static_cast<int>(desiredSlotColor)) {
                    if (gtaSymbols_.cradarChangeBlipColourFn) {
                        gtaSymbols_.cradarChangeBlipColourFn(remoteBlipHandle_[i], desiredSlotColor);
                    }
                    if (gtaSymbols_.cradarSetBlipFriendlyFn) {
                        gtaSymbols_.cradarSetBlipFriendlyFn(remoteBlipHandle_[i], 0u);
                    }
                    remoteBlipColor_[i] = static_cast<int>(desiredSlotColor);
                }
                uintptr_t remPedAddr = reinterpret_cast<uintptr_t>(ped);
                *reinterpret_cast<float*>(remPedAddr + 0x6ac) = 200.0f;
                // Offsets arm64 v2.10: +0x585 bit 0 = bInVehicle, +0x708 = m_pVehicle
                uint8_t remPedFlags = *reinterpret_cast<uint8_t*>(remPedAddr + 0x585);
                bool remCurrentlyInVeh = ((remPedFlags & 1) != 0);
                void* remCurrentVeh = remCurrentlyInVeh ? *reinterpret_cast<void**>(remPedAddr + 0x708) : nullptr;

                // Sincronizar modelo de skin del compañero remoto (si eligió skin distinta de CJ)
                if (rem.skinModelId > 0 && !remCurrentlyInVeh) {
                    int32_t targetRemModel = resolveSafePedModelId(rem.skinModelId);
                    if (targetRemModel > 0 && remotePedSpawnedModel_[i] != targetRemModel) {
                        if (!gtaSymbols_.isModelLoaded(targetRemModel)) {
                            if ((tickCount % 5 == 0) && gtaSymbols_.cstreamingRequestModelFn) {
                                gtaSymbols_.cstreamingRequestModelFn(targetRemModel, 2);
                            }
                        } else if (gtaSymbols_.cpedSetModelIndexFn) {
                            gtaSymbols_.cpedSetModelIndexFn(ped, static_cast<uint32_t>(targetRemModel));
                            remotePedSpawnedModel_[i] = targetRemModel;
                            LOGI("Updated 3D Remote Player P%d Skin (ped=%p, reqSkin=%d -> safeModel=%d, isModelLoaded=1)",
                                 i + 1, ped, rem.skinModelId, targetRemModel);
                        }
                    }
                }

                if (rem.seatIndex >= 0) {
                    // Buscar el CVehicle compartido en el mundo local donde viaja el compañero remoto
                    void* targetVeh = nullptr;
                    int wantedHostSlot = (rem.vehicleNetId >= 1000) ? (rem.vehicleNetId - 1000) : -1;

                    if (!config_.isHost && wantedHostSlot >= 0) {
                        for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                            if (worldSyncVehPtr_[v] != nullptr && worldSyncVehHostSlot_[v] == wantedHostSlot) {
                                targetVeh = worldSyncVehPtr_[v];
                                break;
                            }
                        }
                    } else if (config_.isHost && wantedHostSlot >= 0 && gtaSymbols_.cpoolsVehiclePool) {
                        void* vPoolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsVehiclePool);
                        if (vPoolPtr) {
                            uintptr_t vPoolAddr = reinterpret_cast<uintptr_t>(vPoolPtr);
                            uint8_t* vObjects = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x00);
                            uint8_t* vByteMap = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x08);
                            int32_t vPoolSize = *reinterpret_cast<int32_t*>(vPoolAddr + 0x10);
                            if (vObjects && vByteMap && wantedHostSlot < vPoolSize && (vByteMap[wantedHostSlot] & 0x80) == 0) {
                                targetVeh = reinterpret_cast<void*>(vObjects + static_cast<size_t>(wantedHostSlot) * 0xc68);
                            }
                        }
                    }

                    if (!targetVeh && myVeh) {
                        uintptr_t myVehMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(myVeh) + 0x18);
                        float vx = myVehMat ? *reinterpret_cast<float*>(myVehMat + 0x30) : me.x;
                        float vy = myVehMat ? *reinterpret_cast<float*>(myVehMat + 0x34) : me.y;
                        float dx = vx - targetRemX;
                        float dy = vy - targetRemY;
                        if ((dx * dx + dy * dy) < (14.0f * 14.0f)) {
                            targetVeh = myVeh;
                        }
                    }

                    if (!targetVeh && !config_.isHost) {
                        float bestDistSq = 18.0f * 18.0f;
                        for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                            void* cand = worldSyncVehPtr_[v];
                            if (!cand) continue;
                            uintptr_t cMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(cand) + 0x18);
                            if (!cMat) continue;
                            float dx = *reinterpret_cast<float*>(cMat + 0x30) - targetRemX;
                            float dy = *reinterpret_cast<float*>(cMat + 0x34) - targetRemY;
                            float dSq = dx * dx + dy * dy;
                            if (dSq < bestDistSq) {
                                bestDistSq = dSq;
                                targetVeh = cand;
                            }
                        }
                    }

                    if (targetVeh) {
                        uintptr_t tvAddr = reinterpret_cast<uintptr_t>(targetVeh);
                        void* currentDrv = *reinterpret_cast<void**>(tvAddr + 0x5a0);
                        if (rem.seatIndex == 0 && currentDrv != localPed) {
                            if (!remCurrentlyInVeh || remCurrentVeh != targetVeh || currentDrv != ped) {
                                if (remCurrentlyInVeh && remCurrentVeh != targetVeh) {
                                    warpPedOutOfVehicle(ped);
                                }
                                warpPedIntoVehicleDriver(ped, targetVeh, localPed, true);
                            }
                            // Si el compañero remoto es el conductor y nosotros no somos el conductor, sincronizar el auto desde rem
                            // (En el Guest, si el auto ya fue actualizado por worldState_.vehicles con matriz completa, no sobreescribirlo)
                            if ((targetVeh != myVeh || me.seatIndex > 0) && (config_.isHost || wantedHostSlot < 0)) {
                                float cH = std::cos(rem.heading);
                                float sH = std::sin(rem.heading);
                                SyncedWorldVehicleEntry remVe{};
                                remVe.packedColors = *reinterpret_cast<uint32_t*>(tvAddr + 0x574);
                                remVe.x = targetRemX;
                                remVe.y = targetRemY;
                                remVe.z = targetRemZ;
                                remVe.vx = rem.vx;
                                remVe.vy = rem.vy;
                                remVe.vz = rem.vz;
                                remVe.rightX = cH;
                                remVe.rightY = sH;
                                remVe.rightZ = 0.0f;
                                remVe.upX = -sH;
                                remVe.upY = cH;
                                remVe.upZ = 0.0f;
                                applySmoothVehicleTransform(targetVeh, remVe);
                            }
                            if (gtaSymbols_.cpedSetPedPositionInCarFn) {
                                gtaSymbols_.cpedSetPedPositionInCarFn(ped);
                            }
                            if (gtaSymbols_.centityUpdateRwFrameFn) {
                                gtaSymbols_.centityUpdateRwFrameFn(ped);
                            }
                        } else {
                            // Compañero remoto como pasajero (o ambos en el mismo auto donde localPed es conductor)
                            int seatSlot = (rem.seatIndex > 0) ? (rem.seatIndex - 1) : 0;
                            if (!remCurrentlyInVeh || remCurrentVeh != targetVeh) {
                                if (remCurrentlyInVeh && remCurrentVeh != targetVeh) {
                                    warpPedOutOfVehicle(ped);
                                }
                                warpPedIntoVehiclePassenger(ped, targetVeh, seatSlot, localPed, true);
                            }
                            if (gtaSymbols_.cpedSetPedPositionInCarFn) {
                                gtaSymbols_.cpedSetPedPositionInCarFn(ped);
                            }
                            if (gtaSymbols_.centityUpdateRwFrameFn) {
                                gtaSymbols_.centityUpdateRwFrameFn(ped);
                            }
                        }
                    } else {
                        applySmoothPedTransformAndAnim(
                            ped, targetRemX, targetRemY, targetRemZ,
                            rem.vx, rem.vy, rem.vz, rem.heading,
                            static_cast<int>(rem.animId), 200.0f, rem.lastNetworkUpdateMs
                        );
                    }
                } else {
                    if (remCurrentlyInVeh) {
                        warpPedOutOfVehicle(ped);
                    }
                    applySmoothPedTransformAndAnim(
                        ped, targetRemX, targetRemY, targetRemZ,
                        rem.vx, rem.vy, rem.vz, rem.heading,
                        static_cast<int>(rem.animId), 200.0f, rem.lastNetworkUpdateMs
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
            tv.tv_usec = 4000; // 4ms poll interval for 60 Hz low-latency sync
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

            uint8_t recvBuf[2048];
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

                    // Ignorar nuestros propios paquetes de broadcast (por IP o por selfInstanceId_ en hdr->version)
                    const auto* inHdr = reinterpret_cast<const BinaryPacketHeader*>(recvBuf);
                    if (inHdr->version == selfInstanceId_) {
                        continue;
                    }
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

                // Enviar paquetes de sincronización 3D a 60 Hz (cada 16 ms) TANTO en HOST como en GUEST,
                // o inmediatamente cuando un invitado envía PKT_HANDSHAKE (Join-in-Progress)
                bool forceImmediateJoinSync = pendingJoinInProgressSync_.exchange(false);
                if (forceImmediateJoinSync || (nowMs - lastUnicastMs >= 16)) {
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
        pkt.wantedLevel = static_cast<uint8_t>(std::clamp(p.wantedLevel, 0, 6));
        uint8_t packedColor = (p.markerColor <= 6) ? static_cast<uint8_t>(p.markerColor + 1) : 0u;
        pkt.independentCamera = ((p.cameraMode == 0) ? 1u : 0u) | static_cast<uint8_t>((packedColor & 0x0Fu) << 2);
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
        pkt.isAttachedOnRoof = (p.isAttachedOnRoof || p.seatIndex >= 4) ? 1 : 0;
        pkt.ownerSlot = p.ownerSlot;
        return pkt;
    }

    CutsceneSyncPacket buildCutsceneSyncPacket(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        CutsceneSyncPacket pkt{};
        fillHeaderLocked(pkt.header, PKT_CUTSCENE_SYNC, static_cast<uint8_t>(slot));
        pkt.cutsceneActive = (gtaSymbols_.cutsceneRunning && *gtaSymbols_.cutsceneRunning != 0) ? 1 : 0;
        pkt.skipVotesMask = cutsceneSkipVotesMask_;
        uint8_t activeMask = 0x01;
        for (int i = 1; i < kMaxCoopPlayers; ++i) {
            if (players_[i].isActive) activeMask |= static_cast<uint8_t>(1U << i);
        }
        pkt.activePlayersMask = activeMask;
        std::snprintf(pkt.missionId, sizeof(pkt.missionId), "%s", mission_.missionId);
        return pkt;
    }

    WorldCheatSyncPacket buildWorldCheatSyncPacket(int slot, const std::string& cheatCode, CheatSyncCategory cat, int16_t vehModel = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        WorldCheatSyncPacket pkt{};
        fillHeaderLocked(pkt.header, PKT_WORLD_CHEAT_SYNC, static_cast<uint8_t>(slot));
        uint32_t h = 2166136261U;
        for (char c : cheatCode) h = (h ^ static_cast<uint8_t>(c)) * 16777619U;
        pkt.cheatHash = h;
        pkt.cheatCategory = static_cast<uint8_t>(cat);
        pkt.ownerSlot = static_cast<uint8_t>(slot);
        pkt.spawnedVehicleModelId = vehModel;
        std::snprintf(pkt.cheatCode, sizeof(pkt.cheatCode), "%s", cheatCode.c_str());
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
                if (config_.isHost) {
                    if (players_[0].hasRealGtaCoords) {
                        players_[idx].x = players_[0].x + 1.5f * static_cast<float>(idx);
                        players_[idx].y = players_[0].y;
                        players_[idx].z = players_[0].z;
                    }
                    pendingJoinInProgressSync_.store(true);
                }
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
                if ((hdr->flags & 0x10) == 0) {
                    p.seatIndex = -1;
                    p.vehicleNetId = 0;
                    p.vehicleModelId = 0;
                }
                if (ped->skinModelId >= 0) p.skinModelId = ped->skinModelId;
                if (!p.hasRealGtaCoords && isDefaultHoldPos) {
                    packetsProcessed_.fetch_add(1);
                    return true;
                }

                p.x = ped->position.x;
                p.y = ped->position.y;
                p.z = ped->position.z;
                p.vx = ped->velocity.x;
                p.vy = ped->velocity.y;
                p.vz = ped->velocity.z;
                p.heading = ped->heading;
                p.lastNetworkUpdateMs = nowMs;
                p.heading = ped->heading;
                p.health = ped->health;
                p.armor = ped->armor;
                p.weaponId = ped->currentWeaponId;
                p.ammo = ped->ammoInClip;
                p.animGroupId = ped->animGroupId;
                p.animId = ped->animId;
                // Guardar las estrellas individuales del compañero sin sobreescribir el CWanted local
                p.wantedLevel = std::clamp(static_cast<int>(ped->wantedLevel), 0, 6);
                p.cameraMode = ((ped->independentCamera & 1u) != 0) ? 0 : 1;
                uint8_t recvColor = static_cast<uint8_t>((ped->independentCamera >> 2) & 0x0Fu);
                if (recvColor >= 1 && recvColor <= 7) {
                    p.markerColor = static_cast<uint8_t>(recvColor - 1);
                }
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
                p.vx = veh->velocity.x;
                p.vy = veh->velocity.y;
                p.vz = veh->velocity.z;
                p.heading = veh->heading;
                p.steerAngle = veh->steerAngle;
                p.gasPedal = veh->gasPedal;
                p.brakePedal = veh->brakePedal;
                p.isDriveByActive = (veh->isDriveByActive != 0);
                p.isAttachedOnRoof = (veh->isAttachedOnRoof != 0 || veh->seatIndex >= 4);
                p.ownerSlot = veh->ownerSlot;

                if (veh->vehicleNetId > 0 && veh->seatIndex == 0) {
                    for (int i = 0; i < kMaxCoopPlayers; ++i) {
                        if (i != idx && players_[i].isActive && players_[i].vehicleNetId == veh->vehicleNetId && players_[i].seatIndex > 0) {
                            players_[i].x = veh->position.x;
                            players_[i].y = veh->position.y;
                            players_[i].z = veh->position.z + (players_[i].isAttachedOnRoof ? 1.35f : 0.0f);
                            players_[i].heading = veh->heading;
                        }
                    }
                }
                packetsProcessed_.fetch_add(1);
                return true;
            }
            case PKT_CUTSCENE_SYNC: {
                if (len < sizeof(CutsceneSyncPacket)) return false;
                const auto* cs = reinterpret_cast<const CutsceneSyncPacket*>(data);
                cutsceneSkipVotesMask_ |= cs->skipVotesMask;
                packetsProcessed_.fetch_add(1);
                return true;
            }
            case PKT_WORLD_CHEAT_SYNC: {
                if (len < sizeof(WorldCheatSyncPacket)) return false;
                const auto* wc = reinterpret_cast<const WorldCheatSyncPacket*>(data);
                if (wc->cheatCategory == CHEAT_WORLD_SHARED) {
                    lastWorldCheatSynced_ = wc->cheatCode;
                    pendingRemoteCheatCmd_ = wc->cheatCode;
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

    // Project Kaizo: Anti-Softlock (restaura cámara 3D, limpia bloqueo y eleva Z + 1.2m sin abortar misión)
    bool triggerAntiSoftlock(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        config_.cameraMode = 0;
        players_[idx].cameraMode = 0;
        players_[idx].z += 1.2f;
        pendingAntiSoftlock_.store(true);
        opcodesExecuted_.fetch_add(1);
        return true;
    }

    // Project Kaizo: Transferir arma y munición al compañero más cercano
    bool transferWeaponToPeer(int fromSlot, int toSlot, int weaponId, int ammo) {
        std::lock_guard<std::mutex> lock(mutex_);
        int srcIdx = std::clamp(fromSlot, 1, kMaxCoopPlayers) - 1;
        int dstIdx = std::clamp(toSlot, 1, kMaxCoopPlayers) - 1;
        players_[dstIdx].weaponId = weaponId > 0 ? weaponId : players_[srcIdx].weaponId;
        players_[dstIdx].ammo = ammo > 0 ? ammo : std::max(players_[srcIdx].ammo / 2, 60);
        opcodesExecuted_.fetch_add(1);
        return true;
    }

    // Project Kaizo: Fijar coordenada de Respawn Táctico grupal conservando vida y chaleco
    void setTacticalRespawnPoint(float x, float y, float z) {
        std::lock_guard<std::mutex> lock(mutex_);
        customRespawnPos_ = CVector{x, y, z};
        hasCustomRespawnPos_ = true;
    }

    // SA:COOP: Clasificador de trucos (LOCAL_ONLY vs WORLD_SHARED vs VEHICLE_SPAWN)
    int classifyAndSyncCheat(const std::string& rawCode, int slot = 1) {
        std::string code = trim(rawCode);
        for (char& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (code == "HESOYAM" || code == "LXGIWYL" || code == "KJKSZPJ" || code == "UZUMYMW" ||
            code == "AEZAKMI" || code == "ASNAEB" || code == "BAGUVIX" || code == "FULLCLIP") {
            return static_cast<int>(CHEAT_LOCAL_ONLY);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        lastWorldCheatSynced_ = code;
        return static_cast<int>(CHEAT_WORLD_SHARED);
    }

    // SA:COOP: Votación de 4 jugadores para saltar cinemática (PKT_CUTSCENE_SYNC)
    uint8_t voteSkipCutscene(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        int idx = std::clamp(slot, 1, kMaxCoopPlayers) - 1;
        cutsceneSkipVotesMask_ |= static_cast<uint8_t>(1U << idx);
        return cutsceneSkipVotesMask_;
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
            "libCoopAndreasBridge.so v2.9 (SA:COOP + Tornamic 4P + Kaizo) | Role=%s (Slot %d) | Room=%s | UDP=%d/%d | "
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
    CoopEngineBridge() {
        selfInstanceId_ = static_cast<uint16_t>(
            ((currentTimeMillis() ^ (reinterpret_cast<uintptr_t>(this) >> 4)) & 0x7FFFu) | 0x8000u
        );
    }

    static uint64_t currentTimeMillis() {
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    void fillHeaderLocked(BinaryPacketHeader& hdr, PacketType type, uint8_t slot) {
        int idx = std::clamp(static_cast<int>(slot), 1, kMaxCoopPlayers) - 1;
        hdr.magic = 0x504F4F43;
        hdr.version = selfInstanceId_;
        hdr.packetType = type;
        hdr.playerSlot = slot;
        hdr.flags = (config_.isHost ? 0x01 : 0x00) |
                    ((config_.cameraMode == 0) ? 0x02 : 0x00) |
                    ((mission_.onMissionFlag != 0) ? 0x04 : 0x00) |
                    (players_[idx].hasRealGtaCoords ? 0x08 : 0x00) |
                    ((players_[idx].seatIndex >= 0) ? 0x10 : 0x00);
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
        if (hdr->version == selfInstanceId_) return;

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

        // Si ambos teléfonos distintos tenían el mismo slot por defecto, resolver comparando IPs;
        // NUNCA invertir slot si la IP origen coincide con la nuestra.
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
            }
        }
    }

    void writeLiveDiagnosticLog(const std::string& myIp) {
        FILE* logf = fopen("/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/coop_bridge_live.log", "w");
        if (!logf) return;
        std::string summary = getBridgeSummary();
        fprintf(logf, "=== COOPANDREAS NATIVE BRIDGE ACTIVE IN GTA SA ===\nLocalIP: %s\n%s\n", myIp.c_str(), summary.c_str());
        fprintf(logf, "Session: Role=%s | Slot=%d | Room=%s | HostIP=%s | ActivePeers=%zu | JoinInProgress=READY | OnMission=%d | Mission=%s | LastCheatApplied=%s\n",
                config_.isHost ? "HOST" : "GUEST", config_.localPlayerSlot, config_.roomCode.c_str(),
                config_.hostIp.c_str(), activePeers_.size(), mission_.onMissionFlag, mission_.missionId,
                lastAppliedCheatCmd_.empty() ? "NONE" : lastAppliedCheatCmd_.c_str());
        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            int32_t reqSkin = players_[i].skinModelId;
            int32_t safeSkin = resolveSafePedModelId(reqSkin);
            int loaded = (safeSkin > 0 && gtaSymbols_.isModelLoaded(safeSkin)) ? 1 : (safeSkin == 0 ? 1 : 0);
            fprintf(logf, "Slot P%d: active=%d | reqSkinModelId=%d | safePedModelId=%d | isModelLoaded=%d | remotePedSpawnedModel=%d | pedPtr=%p | pos=(%.1f, %.1f, %.1f)\n",
                    i + 1, players_[i].isActive ? 1 : 0, reqSkin, safeSkin, loaded,
                    remotePedSpawnedModel_[i], remotePedPtr_[i], players_[i].x, players_[i].y, players_[i].z);
        }
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
        else if (key == "Player1_Skin_ID") {
            int sid = std::clamp(std::atoi(val.c_str()), 0, 299);
            if (config_.localPlayerSlot == 1) {
                config_.localSkinModelId = sid;
                players_[0].skinModelId = sid;
            } else if (players_[0].lastNetworkUpdateMs == 0) {
                players_[0].skinModelId = sid;
            }
        }
        else if (key == "Player2_Skin_ID") {
            int sid = std::clamp(std::atoi(val.c_str()), 0, 299);
            if (config_.localPlayerSlot == 2) {
                config_.localSkinModelId = sid;
                players_[1].skinModelId = sid;
            } else if (players_[1].lastNetworkUpdateMs == 0) {
                players_[1].skinModelId = sid;
            }
        }
        else if (key == "Player3_Skin_ID") {
            int sid = std::clamp(std::atoi(val.c_str()), 0, 299);
            if (config_.localPlayerSlot == 3) {
                config_.localSkinModelId = sid;
                players_[2].skinModelId = sid;
            } else if (players_[2].lastNetworkUpdateMs == 0) {
                players_[2].skinModelId = sid;
            }
        }
        else if (key == "Player4_Skin_ID") {
            int sid = std::clamp(std::atoi(val.c_str()), 0, 299);
            if (config_.localPlayerSlot == 4) {
                config_.localSkinModelId = sid;
                players_[3].skinModelId = sid;
            } else if (players_[3].lastNetworkUpdateMs == 0) {
                players_[3].skinModelId = sid;
            }
        }
        else if (key == "Local_Skin_ID" || key == "skin_id") {
            int sid = std::clamp(std::atoi(val.c_str()), 0, 299);
            config_.localSkinModelId = sid;
            int myIdx = std::clamp(config_.localPlayerSlot, 1, kMaxCoopPlayers) - 1;
            players_[myIdx].skinModelId = sid;
        }
        else if (key == "Player_Marker_Color" || key == "marker_color") {
            config_.localMarkerColor = std::clamp(std::atoi(val.c_str()), 0, 6);
        }
    }

    static int32_t resolveSafePedModelId(int32_t modelId) {
        if (modelId <= 0) return 0;
        switch (modelId) {
            case 1: return 7;     // The Truth -> male01 fallback if special slot unloaded
            case 2: return 7;     // Maccer -> male01 fallback
            case 265: return 280; // Tenpenny -> LAPD
            case 266: return 280; // Pulaski -> LAPD
            case 267: return 280; // Hernandez -> LAPD
            case 269: return 149; // Big Smoke -> Smoke Vest (peds.ide)
            case 270: return 105; // Sweet -> Grove Homie FAM1
            case 271: return 106; // Ryder -> Grove Homie FAM2
            case 289: return 170; // Zero -> SOMYST
            case 290: return 187; // Ken Rosenberg -> SOFYBU
            case 291: return 29;  // Kent Paul -> WMIDRUG
            case 292: return 115; // Cesar -> Azteca VLA2
            case 293: return 107; // OG Loc -> Grove FAM3
            case 294: return 118; // Woozie -> Triad TRIADB
            case 295: return 165; // Mike Toreno -> WMOMIB
            case 297: return 22;  // Madd Dogg -> BMYST
            case 298: return 192; // Catalina -> WFYPRO (peds.ide 192)
            case 299: return 100; // Claude Speed -> BIKERA
            default:
                return std::clamp(modelId, 0, 288);
        }
    }

    static std::string trim(const std::string& s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, (last - first + 1));
    }

    uint8_t findClosestOwnerSlotLocked(float ex, float ey, float ez) const {
        uint8_t bestSlot = 1;
        float bestDistSq = 150.0f * 150.0f;
        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            if (!players_[i].isActive) continue;
            float dx = ex - players_[i].x;
            float dy = ey - players_[i].y;
            float dz = ez - players_[i].z;
            float dSq = dx * dx + dy * dy + dz * dz;
            if (dSq < bestDistSq) {
                bestDistSq = dSq;
                bestSlot = static_cast<uint8_t>(i + 1);
            }
        }
        return bestSlot;
    }

    std::mutex mutex_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> udpThreadRunning_{false};
    std::atomic<bool> gotHookInstalled_{false};
    std::atomic<int64_t> mainThreadTicks_{0};
    std::atomic<int64_t> packetsProcessed_{0};
    std::atomic<int64_t> opcodesExecuted_{0};
    std::atomic<bool> pendingAntiSoftlock_{false};
    std::atomic<bool> pendingJoinInProgressSync_{false};
    int32_t localAppliedSkinModel_ = 0;
    std::thread udpWorkerThread_;
    GtaSaNativeSymbols gtaSymbols_;
    BridgeConfig config_;
    PlayerState3D players_[kMaxCoopPlayers];
    MissionSyncState mission_;
    WorldSyncState worldState_;
    uint8_t cutsceneSkipVotesMask_ = 0;
    uint16_t selfInstanceId_ = 2;
    std::string lastWorldCheatSynced_;
    std::string lastAppliedCheatCmd_;
    std::string pendingRemoteCheatCmd_;
    int pendingVehicleSpawnModel_ = 0;
    bool hasCustomRespawnPos_ = false;
    CVector customRespawnPos_{2495.0f, -1687.0f, 13.5f};

    void* lastLocalPedPtr_ = nullptr;
    bool initialSyncTeleportDone_ = false;
    bool initialAlleyFreeRoamDone_ = false;
    int64_t alleyLandWaitTicks_ = 0;
    void* remotePedPtr_[kMaxCoopPlayers] = {nullptr};
    int remotePedHandle_[kMaxCoopPlayers] = {-1, -1, -1, -1};
    int remotePedSpawnedModel_[kMaxCoopPlayers] = {0};
    int remoteBlipHandle_[kMaxCoopPlayers] = {-1, -1, -1, -1};
    int remoteBlipColor_[kMaxCoopPlayers] = {-1, -1, -1, -1};

    void* worldSyncPedPtr_[kMaxSyncedWorldPeds] = {nullptr};
    int worldSyncPedHandle_[kMaxSyncedWorldPeds] = {-1, -1, -1, -1, -1, -1};
    int16_t worldSyncPedHostSlot_[kMaxSyncedWorldPeds] = {-1, -1, -1, -1, -1, -1};
    int16_t worldSyncPedModel_[kMaxSyncedWorldPeds] = {-1, -1, -1, -1, -1, -1};

    void* worldSyncVehPtr_[kMaxSyncedWorldVehicles] = {nullptr};
    int worldSyncVehHandle_[kMaxSyncedWorldVehicles] = {-1, -1, -1, -1, -1};
    int16_t worldSyncVehHostSlot_[kMaxSyncedWorldVehicles] = {-1, -1, -1, -1, -1};
    int16_t worldSyncVehModel_[kMaxSyncedWorldVehicles] = {-1, -1, -1, -1, -1};
    void* worldSyncVehDriverPtr_[kMaxSyncedWorldVehicles] = {nullptr};
    int worldSyncVehDriverHandle_[kMaxSyncedWorldVehicles] = {-1, -1, -1, -1, -1};
    bool worldSyncVehHadDriver_[kMaxSyncedWorldVehicles] = {false};

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
                "2.9.0",
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeTriggerAntiSoftlock(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jint slot
) {
    return coopandreas::CoopEngineBridge::instance().triggerAntiSoftlock(static_cast<int>(slot)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeShareWeaponWithPartner(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jint fromSlot,
    jint toSlot,
    jint weaponId,
    jint ammo
) {
    return coopandreas::CoopEngineBridge::instance().transferWeaponToPeer(
        static_cast<int>(fromSlot), static_cast<int>(toSlot), static_cast<int>(weaponId), static_cast<int>(ammo)
    ) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeClassifyAndSyncCheat(
    JNIEnv* env,
    jobject /* thiz */,
    jstring cheatCode,
    jint slot
) {
    return static_cast<jint>(coopandreas::CoopEngineBridge::instance().classifyAndSyncCheat(
        jstringToStdString(env, cheatCode), static_cast<int>(slot)
    ));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeVoteSkipCutscene(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jint slot
) {
    return static_cast<jint>(coopandreas::CoopEngineBridge::instance().voteSkipCutscene(static_cast<int>(slot)));
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_bridge_CoopNativeBridge_nativeSetTacticalRespawnPoint(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jfloat x,
    jfloat y,
    jfloat z
) {
    coopandreas::CoopEngineBridge::instance().setTacticalRespawnPoint(x, y, z);
}

#endif
