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

struct CVector2D {
    float x = 0.0f;
    float y = 0.0f;
};

struct GtaCRGBA {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

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
    uint32_t damageWheels = 0;    // CDamageManager +0x75c: m_anWheelsStatus[4]
    uint32_t damageDoorsLow = 0;  // CDamageManager +0x760: m_anDoorsStatus[0..3]
    uint16_t damageDoorsHigh = 0; // CDamageManager +0x764: m_anDoorsStatus[4..5]
    uint32_t damageLights = 0;    // CDamageManager +0x768: m_nLightsStatus
    uint32_t damagePanels = 0;    // CDamageManager +0x76c: m_nPanelsStatus
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
    float vehicleHealth = 1000.0f;
    uint32_t packedColors = 0;    // +0x574..+0x577: m_nPrimaryColor, m_nSecondaryColor, m_nTertiaryColor, m_nQuaternaryColor
    uint32_t damageWheels = 0;    // CDamageManager +0x75c: m_anWheelsStatus[4]
    uint32_t damageDoorsLow = 0;  // CDamageManager +0x760: m_anDoorsStatus[0..3]
    uint16_t damageDoorsHigh = 0; // CDamageManager +0x764: m_anDoorsStatus[4..5]
    uint32_t damageLights = 0;    // CDamageManager +0x768: m_nLightsStatus
    uint32_t damagePanels = 0;    // CDamageManager +0x76c: m_nPanelsStatus
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
    using FnCRadarChangeBlipDisplay = void (*)(int blipHandle, int blipDisplay);
    using FnCRadarClearBlip = void (*)(int blipHandle);
    using FnCRadarDrawBlips = void (*)(float circleSize);
    using FnCRadarTransformRealWorldPointToRadarSpace = void (*)(CVector2D* radarPt, const CVector2D* worldPt);
    using FnCRadarLimitRadarPoint = float (*)(CVector2D* radarPt);
    using FnCRadarTransformRadarPointToScreenSpace = void (*)(CVector2D* screenPt, const CVector2D* radarPt);
    using FnCRadarDrawRotatingRadarSprite = void (*)(void* sprite2d, float x, float y, float angle, float scale, const GtaCRGBA* color);
    using FnCRadarGetRadarTraceColour = uint32_t (*)(uint32_t colourId, uint8_t bright, uint8_t friendly);
    FnCRadarChangeBlipColour cradarChangeBlipColourFn = nullptr;
    FnCRadarSetBlipFriendly cradarSetBlipFriendlyFn = nullptr;
    FnCRadarChangeBlipScale cradarChangeBlipScaleFn = nullptr;
    FnCRadarChangeBlipDisplay cradarChangeBlipDisplayFn = nullptr;
    FnCRadarClearBlip cradarClearBlipFn = nullptr;
    FnCRadarDrawBlips cradarDrawBlipsFn = nullptr;
    FnCRadarTransformRealWorldPointToRadarSpace cradarTransformRealWorldPointToRadarSpaceFn = nullptr;
    FnCRadarLimitRadarPoint cradarLimitRadarPointFn = nullptr;
    FnCRadarTransformRadarPointToScreenSpace cradarTransformRadarPointToScreenSpaceFn = nullptr;
    FnCRadarDrawRotatingRadarSprite cradarDrawRotatingRadarSpriteFn = nullptr;
    FnCRadarGetRadarTraceColour cradarGetRadarTraceColourFn = nullptr;
    void* cradarBlipSprites = nullptr;
    void* frontEndMenuManager = nullptr;
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
    FnCCheatVoid ccheatMoneyArmourHealthCheatFn = nullptr;
    FnCCheatVoid ccheatWeaponCheat1Fn = nullptr;
    FnCCheatVoid ccheatWeaponCheat2Fn = nullptr;
    FnCCheatVoid ccheatWeaponCheat3Fn = nullptr;
    FnCCheatVoid ccheatJetpackCheatFn = nullptr;
    FnCCheatVoid ccheatNotWantedCheatFn = nullptr;
    FnCCheatVoid ccheatWantedCheatFn = nullptr;
    FnCCheatVoid ccheatSunnyCheatFn = nullptr;
    FnCCheatVoid ccheatRainCheatFn = nullptr;
    FnCCheatVoid ccheatRiotCheatFn = nullptr;
    FnCCheatVoid ccheatTankCheatFn = nullptr;
    FnCCheatVoid ccheatFlyboyCheatFn = nullptr;
    FnCCheatVoid ccheatApacheCheatFn = nullptr;
    FnCCheatVoid ccheatStockCarCheatFn = nullptr;
    FnCCheatVoid ccheatQuadCheatFn = nullptr;
    FnCCheatVehicle ccheatVehicleCheatFn = nullptr;
    void** cmodelInfoPtrs = nullptr;

    using FnCStreamingRequestSpecialChar = void (*)(int slot, const char* name, int flags);
    using FnCStreamingHasSpecialCharLoaded = bool (*)(int slot);
    using FnCWeaponInfoGetWeaponInfo = void* (*)(int weaponType, int8_t skill);
    using FnCPedIntelGetTask = void* (*)(const void* thisIntel);
    using FnCPedIntelGetTaskDuck = void* (*)(const void* thisIntel, bool bIgnoreControlType);
    using FnCAnimManagerBlendAnimation = void* (*)(void* clump, int assocGroupId, int animId, float clumpAssocBlendData);
    using FnCClothesRebuildPlayer = void (*)(void* playerPed, bool bIgnoreFatAndMuscle);
    using FnCClothesRebuildPlayerIfNeeded = void (*)(void* playerPed);
    using FnCClothesRebuildCutscenePlayer = void (*)(void* playerPed, int modelId);
    using FnCTaskSimplePlayerOnFootCtor = void* (*)(void* self);
    using FnCTaskComplexEnterCarAsPassengerCtor = void* (*)(void* self, void* targetVeh, int targetDoor, bool carryOnAfterFallingOff);
    using FnCTaskSimpleDuckToggleCtor = void* (*)(void* self, int action);
    using FnCTaskSimpleDuckControlDuckMove = void (*)(void* self, CVector2D moveVec);
    using FnCTaskManagerSetTaskSecondary = void (*)(void* self, void* pTask, int nTaskIndex);
    using FnCAutomobileSetupDamageAfterLoad = void (*)(void* selfAuto);
    using FnCRunningScriptLocateCharCommand = void (*)(void* selfScript, int commandId);
    using FnCPlayerPedCanPlayerStartMission = bool (*)(void* selfPlayerPed);
    using FnCTaskSimpleGangDriveByCtor = void* (*)(void* self, void* pTargetEntity, const CVector* pTargetPos, float fAbortRange, int8_t nFrequencyPercentage, int8_t nDrivebyStyle, bool bSeatRHS);
    using FnCCutsceneMgrIsSkipPressed = bool (*)();
    using FnCTouchInterfaceIsTouched = bool (*)(int widgetId, CVector2D* pVecOut, int nEnableWidget);
    using FnCTouchInterfaceIsHeldDown = bool (*)(int widgetId, int nEnableWidget);
    using FnCPedAddWeaponModel = void (*)(void* selfPed, int modelIndex);
    using FnCPedSetCurrentWeaponSlot = void (*)(void* selfPed, int slotIndex);
    using FnCPadGetWeapon = uint8_t (*)(void* selfPad, void* ped, bool bCheckWeapon);
    using FnCCameraSetNewPlayerWeaponMode = void (*)(void* selfCamera, int16_t mode, int16_t maxZoom, int16_t minZoom);
    using FnCTaskSimpleGangDriveByPed = void (*)(void* selfTask, void* ped);

    FnCStreamingRequestSpecialChar cstreamingRequestSpecialCharFn = nullptr;
    FnCStreamingHasSpecialCharLoaded cstreamingHasSpecialCharLoadedFn = nullptr;
    FnCWeaponInfoGetWeaponInfo cweaponInfoGetWeaponInfoFn = nullptr;
    FnCPedIntelGetTask cpedIntelGetTaskFightingFn = nullptr;
    FnCPedIntelGetTask cpedIntelGetTaskUseGunFn = nullptr;
    FnCPedIntelGetTaskDuck cpedIntelGetTaskDuckFn = nullptr;
    FnCAnimManagerBlendAnimation canimManagerBlendAnimationFn = nullptr;
    FnCClothesRebuildPlayer cclothesRebuildPlayerFn = nullptr;
    FnCClothesRebuildPlayerIfNeeded cclothesRebuildPlayerIfNeededFn = nullptr;
    FnCClothesRebuildCutscenePlayer cclothesRebuildCutscenePlayerFn = nullptr;
    FnCTaskSimplePlayerOnFootCtor ctaskSimplePlayerOnFootCtorFn = nullptr;
    FnCTaskComplexEnterCarAsPassengerCtor ctaskComplexEnterCarAsPassengerCtorFn = nullptr;
    FnCTaskSimpleDuckToggleCtor ctaskSimpleDuckToggleCtorFn = nullptr;
    FnCTaskProcessPed ctaskSimpleDuckToggleProcessPedFn = nullptr;
    FnCTaskSimpleDuckControlDuckMove ctaskSimpleDuckControlDuckMoveFn = nullptr;
    FnCTaskManagerSetTaskSecondary ctaskManagerSetTaskSecondaryFn = nullptr;
    FnCAutomobileSetupDamageAfterLoad cautomobileSetupDamageAfterLoadFn = nullptr;
    FnCRunningScriptLocateCharCommand crunningScriptLocateCharCommandFn = nullptr;
    FnCPlayerPedCanPlayerStartMission cplayerPedCanPlayerStartMissionFn = nullptr;
    FnCTaskSimpleGangDriveByCtor ctaskSimpleGangDriveByCtorFn = nullptr;
    FnCCutsceneMgrIsSkipPressed ccutsceneMgrIsSkipPressedFn = nullptr;
    FnCTouchInterfaceIsTouched ctouchInterfaceIsTouchedFn = nullptr;
    FnCTouchInterfaceIsHeldDown ctouchInterfaceIsHeldDownFn = nullptr;
    FnCPedAddWeaponModel cpedAddWeaponModelFn = nullptr;
    FnCPedSetCurrentWeaponSlot cpedSetCurrentWeaponSlotFn = nullptr;
    FnCPadGetWeapon cpadGetWeaponFn = nullptr;
    FnCCameraSetNewPlayerWeaponMode ccameraSetNewPlayerWeaponModeFn = nullptr;
    FnCTaskSimpleGangDriveByPed ctaskSimpleGangDriveByFireGunFn = nullptr;
    FnCTaskSimpleGangDriveByPed ctaskSimpleGangDriveByAimGunFn = nullptr;
    void* theCamera = nullptr;                      // TheCamera
    int32_t* scriptParams = nullptr;                // ScriptParams
    void* canimManagerAssocGroups = nullptr;        // _ZN12CAnimManager19ms_aAnimAssocGroupsE
    void* canimManagerAnimBlocks = nullptr;         // _ZN12CAnimManager14ms_aAnimBlocksE

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
        cradarChangeBlipDisplayFn = reinterpret_cast<FnCRadarChangeBlipDisplay>(dlsym(libHandle, "_ZN6CRadar17ChangeBlipDisplayEi12eBlipDisplay"));
        cradarClearBlipFn = reinterpret_cast<FnCRadarClearBlip>(dlsym(libHandle, "_ZN6CRadar9ClearBlipEi"));
        cradarDrawBlipsFn = reinterpret_cast<FnCRadarDrawBlips>(dlsym(libHandle, "_ZN6CRadar9DrawBlipsEf"));
        cradarTransformRealWorldPointToRadarSpaceFn = reinterpret_cast<FnCRadarTransformRealWorldPointToRadarSpace>(dlsym(libHandle, "_ZN6CRadar35TransformRealWorldPointToRadarSpaceER9CVector2DRKS0_"));
        cradarLimitRadarPointFn = reinterpret_cast<FnCRadarLimitRadarPoint>(dlsym(libHandle, "_ZN6CRadar15LimitRadarPointER9CVector2D"));
        cradarTransformRadarPointToScreenSpaceFn = reinterpret_cast<FnCRadarTransformRadarPointToScreenSpace>(dlsym(libHandle, "_ZN6CRadar32TransformRadarPointToScreenSpaceER9CVector2DRKS0_"));
        cradarDrawRotatingRadarSpriteFn = reinterpret_cast<FnCRadarDrawRotatingRadarSprite>(dlsym(libHandle, "_ZN6CRadar23DrawRotatingRadarSpriteEP9CSprite2dffff5CRGBA"));
        cradarGetRadarTraceColourFn = reinterpret_cast<FnCRadarGetRadarTraceColour>(dlsym(libHandle, "_ZN6CRadar19GetRadarTraceColourEjhh"));
        cradarBlipSprites = dlsym(libHandle, "_ZN6CRadar16RadarBlipSpritesE");
        frontEndMenuManager = dlsym(libHandle, "FrontEndMenuManager");
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
        cmodelInfoPtrs = reinterpret_cast<void**>(dlsym(libHandle, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
        cstreamingRequestSpecialCharFn = reinterpret_cast<FnCStreamingRequestSpecialChar>(dlsym(libHandle, "_ZN10CStreaming18RequestSpecialCharEiPKci"));
        cstreamingHasSpecialCharLoadedFn = reinterpret_cast<FnCStreamingHasSpecialCharLoaded>(dlsym(libHandle, "_ZN10CStreaming20HasSpecialCharLoadedEi"));
        cweaponInfoGetWeaponInfoFn = reinterpret_cast<FnCWeaponInfoGetWeaponInfo>(dlsym(libHandle, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponTypea"));
        cpedIntelGetTaskFightingFn = reinterpret_cast<FnCPedIntelGetTask>(dlsym(libHandle, "_ZNK16CPedIntelligence15GetTaskFightingEv"));
        cpedIntelGetTaskUseGunFn = reinterpret_cast<FnCPedIntelGetTask>(dlsym(libHandle, "_ZNK16CPedIntelligence13GetTaskUseGunEv"));
        cpedIntelGetTaskDuckFn = reinterpret_cast<FnCPedIntelGetTaskDuck>(dlsym(libHandle, "_ZNK16CPedIntelligence11GetTaskDuckEb"));
        canimManagerBlendAnimationFn = reinterpret_cast<FnCAnimManagerBlendAnimation>(dlsym(libHandle, "_ZN12CAnimManager14BlendAnimationEP7RpClump12AssocGroupId11AnimationIdf"));
        cclothesRebuildPlayerFn = reinterpret_cast<FnCClothesRebuildPlayer>(dlsym(libHandle, "_ZN8CClothes13RebuildPlayerEP10CPlayerPedb"));
        cclothesRebuildPlayerIfNeededFn = reinterpret_cast<FnCClothesRebuildPlayerIfNeeded>(dlsym(libHandle, "_ZN8CClothes21RebuildPlayerIfNeededEP10CPlayerPed"));
        cclothesRebuildCutscenePlayerFn = reinterpret_cast<FnCClothesRebuildCutscenePlayer>(dlsym(libHandle, "_ZN8CClothes21RebuildCutscenePlayerEP10CPlayerPedi"));
        ctaskSimplePlayerOnFootCtorFn = reinterpret_cast<FnCTaskSimplePlayerOnFootCtor>(dlsym(libHandle, "_ZN23CTaskSimplePlayerOnFootC1Ev"));
        ctaskComplexEnterCarAsPassengerCtorFn = reinterpret_cast<FnCTaskComplexEnterCarAsPassengerCtor>(dlsym(libHandle, "_ZN31CTaskComplexEnterCarAsPassengerC1EP8CVehicleib"));
        ctaskSimpleDuckToggleCtorFn = reinterpret_cast<FnCTaskSimpleDuckToggleCtor>(dlsym(libHandle, "_ZN21CTaskSimpleDuckToggleC1Ei"));
        ctaskSimpleDuckToggleProcessPedFn = reinterpret_cast<FnCTaskProcessPed>(dlsym(libHandle, "_ZN21CTaskSimpleDuckToggle10ProcessPedEP4CPed"));
        ctaskSimpleDuckControlDuckMoveFn = reinterpret_cast<FnCTaskSimpleDuckControlDuckMove>(dlsym(libHandle, "_ZN15CTaskSimpleDuck15ControlDuckMoveE9CVector2D"));
        ctaskManagerSetTaskSecondaryFn = reinterpret_cast<FnCTaskManagerSetTaskSecondary>(dlsym(libHandle, "_ZN12CTaskManager16SetTaskSecondaryEP5CTaski"));
        cautomobileSetupDamageAfterLoadFn = reinterpret_cast<FnCAutomobileSetupDamageAfterLoad>(dlsym(libHandle, "_ZN11CAutomobile20SetupDamageAfterLoadEv"));
        crunningScriptLocateCharCommandFn = reinterpret_cast<FnCRunningScriptLocateCharCommand>(dlsym(libHandle, "_ZN14CRunningScript17LocateCharCommandEi"));
        cplayerPedCanPlayerStartMissionFn = reinterpret_cast<FnCPlayerPedCanPlayerStartMission>(dlsym(libHandle, "_ZN10CPlayerPed21CanPlayerStartMissionEv"));
        ctaskSimpleGangDriveByCtorFn = reinterpret_cast<FnCTaskSimpleGangDriveByCtor>(dlsym(libHandle, "_ZN22CTaskSimpleGangDriveByC1EP7CEntityPK7CVectorfaab"));
        ccutsceneMgrIsSkipPressedFn = reinterpret_cast<FnCCutsceneMgrIsSkipPressed>(dlsym(libHandle, "_ZN12CCutsceneMgr32IsCutsceneSkipButtonBeingPressedEv"));
        ctouchInterfaceIsTouchedFn = reinterpret_cast<FnCTouchInterfaceIsTouched>(dlsym(libHandle, "_ZN15CTouchInterface9IsTouchedENS_9WidgetIDsEP9CVector2Di"));
        ctouchInterfaceIsHeldDownFn = reinterpret_cast<FnCTouchInterfaceIsHeldDown>(dlsym(libHandle, "_ZN15CTouchInterface10IsHeldDownENS_9WidgetIDsEi"));
        cpedAddWeaponModelFn = reinterpret_cast<FnCPedAddWeaponModel>(dlsym(libHandle, "_ZN4CPed14AddWeaponModelEi"));
        cpedSetCurrentWeaponSlotFn = reinterpret_cast<FnCPedSetCurrentWeaponSlot>(dlsym(libHandle, "_ZN4CPed16SetCurrentWeaponEi"));
        cpadGetWeaponFn = reinterpret_cast<FnCPadGetWeapon>(dlsym(libHandle, "_ZN4CPad9GetWeaponEP4CPedb"));
        ccameraSetNewPlayerWeaponModeFn = reinterpret_cast<FnCCameraSetNewPlayerWeaponMode>(dlsym(libHandle, "_ZN7CCamera22SetNewPlayerWeaponModeEsss"));
        ctaskSimpleGangDriveByFireGunFn = reinterpret_cast<FnCTaskSimpleGangDriveByPed>(dlsym(libHandle, "_ZN22CTaskSimpleGangDriveBy7FireGunEP4CPed"));
        ctaskSimpleGangDriveByAimGunFn = reinterpret_cast<FnCTaskSimpleGangDriveByPed>(dlsym(libHandle, "_ZN22CTaskSimpleGangDriveBy6AimGunEP4CPed"));
        theCamera = dlsym(libHandle, "TheCamera");
        scriptParams = reinterpret_cast<int32_t*>(dlsym(libHandle, "ScriptParams"));
        canimManagerAssocGroups = dlsym(libHandle, "_ZN12CAnimManager19ms_aAnimAssocGroupsE");
        canimManagerAnimBlocks = dlsym(libHandle, "_ZN12CAnimManager14ms_aAnimBlocksE");
        ccheatHealthCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat11HealthCheatEv"));
        ccheatMoneyArmourHealthCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat22MoneyArmourHealthCheatEv"));
        ccheatWeaponCheat1Fn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat12WeaponCheat1Ev"));
        ccheatWeaponCheat2Fn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat12WeaponCheat2Ev"));
        ccheatWeaponCheat3Fn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat12WeaponCheat3Ev"));
        ccheatJetpackCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat12JetpackCheatEv"));
        ccheatNotWantedCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat20WantedLevelDownCheatEv"));
        if (!ccheatNotWantedCheatFn) ccheatNotWantedCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat14NotWantedCheatEv"));
        ccheatWantedCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat18WantedLevelUpCheatEv"));
        if (!ccheatWantedCheatFn) ccheatWantedCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat11WantedCheatEv"));
        ccheatSunnyCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat17SunnyWeatherCheatEv"));
        if (!ccheatSunnyCheatFn) ccheatSunnyCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat10SunnyCheatEv"));
        ccheatRainCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat10StormCheatEv"));
        if (!ccheatRainCheatFn) ccheatRainCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat17RainyWeatherCheatEv"));
        ccheatRiotCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat9RiotCheatEv"));
        if (!ccheatRiotCheatFn) ccheatRiotCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat11MayhemCheatEv"));
        ccheatVehicleCheatFn = reinterpret_cast<FnCCheatVehicle>(dlsym(libHandle, "_ZN6CCheat12VehicleCheatEi"));
        ccheatTankCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat9TankCheatEv"));
        ccheatFlyboyCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat11FlyboyCheatEv"));
        ccheatApacheCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat11ApacheCheatEv"));
        ccheatStockCarCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat13StockCarCheatEv"));
        ccheatQuadCheatFn = reinterpret_cast<FnCCheatVoid>(dlsym(libHandle, "_ZN6CCheat9QuadCheatEv"));
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
        if (cmodelInfoPtrs && cmodelInfoPtrs[modelId] == nullptr) return false;
        // Each CStreamingInfo is 20 bytes; m_nLoadState (uint8_t) is at offset 0x10 (1 == LOADSTATE_LOADED)
        return cstreamingInfoForModel[modelId * 20 + 16] == 1;
    }

    bool isValidModelInfo(int modelId) const {
        if (modelId < 0 || modelId > 20000) return false;
        if (cmodelInfoPtrs && cmodelInfoPtrs[modelId] == nullptr) return false;
        return true;
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
    uint8_t isCrouching = 0;
    uint8_t isJumping = 0;
    uint8_t isAiming = 0;

    int32_t vehicleNetId = 0;
    int32_t vehicleModelId = 492;
    int32_t seatIndex = -1; // -1 = a pie, 0 = conductor, 1..3 = pasajeros, 4..6 = Vehicle Surf
    float steerAngle = 0.0f;
    float gasPedal = 0.0f;
    float brakePedal = 0.0f;
    float vehicleHealth = 1000.0f;
    uint32_t damageWheels = 0;
    uint32_t damageDoorsLow = 0;
    uint16_t damageDoorsHigh = 0;
    uint32_t damageLights = 0;
    uint32_t damagePanels = 0;
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
static GtaSaNativeSymbols::FnCClothesRebuildPlayer g_origCClothesRebuildPlayer = nullptr;
static GtaSaNativeSymbols::FnCClothesRebuildPlayerIfNeeded g_origCClothesRebuildPlayerIfNeeded = nullptr;
static GtaSaNativeSymbols::FnCClothesRebuildCutscenePlayer g_origCClothesRebuildCutscenePlayer = nullptr;
static GtaSaNativeSymbols::FnCRadarDrawBlips g_origCRadarDrawBlips = nullptr;
static GtaSaNativeSymbols::FnCRunningScriptLocateCharCommand g_origCRunningScriptLocateCharCommand = nullptr;
static GtaSaNativeSymbols::FnCPlayerPedCanPlayerStartMission g_origCPlayerPedCanPlayerStartMission = nullptr;
static GtaSaNativeSymbols::FnCCutsceneMgrSkipCutscene g_origCCutsceneMgrSkipCutscene = nullptr;
static GtaSaNativeSymbols::FnCCutsceneMgrIsSkipPressed g_origCCutsceneMgrIsSkipPressed = nullptr;
static GtaSaNativeSymbols::FnCPadGetWeapon g_origCPadGetWeapon = nullptr;
static void Hooked_CWorld_Process();
static void Hooked_CClothes_RebuildPlayer(void* playerPed, bool bIgnoreFatAndMuscle);
static void Hooked_CClothes_RebuildPlayerIfNeeded(void* playerPed);
static void Hooked_CClothes_RebuildCutscenePlayer(void* playerPed, int modelId);
static void Hooked_CRadar_DrawBlips(float circleSize);
static void Hooked_CRunningScript_LocateCharCommand(void* selfScript, int commandId);
static bool Hooked_CPlayerPed_CanPlayerStartMission(void* selfPlayerPed);
static void Hooked_CCutsceneMgr_SkipCutscene();
static bool Hooked_CCutsceneMgr_IsSkipButtonPressed();
static uint8_t Hooked_CPad_GetWeapon(void* selfPad, void* ped, bool bCheckWeapon);

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
        g_origCClothesRebuildPlayer = gtaSymbols_.cclothesRebuildPlayerFn;
        g_origCClothesRebuildPlayerIfNeeded = gtaSymbols_.cclothesRebuildPlayerIfNeededFn;
        g_origCClothesRebuildCutscenePlayer = gtaSymbols_.cclothesRebuildCutscenePlayerFn;
        g_origCRadarDrawBlips = gtaSymbols_.cradarDrawBlipsFn;
        g_origCRunningScriptLocateCharCommand = gtaSymbols_.crunningScriptLocateCharCommandFn;
        g_origCPlayerPedCanPlayerStartMission = gtaSymbols_.cplayerPedCanPlayerStartMissionFn;
        g_origCCutsceneMgrSkipCutscene = gtaSymbols_.ccutsceneMgrSkipCutsceneFn;
        g_origCCutsceneMgrIsSkipPressed = gtaSymbols_.ccutsceneMgrIsSkipPressedFn;
        g_origCPadGetWeapon = gtaSymbols_.cpadGetWeaponFn;

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

        auto hookGotOffset = [this](uintptr_t offset, void* origFn, void* hookFn) -> bool {
            if (gtaSymbols_.libBase == 0 || !origFn || !hookFn) return false;
            uintptr_t candidateGot = gtaSymbols_.libBase + offset;
            uintptr_t pageStart = candidateGot & ~static_cast<uintptr_t>(0xFFF);
            if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                void** slot = reinterpret_cast<void**>(candidateGot);
                if (*slot == origFn) {
                    *slot = hookFn;
                    return true;
                }
            }
            return false;
        };

        hookGotOffset(0x847de0, reinterpret_cast<void*>(gtaSymbols_.cclothesRebuildPlayerFn), reinterpret_cast<void*>(&Hooked_CClothes_RebuildPlayer));
        hookGotOffset(0x847398, reinterpret_cast<void*>(gtaSymbols_.cclothesRebuildPlayerIfNeededFn), reinterpret_cast<void*>(&Hooked_CClothes_RebuildPlayerIfNeeded));
        hookGotOffset(0x841800, reinterpret_cast<void*>(gtaSymbols_.cclothesRebuildCutscenePlayerFn), reinterpret_cast<void*>(&Hooked_CClothes_RebuildCutscenePlayer));
        hookGotOffset(0x83ded0, reinterpret_cast<void*>(gtaSymbols_.cradarDrawBlipsFn), reinterpret_cast<void*>(&Hooked_CRadar_DrawBlips));
        hookGotOffset(0x849880, reinterpret_cast<void*>(gtaSymbols_.crunningScriptLocateCharCommandFn), reinterpret_cast<void*>(&Hooked_CRunningScript_LocateCharCommand));
        hookGotOffset(0x848630, reinterpret_cast<void*>(gtaSymbols_.cplayerPedCanPlayerStartMissionFn), reinterpret_cast<void*>(&Hooked_CPlayerPed_CanPlayerStartMission));
        hookGotOffset(0x846890, reinterpret_cast<void*>(gtaSymbols_.ccutsceneMgrSkipCutsceneFn), reinterpret_cast<void*>(&Hooked_CCutsceneMgr_SkipCutscene));
        hookGotOffset(0x8448c0, reinterpret_cast<void*>(gtaSymbols_.ccutsceneMgrIsSkipPressedFn), reinterpret_cast<void*>(&Hooked_CCutsceneMgr_IsSkipButtonPressed));
        hookGotOffset(0x844f50, reinterpret_cast<void*>(gtaSymbols_.cpadGetWeaponFn), reinterpret_cast<void*>(&Hooked_CPad_GetWeapon));

        // 1. Check exact GOT offset 0x843fb0 in libGTASA.so v2.10 arm64-v8a first
        if (hookGotOffset(0x843fb0, targetFn, reinterpret_cast<void*>(&Hooked_CWorld_Process))) {
            gotHookInstalled_.store(true);
            LOGI("Installed GOT Hooks on _ZN6CWorld7ProcessEv, CClothes::RebuildPlayer, CRadar::DrawBlips, and CRunningScript::LocateCharCommand at base=0x%lx",
                 static_cast<unsigned long>(gtaSymbols_.libBase));
            return true;
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
                if (gtaSymbols_.cclothesRebuildPlayerFn && *slot == reinterpret_cast<void*>(gtaSymbols_.cclothesRebuildPlayerFn)) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CClothes_RebuildPlayer);
                    }
                } else if (gtaSymbols_.cclothesRebuildPlayerIfNeededFn && *slot == reinterpret_cast<void*>(gtaSymbols_.cclothesRebuildPlayerIfNeededFn)) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CClothes_RebuildPlayerIfNeeded);
                    }
                } else if (gtaSymbols_.cclothesRebuildCutscenePlayerFn && *slot == reinterpret_cast<void*>(gtaSymbols_.cclothesRebuildCutscenePlayerFn)) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CClothes_RebuildCutscenePlayer);
                    }
                } else if (gtaSymbols_.cradarDrawBlipsFn && *slot == reinterpret_cast<void*>(gtaSymbols_.cradarDrawBlipsFn)) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CRadar_DrawBlips);
                    }
                } else if (gtaSymbols_.crunningScriptLocateCharCommandFn && *slot == reinterpret_cast<void*>(gtaSymbols_.crunningScriptLocateCharCommandFn)) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CRunningScript_LocateCharCommand);
                    }
                } else if (gtaSymbols_.cplayerPedCanPlayerStartMissionFn && *slot == reinterpret_cast<void*>(gtaSymbols_.cplayerPedCanPlayerStartMissionFn)) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CPlayerPed_CanPlayerStartMission);
                    }
                } else if (gtaSymbols_.ccutsceneMgrSkipCutsceneFn && *slot == reinterpret_cast<void*>(gtaSymbols_.ccutsceneMgrSkipCutsceneFn)) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CCutsceneMgr_SkipCutscene);
                    }
                } else if (gtaSymbols_.ccutsceneMgrIsSkipPressedFn && *slot == reinterpret_cast<void*>(gtaSymbols_.ccutsceneMgrIsSkipPressedFn)) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CCutsceneMgr_IsSkipButtonPressed);
                    }
                } else if (*slot == targetFn) {
                    uintptr_t pageStart = p & ~static_cast<uintptr_t>(0xFFF);
                    if (::mprotect(reinterpret_cast<void*>(pageStart), 4096, PROT_READ | PROT_WRITE) == 0) {
                        *slot = reinterpret_cast<void*>(&Hooked_CWorld_Process);
                        gotHookInstalled_.store(true);
                        LOGI("Installed scanned GOT Hook on _ZN6CWorld7ProcessEv at 0x%lx (orig=%p)",
                             static_cast<unsigned long>(p), targetFn);
                    }
                }
            }
        }
        return gotHookInstalled_.load();
#endif
        return false;
    }

    // ========================================================================
    // HELPER: REPRODUCIR ANIMACIÓN DE FORMA SEGURA VERIFICANDO CANIMBLOCK CARGADO
    // ========================================================================
    void safeBlendAnimation(void* clump, int groupId, int animId, float blendDelta) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!clump || !gtaSymbols_.canimManagerBlendAnimationFn || !gtaSymbols_.canimManagerAssocGroups) return;
        uintptr_t groupsBase = *reinterpret_cast<uintptr_t*>(gtaSymbols_.canimManagerAssocGroups);
        if (groupsBase == 0 || groupId < 0 || groupId >= 140) return;

        uintptr_t grpAddr = groupsBase + static_cast<size_t>(groupId) * 0x20;
        uintptr_t animBlock = *reinterpret_cast<uintptr_t*>(grpAddr + 0x00);
        if (animBlock != 0 && *reinterpret_cast<uint8_t*>(animBlock + 0x10) == 0) {
            if (gtaSymbols_.canimManagerAnimBlocks && gtaSymbols_.cstreamingRequestModelFn) {
                uintptr_t blocksBase = reinterpret_cast<uintptr_t>(gtaSymbols_.canimManagerAnimBlocks);
                if (animBlock >= blocksBase) {
                    size_t blockIdx = (animBlock - blocksBase) / 0x20;
                    if (blockIdx < 180) {
                        gtaSymbols_.cstreamingRequestModelFn(25575 + static_cast<int>(blockIdx), 2);
                    }
                }
            }
            return;
        }

        uintptr_t assocArray = *reinterpret_cast<uintptr_t*>(grpAddr + 0x08);
        int32_t numAnims = *reinterpret_cast<int32_t*>(grpAddr + 0x10);
        int32_t idOffset = *reinterpret_cast<int32_t*>(grpAddr + 0x14);
        if (assocArray == 0 || numAnims <= 0) return;

        int32_t targetAnimId = (animId < idOffset) ? idOffset : animId;
        int32_t relIdx = targetAnimId - idOffset;
        if (relIdx < 0 || relIdx >= numAnims) return;

        uintptr_t staticAssoc = assocArray + static_cast<size_t>(relIdx) * 0x20;
        uintptr_t hierarchy = *reinterpret_cast<uintptr_t*>(staticAssoc + 0x18);
        if (hierarchy == 0 || *reinterpret_cast<uintptr_t*>(hierarchy + 0x08) == 0) return;

        gtaSymbols_.canimManagerBlendAnimationFn(clump, groupId, targetAnimId, blendDelta);
#endif
    }

    // ========================================================================
    // HELPER: SINCRONIZAR SALUD Y DAÑO VISUAL 3D (CDamageManager) DE UN CVEHICLE
    // ========================================================================
    void applyVehicleDamageState(
        void* veh, float health,
        uint32_t wheels, uint32_t doorsLo, uint16_t doorsHi, uint32_t lights, uint32_t panels
    ) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!veh) return;
        uintptr_t vehAddr = reinterpret_cast<uintptr_t>(veh);
        if (std::isfinite(health) && health > 0.0f) {
            *reinterpret_cast<float*>(vehAddr + 0x634) = std::clamp(health, 50.0f, 1000.0f);
        }
        int32_t vehSubClass = *reinterpret_cast<int32_t*>(vehAddr + 0x734);
        if (vehSubClass == 0) { // VEHICLE_AUTOMOBILE
            uint32_t* curWheels = reinterpret_cast<uint32_t*>(vehAddr + 0x75c);
            uint32_t* curDoorsLo = reinterpret_cast<uint32_t*>(vehAddr + 0x760);
            uint16_t* curDoorsHi = reinterpret_cast<uint16_t*>(vehAddr + 0x764);
            uint32_t* curLights = reinterpret_cast<uint32_t*>(vehAddr + 0x768);
            uint32_t* curPanels = reinterpret_cast<uint32_t*>(vehAddr + 0x76c);
            if (*curWheels != wheels || *curDoorsLo != doorsLo || *curDoorsHi != doorsHi ||
                *curLights != lights || *curPanels != panels) {
                *curWheels = wheels;
                *curDoorsLo = doorsLo;
                *curDoorsHi = doorsHi;
                *curLights = lights;
                *curPanels = panels;
                if (gtaSymbols_.cautomobileSetupDamageAfterLoadFn && *reinterpret_cast<void**>(vehAddr + 0x20) != nullptr) {
                    gtaSymbols_.cautomobileSetupDamageAfterLoadFn(veh);
                }
            }
        }
#endif
    }

    // ========================================================================
    // HELPER: ACTUALIZAR POSICIÓN, ROTACIÓN Y ANIMACIÓN DE UN CPED SIN BORRAR VELOCIDAD
    // ========================================================================
    void applySmoothPedTransformAndAnim(
        void* ped, float targetX, float targetY, float targetZ,
        float vx, float vy, float vz, float heading, int moveState, float health,
        uint64_t lastNetworkUpdateMs = 0, bool isCrouching = false
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

        // Sincronizar estado de agachado (CTaskSimpleDuck en slot secundario 1 = TASK_SECONDARY_DUCK)
        if (pedIntel != 0 && gtaSymbols_.cpedIntelGetTaskDuckFn) {
            void* duckTask = gtaSymbols_.cpedIntelGetTaskDuckFn(reinterpret_cast<const void*>(pedIntel), false);
            if (isCrouching) {
                if (!duckTask && gtaSymbols_.ctaskSimpleDuckToggleCtorFn && gtaSymbols_.ctaskSimpleDuckToggleProcessPedFn) {
                    alignas(16) uint8_t duckToggleBuf[64] = {0};
                    gtaSymbols_.ctaskSimpleDuckToggleCtorFn(duckToggleBuf, 1);
                    gtaSymbols_.ctaskSimpleDuckToggleProcessPedFn(duckToggleBuf, ped);
                    duckTask = gtaSymbols_.cpedIntelGetTaskDuckFn(reinterpret_cast<const void*>(pedIntel), false);
                }
                if (duckTask && gtaSymbols_.ctaskSimpleDuckControlDuckMoveFn) {
                    float moveY = (speed2d > 0.012f) ? -1.0f : 0.0f;
                    gtaSymbols_.ctaskSimpleDuckControlDuckMoveFn(duckTask, CVector2D{0.0f, moveY});
                }
                return; // Mientras está agachado, CTaskSimpleDuck controla la animación de movimiento
            } else if (duckTask) {
                if (gtaSymbols_.ctaskSimpleDuckToggleCtorFn && gtaSymbols_.ctaskSimpleDuckToggleProcessPedFn) {
                    alignas(16) uint8_t duckToggleBuf[64] = {0};
                    gtaSymbols_.ctaskSimpleDuckToggleCtorFn(duckToggleBuf, 0);
                    gtaSymbols_.ctaskSimpleDuckToggleProcessPedFn(duckToggleBuf, ped);
                } else if (gtaSymbols_.ctaskManagerSetTaskSecondaryFn) {
                    gtaSymbols_.ctaskManagerSetTaskSecondaryFn(reinterpret_cast<void*>(pedIntel + 8), nullptr, 1);
                }
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
#endif
    }

    // ========================================================================
    // HELPER: ACTUALIZAR TRANSFORMACIÓN 3D, COLOR, DAÑO Y VELOCIDAD DE UN CVEHICLE SINCRONIZADO
    // ========================================================================
    void applySmoothVehicleTransform(void* veh, const SyncedWorldVehicleEntry& ve, bool isLocalOccupiedOrEntering = false, uint64_t lastNetMs = 0) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!veh) return;
        uintptr_t vehAddr = reinterpret_cast<uintptr_t>(veh);
        uintptr_t mat = *reinterpret_cast<uintptr_t*>(vehAddr + 0x18);
        if (mat == 0 || !gtaSymbols_.cworldRemoveFn || !gtaSymbols_.cworldAddFn) return;

        if (ve.packedColors != 0) {
            *reinterpret_cast<uint32_t*>(vehAddr + 0x574) = ve.packedColors;
        }
        applyVehicleDamageState(
            veh, ve.vehicleHealth,
            ve.damageWheels, ve.damageDoorsLow, ve.damageDoorsHigh, ve.damageLights, ve.damagePanels
        );

        float vx = std::isfinite(ve.vx) ? ve.vx : 0.0f;
        float vy = std::isfinite(ve.vy) ? ve.vy : 0.0f;
        float vz = std::isfinite(ve.vz) ? ve.vz : 0.0f;

        // Dead-reckoning (extrapolación suave por latencia Wi-Fi) para eliminar el efecto serrucho/trabado en la pantalla del pasajero
        float leadFrames = 0.0f;
        if (lastNetMs > 0) {
            uint64_t nowMs = currentTimeMillis();
            if (nowMs > lastNetMs && (nowMs - lastNetMs) < 180) {
                leadFrames = std::clamp(static_cast<float>(nowMs - lastNetMs) / 16.666f, 0.0f, 3.0f);
            }
        }
        float targetX = ve.x + vx * leadFrames;
        float targetY = ve.y + vy * leadFrames;
        float targetZ = ve.z + vz * leadFrames;

        float curX = *reinterpret_cast<float*>(mat + 0x30);
        float curY = *reinterpret_cast<float*>(mat + 0x34);
        float curZ = *reinterpret_cast<float*>(mat + 0x38);
        float dx = targetX - curX;
        float dy = targetY - curY;
        float dz = targetZ - curZ;
        float distSq = dx * dx + dy * dy + dz * dz;

        float posAlpha = isLocalOccupiedOrEntering ? 0.25f : 0.38f;
        float nextX = (distSq > 64.0f) ? targetX : (curX + dx * posAlpha);
        float nextY = (distSq > 64.0f) ? targetY : (curY + dy * posAlpha);
        float nextZ = (distSq > 64.0f) ? targetZ : (curZ + dz * posAlpha);

        // Si localPed está subiendo o viajando dentro de este vehículo, jamás llamar CWorld::Remove/Add cada 10 frames
        // (eso abortaría CTaskComplexEnterCarAsPassenger y provocaría tirones/glitches al pasajero)
        bool needSectorRelink = isLocalOccupiedOrEntering
            ? (distSq >= 64.0f)
            : ((distSq >= 2.25f) || ((mainThreadTicks_.load() % 10) == 0));
        if (needSectorRelink) {
            gtaSymbols_.cworldRemoveFn(veh);
        }

        // Interpolar y re-ortonormalizar la matriz de rotación para giros fluidos sin saltos bruscos
        float curRx = *reinterpret_cast<float*>(mat + 0x00);
        float curRy = *reinterpret_cast<float*>(mat + 0x04);
        float curRz = *reinterpret_cast<float*>(mat + 0x08);
        float curUx = *reinterpret_cast<float*>(mat + 0x10);
        float curUy = *reinterpret_cast<float*>(mat + 0x14);
        float curUz = *reinterpret_cast<float*>(mat + 0x18);

        float rotAlpha = (distSq > 64.0f) ? 1.0f : (isLocalOccupiedOrEntering ? 0.28f : 0.40f);
        float ux = curUx + (ve.upX - curUx) * rotAlpha;
        float uy = curUy + (ve.upY - curUy) * rotAlpha;
        float uz = curUz + (ve.upZ - curUz) * rotAlpha;
        float uLen = std::sqrt(ux * ux + uy * uy + uz * uz);
        if (uLen > 1e-4f) {
            ux /= uLen; uy /= uLen; uz /= uLen;
        } else {
            ux = ve.upX; uy = ve.upY; uz = ve.upZ;
        }

        float rxTmp = curRx + (ve.rightX - curRx) * rotAlpha;
        float ryTmp = curRy + (ve.rightY - curRy) * rotAlpha;
        float rzTmp = curRz + (ve.rightZ - curRz) * rotAlpha;
        float ax = ryTmp * uz - rzTmp * uy;
        float ay = rzTmp * ux - rxTmp * uz;
        float az = rxTmp * uy - ryTmp * ux;
        float aLen = std::sqrt(ax * ax + ay * ay + az * az);
        if (aLen > 1e-4f) {
            ax /= aLen; ay /= aLen; az /= aLen;
        } else {
            ax = 0.0f; ay = 0.0f; az = 1.0f;
        }
        float rx = uy * az - uz * ay;
        float ry = uz * ax - ux * az;
        float rz = ux * ay - uy * ax;

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
        // Conservar la velocidad real del vehículo para que CWorld::Process avance suavemente a 60fps
        *reinterpret_cast<float*>(vehAddr + 0x68) = vx;
        *reinterpret_cast<float*>(vehAddr + 0x6c) = vy;
        *reinterpret_cast<float*>(vehAddr + 0x70) = vz;
        if (isLocalOccupiedOrEntering) {
            *reinterpret_cast<float*>(vehAddr + 0x74) = 0.0f;
            *reinterpret_cast<float*>(vehAddr + 0x78) = 0.0f;
            *reinterpret_cast<float*>(vehAddr + 0x7c) = 0.0f;
        }

        if (gtaSymbols_.centityUpdateRwFrameFn) {
            gtaSymbols_.centityUpdateRwFrameFn(veh);
        }
        if (needSectorRelink) {
            gtaSymbols_.cworldAddFn(veh);
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
        uintptr_t pedIntel = *reinterpret_cast<uintptr_t*>(pedAddr + 0x538);
        if (ped != localPed && pedIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
            for (int ts = 0; ts < 4; ++ts) {
                if (*reinterpret_cast<void**>(pedIntel + 0x08 + static_cast<size_t>(ts) * 8) != nullptr) {
                    gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(pedIntel + 8), nullptr, ts, false);
                }
            }
        }
        if (isCoopPlayer && gtaSymbols_.ccarEnterExitSetPedInCarDirectFn) {
            gtaSymbols_.ccarEnterExitSetPedInCarDirectFn(ped, veh, 0, true);
            // SetPedInCarDirect(..., true) agrega CTaskComplexCarDriveWander en el slot 3 (IA errante);
            // limpiamos el slot 3 en actores remotos para que mantengan únicamente CTaskSimpleCarDrive en el slot 4.
            if (ped != localPed && pedIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
                gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(pedIntel + 8), nullptr, 3, false);
            }
            return;
        }
        if (gtaSymbols_.ctaskSetPedInAsDriverCtorFn && gtaSymbols_.ctaskSetPedInAsDriverProcessFn && gtaSymbols_.ctaskSetPedInAsDriverDtorFn) {
            alignas(16) uint8_t taskBuf[128] = {0};
            gtaSymbols_.ctaskSetPedInAsDriverCtorFn(taskBuf, veh, nullptr);
            taskBuf[0x30] = 1; // m_bWarpingInToCar = true
            gtaSymbols_.ctaskSetPedInAsDriverProcessFn(taskBuf, ped);
            gtaSymbols_.ctaskSetPedInAsDriverDtorFn(taskBuf);
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
        uintptr_t pedIntel = *reinterpret_cast<uintptr_t*>(pedAddr + 0x538);
        if (ped != localPed && pedIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
            for (int ts = 0; ts < 4; ++ts) {
                if (*reinterpret_cast<void**>(pedIntel + 0x08 + static_cast<size_t>(ts) * 8) != nullptr) {
                    gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(pedIntel + 8), nullptr, ts, false);
                }
            }
        }
        int door = gtaSymbols_.ccarEnterExitComputePassengerDoorFn
            ? gtaSymbols_.ccarEnterExitComputePassengerDoorFn(veh, clampedSlot)
            : 8;
        if (isCoopPlayer && gtaSymbols_.ccarEnterExitSetPedInCarDirectFn) {
            gtaSymbols_.ccarEnterExitSetPedInCarDirectFn(ped, veh, door, false);
            if (ped != localPed && pedIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
                gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(pedIntel + 8), nullptr, 3, false);
            }
            return;
        }
        if (gtaSymbols_.ctaskSetPedInAsPassengerCtorFn && gtaSymbols_.ctaskSetPedInAsPassengerProcessFn && gtaSymbols_.ctaskSetPedInAsPassengerDtorFn) {
            alignas(16) uint8_t taskBuf[128] = {0};
            gtaSymbols_.ctaskSetPedInAsPassengerCtorFn(taskBuf, veh, door, nullptr);
            taskBuf[0x38] = 1; // m_bWarpingInToCar = true
            gtaSymbols_.ctaskSetPedInAsPassengerProcessFn(taskBuf, ped);
            gtaSymbols_.ctaskSetPedInAsPassengerDtorFn(taskBuf);
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
            if (!cutsceneSkipAlreadyTriggered_ && cutsceneSkipVotesMask_ != 0 && g_origCCutsceneMgrSkipCutscene) {
                uint64_t nowCutMs = currentTimeMillis();
                int localCutIdx = std::clamp(config_.localPlayerSlot, 1, kMaxCoopPlayers) - 1;
                uint8_t activeMask = static_cast<uint8_t>(1U << localCutIdx);
                for (int i = 0; i < kMaxCoopPlayers; ++i) {
                    if (i != localCutIdx && players_[i].isActive && players_[i].lastNetworkUpdateMs > 0 &&
                        (nowCutMs - players_[i].lastNetworkUpdateMs) < 15000) {
                        activeMask |= static_cast<uint8_t>(1U << i);
                    }
                }
                if ((cutsceneSkipVotesMask_ & activeMask) == activeMask) {
                    if (cutsceneSkipApprovedUntilMs_ < nowCutMs) {
                        cutsceneSkipApprovedUntilMs_ = nowCutMs + 1200;
                    }
                    cutsceneSkipAlreadyTriggered_ = true;
                    LOGI("All co-op players voted to skip cutscene (mask=0x%x)! Skipping simultaneously.", activeMask);
                    g_origCCutsceneMgrSkipCutscene();
                }
            }
            return;
        } else {
            cutsceneSkipAlreadyTriggered_ = false;
            if (cutsceneSkipVotesMask_ != 0) {
                uint64_t nowCutMs = currentTimeMillis();
                if (nowCutMs > cutsceneSkipApprovedUntilMs_ && (nowCutMs - lastCutsceneVoteMs_) > 2500) {
                    cutsceneSkipVotesMask_ = 0;
                }
            }
        }

        void* localPed = gtaSymbols_.findPlayerPedFn(-1);
        if (!localPed) {
            // El jugador aún está en el menú principal o pantalla de carga
            lastLocalPedPtr_ = nullptr;
            localAppliedSkinReqId_ = 0;
            localPassengerDriveByFiring_.store(false, std::memory_order_relaxed);
            localPassengerDriveByPed_.store(nullptr, std::memory_order_relaxed);
            for (int i = 0; i < kMaxCoopPlayers; ++i) {
                remotePedPtr_[i] = nullptr;
                remotePedHandle_[i] = -1;
                remotePedAppliedSkinId_[i] = 0;
                specialSlotRequestedSkin_[i] = -1;
                remoteWasJumping_[i] = false;
                remotePartnerVehPtr_[i] = nullptr;
                remotePartnerVehHandle_[i] = -1;
                remoteLastVehSpawnTick_[i] = 0;
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
                worldSyncVehSpawnedByUs_[v] = false;
            }
            localRoofVehPtr_ = nullptr;
            return;
        }

        if (localPed != lastLocalPedPtr_) {
            // Se acaba de cargar una partida o reiniciar misión
            lastLocalPedPtr_ = localPed;
            localAppliedSkinReqId_ = 0;
            initialSyncTeleportDone_ = false;
            initialAlleyFreeRoamDone_ = false;
            alleyLandWaitTicks_ = 0;
            for (int i = 0; i < kMaxCoopPlayers; ++i) {
                remotePedPtr_[i] = nullptr;
                remotePedHandle_[i] = -1;
                remotePedAppliedSkinId_[i] = 0;
                specialSlotRequestedSkin_[i] = -1;
                remoteWasJumping_[i] = false;
                remotePartnerVehPtr_[i] = nullptr;
                remotePartnerVehHandle_[i] = -1;
                remoteLastVehSpawnTick_[i] = 0;
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
                worldSyncVehSpawnedByUs_[v] = false;
            }
            localRoofVehPtr_ = nullptr;
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
        // durante la cinemática de apertura del aeropuerto/patrulla ($ONMISSION != 0), no intervenir ni spawnear entidades.
        // En modo libre ($ONMISSION == 0) en Grove Street jamás bloquear la casa de CJ.
        bool onMissionNowPre = (gtaSymbols_.onAMissionFlagOffset && *gtaSymbols_.onAMissionFlagOffset != 0) ||
                               (gtaSymbols_.isPlayerOnAMissionFn && gtaSymbols_.isPlayerOnAMissionFn());
        bool inIntroHoldingSpot = (onMissionNowPre &&
                                   std::fabs(myPos.x - 2495.0f) < 2.5f &&
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

        // Leer el arma activa real (+0x8dc = m_nActiveWeaponSlot, +0x730 = m_aWeapons[13] de 0x20 bytes)
        // y las tareas secundarias de combate/disparo (GetTaskFighting / GetTaskUseGun) del jugador local
        {
            uint8_t activeWepSlot = *reinterpret_cast<uint8_t*>(localPedAddr + 0x8dc);
            int32_t curWepType = 0;
            uint32_t curWepState = 0;
            if (activeWepSlot < 13) {
                uintptr_t wepEntry = localPedAddr + 0x730 + static_cast<size_t>(activeWepSlot) * 0x20;
                curWepType = *reinterpret_cast<int32_t*>(wepEntry + 0x00);
                curWepState = *reinterpret_cast<uint32_t*>(wepEntry + 0x04);
            }
            me.weaponId = std::clamp(curWepType, 0, 46);

            uint16_t combatFlags = 0;
            bool localIsCrouching = ((*reinterpret_cast<uint8_t*>(localPedAddr + 0x587) & 0x04u) != 0);
            bool localIsJumping = false;
            uintptr_t localIntelPtr = *reinterpret_cast<uintptr_t*>(localPedAddr + 0x538);
            if (localIntelPtr != 0) {
                if (gtaSymbols_.cpedIntelGetTaskFightingFn &&
                    gtaSymbols_.cpedIntelGetTaskFightingFn(reinterpret_cast<const void*>(localIntelPtr)) != nullptr) {
                    combatFlags |= 0x01u; // Golpeando / atacando cuerpo a cuerpo
                }
                if (gtaSymbols_.cpedIntelGetTaskUseGunFn &&
                    gtaSymbols_.cpedIntelGetTaskUseGunFn(reinterpret_cast<const void*>(localIntelPtr)) != nullptr) {
                    combatFlags |= 0x02u; // Apuntando / disparando arma de fuego
                }
                if (!localIsCrouching && gtaSymbols_.cpedIntelGetTaskDuckFn &&
                    gtaSymbols_.cpedIntelGetTaskDuckFn(reinterpret_cast<const void*>(localIntelPtr), false) != nullptr) {
                    localIsCrouching = true;
                }
                if (gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn) {
                    void* localTaskMgr = reinterpret_cast<void*>(localIntelPtr + 0x08);
                    if (gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(localTaskMgr, 210 /*TASK_SIMPLE_JUMP*/) != nullptr ||
                        gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(localTaskMgr, 211 /*TASK_COMPLEX_JUMP*/) != nullptr ||
                        gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(localTaskMgr, 267 /*TASK_COMPLEX_CLIMB*/) != nullptr) {
                        localIsJumping = true;
                    }
                }
            }
            if (!myVeh && !localIsJumping && mySpeed.z > 0.08f) {
                localIsJumping = true;
            }
            if (curWepState == 1) { // WEAPONSTATE_FIRING
                combatFlags |= (me.weaponId >= 22) ? 0x06u : 0x01u;
                if (me.weaponId >= 22) {
                    pendingWeaponFireBroadcast_.store(true, std::memory_order_relaxed);
                }
            }
            me.animGroupId = combatFlags;
            me.isCrouching = localIsCrouching ? 1u : 0u;
            me.isJumping = localIsJumping ? 1u : 0u;
            me.isAiming = ((combatFlags & 0x02u) != 0) ? 1u : 0u;
        }

        // Detectar si el jugador local a pie tiene una tarea activa de entrada/robo de vehículo:
        // En libGTASA.so arm64 v2.10: 700 = CTaskComplexEnterCarAsPassenger, 701 = CTaskComplexEnterCarAsDriver, 713 = EnterCarAsDriverTimed
        bool localTryingToEnterCar = false;
        bool localEnteringAsDriver = false;
        bool localAlreadyEnteringAsPassenger = false;
        if (!myVeh) {
            uintptr_t localIntel = *reinterpret_cast<uintptr_t*>(localPedAddr + 0x538);
            if (localIntel != 0 && gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn) {
                void* taskMgr = reinterpret_cast<void*>(localIntel + 0x08);
                if (gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(taskMgr, 700 /*TASK_COMPLEX_ENTER_CAR_AS_PASSENGER*/) ||
                    gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(taskMgr, 712 /*TASK_COMPLEX_ENTER_CAR_AS_PASSENGER_WAIT*/)) {
                    localTryingToEnterCar = true;
                    localAlreadyEnteringAsPassenger = true;
                } else if (gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(taskMgr, 701 /*TASK_COMPLEX_ENTER_CAR_AS_DRIVER*/) ||
                           gtaSymbols_.ctaskManagerFindActiveTaskByTypeFn(taskMgr, 713 /*TASK_COMPLEX_ENTER_CAR_AS_DRIVER_TIMED*/)) {
                    localTryingToEnterCar = true;
                    localEnteringAsDriver = true;
                }
            }
            if (localEnteringAsDriver && !localAlreadyEnteringAsPassenger) {
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
                                if (worldSyncVehPtr_[v] && worldSyncVehHostSlot_[v] == wantedSlot &&
                                    worldSyncVehModel_[v] == players_[r].vehicleModelId) {
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
                                    void* cand = reinterpret_cast<void*>(vObjects + static_cast<size_t>(wantedSlot) * 0xc68);
                                    int16_t candModel = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(cand) + 0x32);
                                    if (candModel == players_[r].vehicleModelId) {
                                        partnerVeh = cand;
                                    }
                                }
                            }
                        }
                    }
                    if (!partnerVeh) continue;
                    uintptr_t pvMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(partnerVeh) + 0x18);
                    if (pvMat == 0) continue;
                    float dvx = *reinterpret_cast<float*>(pvMat + 0x30) - myPos.x;
                    float dvy = *reinterpret_cast<float*>(pvMat + 0x34) - myPos.y;
                    if ((dvx * dvx + dvy * dvy) < (9.5f * 9.5f)) {
                        uintptr_t pvAddr = reinterpret_cast<uintptr_t>(partnerVeh);
                        int16_t pvModel = *reinterpret_cast<int16_t*>(pvAddr + 0x32);
                        bool pvIsBike = (pvModel == 481 || pvModel == 509 || pvModel == 510 ||
                                         pvModel == 448 || pvModel == 461 || pvModel == 462 ||
                                         pvModel == 463 || pvModel == 468 || pvModel == 471 ||
                                         pvModel == 521 || pvModel == 522 || pvModel == 523 ||
                                         pvModel == 581 || pvModel == 586);
                        uint8_t numPass = *reinterpret_cast<uint8_t*>(pvAddr + 0x5e8);
                        uint8_t maxPass = *reinterpret_cast<uint8_t*>(pvAddr + 0x5ec);
                        int freePassSlot = -1;
                        if (maxPass > 0) {
                            int slotLimit = std::min<int>(static_cast<int>(maxPass), 3);
                            for (int s = 0; s < slotLimit; ++s) {
                                void* occ = *reinterpret_cast<void**>(pvAddr + 0x5a8 + static_cast<size_t>(s) * 8);
                                if (occ == nullptr || occ == localPed) {
                                    freePassSlot = s;
                                    break;
                                }
                            }
                        }
                        bool hasFreePassengerSeat = (freePassSlot >= 0);
                        if (hasFreePassengerSeat) {
                            float distToPartnerVehSq = dvx * dvx + dvy * dvy;
                            int targetDoor = gtaSymbols_.ccarEnterExitComputePassengerDoorFn
                                ? gtaSymbols_.ccarEnterExitComputePassengerDoorFn(partnerVeh, freePassSlot)
                                : ((freePassSlot == 0) ? 8 : ((freePassSlot == 1) ? 11 : 9));
                            if (targetDoor <= 0) {
                                targetDoor = (freePassSlot == 0) ? 8 : ((freePassSlot == 1) ? 11 : 9);
                            }
                            float pvx = *reinterpret_cast<float*>(pvAddr + 0x68);
                            float pvy = *reinterpret_cast<float*>(pvAddr + 0x6c);
                            bool partnerCarMoving = (pvx * pvx + pvy * pvy) > (0.04f * 0.04f);
                            // Si estamos cerca (< 6.0m) o el auto del compañero está en movimiento por red, sentar directo como pasajero
                            // para que jamás aborte CTaskComplexGoToCarDoorAndStandStill ni intente bajar al conductor
                            if (distToPartnerVehSq < (6.0f * 6.0f) || partnerCarMoving ||
                                !gtaSymbols_.ctaskComplexEnterCarAsPassengerCtorFn ||
                                !gtaSymbols_.ctaskOperatorNewFn || !gtaSymbols_.ctaskManagerSetTaskFn) {
                                if (localIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
                                    gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(localIntel + 0x08), nullptr, 3, false);
                                }
                                warpPedIntoVehiclePassenger(localPed, partnerVeh, freePassSlot, localPed, true);
                                myVeh = partnerVeh;
                                localEnteringAsDriver = false;
                                localAlreadyEnteringAsPassenger = false;
                                LOGI("Seated localPed directly as passenger slot=%d (door=%d) in partnerVeh=%p",
                                     freePassSlot, targetDoor, partnerVeh);
                            } else if (localIntel != 0) {
                                void* passTask = gtaSymbols_.ctaskOperatorNewFn(0x70);
                                if (passTask) {
                                    gtaSymbols_.ctaskComplexEnterCarAsPassengerCtorFn(passTask, partnerVeh, targetDoor, false);
                                    gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(localIntel + 0x08), passTask, 3, false);
                                    localEnteringAsDriver = false;
                                    localAlreadyEnteringAsPassenger = true;
                                    LOGI("Redirected localPed to CTaskComplexEnterCarAsPassenger (700) slot=%d door=%d on partnerVeh=%p",
                                         freePassSlot, targetDoor, partnerVeh);
                                }
                            }
                        } else if (!pvIsBike) {
                            // No hay asiento libre en un vehículo de 4 ruedas (1 plaza o lleno): subir al techo del vehículo del compañero
                            if (localIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
                                gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(localIntel + 0x08), nullptr, 3, false);
                            }
                            localRoofVehPtr_ = partnerVeh;
                            localRoofAttachTick_ = tickCount;
                            me.isAttachedOnRoof = true;
                            me.seatIndex = 4;
                            me.vehicleNetId = players_[r].vehicleNetId;
                            me.vehicleModelId = static_cast<int32_t>(pvModel);
                            localEnteringAsDriver = false;
                            LOGI("Attached localPed onto roof of full partnerVeh=%p (numPass=%u, maxPass=%u)",
                                 partnerVeh, static_cast<unsigned>(numPass), static_cast<unsigned>(maxPass));
                        } else {
                            // Es una bicicleta/moto de 1 plaza ocupada por el compañero: cancelar robo y spawnear una bici propia al lado para que cada uno tenga la suya
                            if (localIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
                                gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(localIntel + 0x08), nullptr, 3, false);
                            }
                            localEnteringAsDriver = false;
                            if (gtaSymbols_.ccarCtrlCreateCarForScriptFn && gtaSymbols_.isModelLoaded(pvModel) &&
                                (tickCount - lastLocalSpareBikeTick_) >= 120) {
                                lastLocalSpareBikeTick_ = tickCount;
                                CVector sparePos{myPos.x + 1.8f, myPos.y + 1.2f, myPos.z + 0.3f};
                                void* spareBike = gtaSymbols_.ccarCtrlCreateCarForScriptFn(pvModel, sparePos, 0);
                                if (spareBike) {
                                    *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(spareBike) + 0x610) = 2; // MISSION_VEHICLE
                                    warpPedIntoVehicleDriver(localPed, spareBike, localPed, true);
                                    myVeh = spareBike;
                                    LOGI("Spawned personal spare bike (model=%d, ptr=%p) so both players have separate bikes!", pvModel, spareBike);
                                }
                            }
                        }
                        break;
                    }
                }
            }

            // Mantener al jugador local sobre el techo si subió al techo de un vehículo lleno (salta o pulsa subir de nuevo para bajarse)
            if (me.isAttachedOnRoof && localRoofVehPtr_ != nullptr) {
                bool wantDetach = (me.isJumping != 0 && (tickCount - localRoofAttachTick_) > 20) ||
                                  (localTryingToEnterCar && (tickCount - localRoofAttachTick_) > 25);
                uintptr_t rvAddr = reinterpret_cast<uintptr_t>(localRoofVehPtr_);
                uintptr_t rvMat = *reinterpret_cast<uintptr_t*>(rvAddr + 0x18);
                if (wantDetach || rvMat == 0) {
                    localRoofVehPtr_ = nullptr;
                    me.isAttachedOnRoof = false;
                    me.seatIndex = -1;
                    me.vehicleNetId = 0;
                    me.vehicleModelId = 0;
                } else {
                    float rx = *reinterpret_cast<float*>(rvMat + 0x30) + *reinterpret_cast<float*>(rvMat + 0x20) * 1.18f;
                    float ry = *reinterpret_cast<float*>(rvMat + 0x34) + *reinterpret_cast<float*>(rvMat + 0x24) * 1.18f;
                    float rz = *reinterpret_cast<float*>(rvMat + 0x38) + *reinterpret_cast<float*>(rvMat + 0x28) * 1.18f;
                    float rvx = *reinterpret_cast<float*>(rvAddr + 0x68);
                    float rvy = *reinterpret_cast<float*>(rvAddr + 0x6c);
                    float rvz = *reinterpret_cast<float*>(rvAddr + 0x70);
                    uintptr_t lMat = *reinterpret_cast<uintptr_t*>(localPedAddr + 0x18);
                    if (lMat != 0) {
                        *reinterpret_cast<float*>(lMat + 0x30) = rx;
                        *reinterpret_cast<float*>(lMat + 0x34) = ry;
                        *reinterpret_cast<float*>(lMat + 0x38) = rz;
                    }
                    *reinterpret_cast<float*>(localPedAddr + 0x08) = rx;
                    *reinterpret_cast<float*>(localPedAddr + 0x0c) = ry;
                    *reinterpret_cast<float*>(localPedAddr + 0x10) = rz;
                    *reinterpret_cast<float*>(localPedAddr + 0x68) = rvx;
                    *reinterpret_cast<float*>(localPedAddr + 0x6c) = rvy;
                    *reinterpret_cast<float*>(localPedAddr + 0x70) = rvz;
                    if (gtaSymbols_.centityUpdateRwFrameFn) {
                        gtaSymbols_.centityUpdateRwFrameFn(localPed);
                    }
                    me.x = rx; me.y = ry; me.z = rz;
                    me.vx = rvx; me.vy = rvy; me.vz = rvz;
                }
            }
        } else if (me.isAttachedOnRoof) {
            localRoofVehPtr_ = nullptr;
            me.isAttachedOnRoof = false;
        }

        int32_t resolvedVehNetId = 0;
        if (myVeh) {
            uintptr_t myVehAddr = reinterpret_cast<uintptr_t>(myVeh);
            void* curDrv = *reinterpret_cast<void**>(myVehAddr + 0x5a0);
            if (curDrv == localPed) {
                me.seatIndex = 0;
            } else {
                int foundPassSlot = 0;
                for (int s = 0; s < 3; ++s) {
                    if (*reinterpret_cast<void**>(myVehAddr + 0x5a8 + static_cast<size_t>(s) * 8) == localPed) {
                        foundPassSlot = s;
                        break;
                    }
                }
                me.seatIndex = static_cast<int8_t>(foundPassSlot + 1);
            }
            me.isAttachedOnRoof = false;
            me.vehicleModelId = static_cast<int32_t>(*reinterpret_cast<int16_t*>(myVehAddr + 0x32));
            me.vehicleHealth = *reinterpret_cast<float*>(myVehAddr + 0x634);
            me.damageWheels = *reinterpret_cast<uint32_t*>(myVehAddr + 0x75c);
            me.damageDoorsLow = *reinterpret_cast<uint32_t*>(myVehAddr + 0x760);
            me.damageDoorsHigh = *reinterpret_cast<uint16_t*>(myVehAddr + 0x764);
            me.damageLights = *reinterpret_cast<uint32_t*>(myVehAddr + 0x768);
            me.damagePanels = *reinterpret_cast<uint32_t*>(myVehAddr + 0x76c);
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
            if (resolvedVehNetId == 0 && me.seatIndex > 0) {
                for (int r = 0; r < kMaxCoopPlayers; ++r) {
                    if (r != localIdx && players_[r].isActive && players_[r].seatIndex == 0 &&
                        players_[r].vehicleModelId == me.vehicleModelId && players_[r].vehicleNetId > 0) {
                        resolvedVehNetId = players_[r].vehicleNetId;
                        break;
                    }
                }
            }
            if (resolvedVehNetId == 0) resolvedVehNetId = 101;

            // Habilitar disparo real desde la ventana (Drive-By 360° manual + estilo IA) para el acompañante (me.seatIndex > 0).
            // Al entrar como pasajero, CTaskSimpleCarSetPedInAsPassenger::ProcessPed (0x610d14) llama a RemoveWeaponWhenEnteringVehicle(0)
            // que des equipa el arma activa a slot 0 (puño); aquí restauramos automáticamente su subfusil/arma de fuego en mano.
            if (me.seatIndex > 0) {
                if (me.weaponId < 22 || me.weaponId > 34) {
                    int bestSlot = -1;
                    int32_t bestWepType = 0;
                    // Priorizar slot 4 (Micro Uzi 28, MP5 29, Tec-9 32), luego pistolas (slot 2), rifles (slot 5) y escopetas (slot 3)
                    const int slotOrder[5] = {4, 2, 5, 3, 6};
                    for (int idx = 0; idx < 5; ++idx) {
                        int s = slotOrder[idx];
                        uintptr_t wepEntry = localPedAddr + 0x730 + static_cast<size_t>(s) * 0x20;
                        int32_t wType = *reinterpret_cast<int32_t*>(wepEntry + 0x00);
                        uint32_t wTotal = *reinterpret_cast<uint32_t*>(wepEntry + 0x0c);
                        if (wType >= 22 && wType <= 34 && wTotal > 0) {
                            bestSlot = s;
                            bestWepType = wType;
                            break;
                        }
                    }
                    // Si el pasajero no tiene ningún arma con balas, equiparle una Micro Uzi (28) de respaldo
                    if (bestSlot < 0 && gtaSymbols_.cweaponInfoGetWeaponInfoFn && gtaSymbols_.cpedGiveWeaponFn) {
                        void* uziInfo = gtaSymbols_.cweaponInfoGetWeaponInfoFn(28, 1);
                        int32_t uziModel = uziInfo ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(uziInfo) + 0x0c) : 352;
                        if (uziModel > 0 && !gtaSymbols_.isModelLoaded(uziModel)) {
                            if ((tickCount % 5 == 0) && gtaSymbols_.cstreamingRequestModelFn) {
                                gtaSymbols_.cstreamingRequestModelFn(uziModel, 2);
                            }
                        } else {
                            gtaSymbols_.cpedGiveWeaponFn(localPed, 28, 500, true);
                            bestSlot = 4;
                            bestWepType = 28;
                        }
                    }
                    if (bestSlot >= 0 && bestWepType >= 22) {
                        if (gtaSymbols_.cpedSetCurrentWeaponSlotFn) {
                            gtaSymbols_.cpedSetCurrentWeaponSlotFn(localPed, bestSlot);
                        } else if (gtaSymbols_.cpedSetCurrentWeaponFn) {
                            gtaSymbols_.cpedSetCurrentWeaponFn(localPed, bestWepType);
                        }
                        *reinterpret_cast<uint8_t*>(localPedAddr + 0x8dc) = static_cast<uint8_t>(bestSlot);
                        if (*reinterpret_cast<void**>(localPedAddr + 0x650) == nullptr &&
                            gtaSymbols_.cweaponInfoGetWeaponInfoFn && gtaSymbols_.cpedAddWeaponModelFn) {
                            void* wInfo = gtaSymbols_.cweaponInfoGetWeaponInfoFn(bestWepType, 1);
                            int32_t wModel = wInfo ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(wInfo) + 0x0c) : 0;
                            if (wModel > 0 && gtaSymbols_.isModelLoaded(wModel)) {
                                gtaSymbols_.cpedAddWeaponModelFn(localPed, wModel);
                            }
                        }
                        me.weaponId = bestWepType;
                    }
                }

                if (me.weaponId >= 22 && me.weaponId <= 34) {
                    uintptr_t localIntel = *reinterpret_cast<uintptr_t*>(localPedAddr + 0x538);
                    if (localIntel != 0) {
                        void* secAttackTask = *reinterpret_cast<void**>(localIntel + 0x30); // m_aSecondaryTasks[0] = TASK_SECONDARY_ATTACK
                        if (secAttackTask == nullptr &&
                            gtaSymbols_.ctaskOperatorNewFn &&
                            gtaSymbols_.ctaskSimpleGangDriveByCtorFn &&
                            gtaSymbols_.ctaskManagerSetTaskSecondaryFn) {
                            void* dbTask = gtaSymbols_.ctaskOperatorNewFn(0x60);
                            if (dbTask) {
                                bool seatRHS = (me.seatIndex == 1 || me.seatIndex == 3);
                                gtaSymbols_.ctaskSimpleGangDriveByCtorFn(dbTask, nullptr, nullptr, 1000.0f, 100, 8, seatRHS);
                                gtaSymbols_.ctaskManagerSetTaskSecondaryFn(reinterpret_cast<void*>(localIntel + 0x08), dbTask, 0);
                                secAttackTask = *reinterpret_cast<void**>(localIntel + 0x30);
                                LOGI("Enabled Passenger Drive-By (CTaskSimpleGangDriveBy) on localPed=%p seat=%d wep=%d",
                                     localPed, me.seatIndex, me.weaponId);
                            }
                        }
                        // Activar cámara nativa de Drive-By desde ventanilla (MODE_AIMWEAPON_FROMCAR = 55) en TheCamera
                        // para que CTaskSimpleGangDriveBy::PlayerTarget marque m_bTargetInRange = 1 (+0x14), saque el torso por la ventanilla
                        // y muestre la mira 360° exactamente como los miembros de la pandilla en las misiones
                        if (gtaSymbols_.theCamera != nullptr) {
                            uintptr_t camBase = reinterpret_cast<uintptr_t>(gtaSymbols_.theCamera);
                            uint8_t activeCamIdx = *reinterpret_cast<uint8_t*>(camBase + 0x5f);
                            if (activeCamIdx < 3) {
                                uintptr_t activeCam = camBase + static_cast<size_t>(activeCamIdx) * 0x228;
                                uint16_t curCamMode = *reinterpret_cast<uint16_t*>(activeCam + 0x186);
                                if (curCamMode != 55 /*MODE_AIMWEAPON_FROMCAR*/ &&
                                    curCamMode != 49 /*MODE_TWOPLAYER_IN_CAR_AND_SHOOTING*/ &&
                                    curCamMode != 14 /*MODE_FIXED*/ && curCamMode != 15 /*MODE_1STPERSON*/) {
                                    if (gtaSymbols_.ccameraSetNewPlayerWeaponModeFn) {
                                        gtaSymbols_.ccameraSetNewPlayerWeaponModeFn(gtaSymbols_.theCamera, 55, 0, 0);
                                    }
                                    *reinterpret_cast<uint16_t*>(activeCam + 0x186) = 55;
                                }
                            }
                        }
                        // Mantener activo el flag de targeting en vehículo (m_nPlayerFlags |= 8 en CPlayerPedData + 0x48)
                        // y habilitar los botones táctiles de disparo en el HUD móvil (WIDGET_ATTACK=0, SHOOT_LEFT=10, SHOOT_RIGHT=11)
                        uintptr_t playerData = *reinterpret_cast<uintptr_t*>(localPedAddr + 0x540);
                        if (playerData != 0) {
                            *reinterpret_cast<uint16_t*>(playerData + 0x48) |= 0x08u;
                        }
                        bool manualFirePressed = false;
                        if (gtaSymbols_.ctouchInterfaceIsHeldDownFn) {
                            if (gtaSymbols_.ctouchInterfaceIsHeldDownFn(0 /*WIDGET_ATTACK*/, 1)) manualFirePressed = true;
                            if (gtaSymbols_.ctouchInterfaceIsHeldDownFn(10 /*WIDGET_VEHICLE_SHOOT_LEFT*/, 1)) manualFirePressed = true;
                            if (gtaSymbols_.ctouchInterfaceIsHeldDownFn(11 /*WIDGET_VEHICLE_SHOOT_RIGHT*/, 1)) manualFirePressed = true;
                        } else if (gtaSymbols_.ctouchInterfaceIsTouchedFn) {
                            if (gtaSymbols_.ctouchInterfaceIsTouchedFn(0 /*WIDGET_ATTACK*/, nullptr, 1)) manualFirePressed = true;
                        }
                        localPassengerDriveByPed_.store(localPed, std::memory_order_relaxed);
                        localPassengerDriveByFiring_.store(manualFirePressed, std::memory_order_relaxed);
                        if (secAttackTask != nullptr) {
                            uintptr_t dbAddr = reinterpret_cast<uintptr_t>(secAttackTask);
                            bool seatRHS = (me.seatIndex == 1 || me.seatIndex == 3);
                            // Calcular punto de mira 35m al frente de la cámara activa (TheCamera + m_nActiveCam*0x228 + 0x2e0)
                            CVector aimPoint{
                                myPos.x - std::sin(myHeading) * 35.0f,
                                myPos.y + std::cos(myHeading) * 35.0f,
                                myPos.z + 0.5f
                            };
                            if (gtaSymbols_.theCamera != nullptr) {
                                uintptr_t camBase = reinterpret_cast<uintptr_t>(gtaSymbols_.theCamera);
                                uint8_t activeCamIdx = *reinterpret_cast<uint8_t*>(camBase + 0x5f);
                                if (activeCamIdx < 3) {
                                    uintptr_t activeCam = camBase + static_cast<size_t>(activeCamIdx) * 0x228;
                                    float fx = *reinterpret_cast<float*>(activeCam + 0x2e0);
                                    float fy = *reinterpret_cast<float*>(activeCam + 0x2e4);
                                    float fz = *reinterpret_cast<float*>(activeCam + 0x2e8);
                                    float fLenSq = fx * fx + fy * fy + fz * fz;
                                    if (std::isfinite(fLenSq) && fLenSq > 0.1f && fLenSq < 4.0f) {
                                        aimPoint.x = myPos.x + fx * 35.0f;
                                        aimPoint.y = myPos.y + fy * 35.0f;
                                        aimPoint.z = myPos.z + 0.5f + fz * 35.0f;
                                    }
                                }
                            }
                            *reinterpret_cast<CVector*>(dbAddr + 0x50) = aimPoint;
                            *reinterpret_cast<uint8_t*>(dbAddr + 0x13) = 1; // m_bCanFire = 1
                            *reinterpret_cast<uint8_t*>(dbAddr + 0x14) = 1; // m_bTargetInRange = 1
                            int32_t curDbAnim = *reinterpret_cast<int32_t*>(dbAddr + 0x2c);
                            if (curDbAnim < 231 || curDbAnim > 238) {
                                *reinterpret_cast<int32_t*>(dbAddr + 0x2c) = seatRHS ? 236 /*DRIVEBY_RHS*/ : 232 /*DRIVEBY_LHS*/;
                            }
                            if (manualFirePressed) {
                                int8_t curSubState = *reinterpret_cast<int8_t*>(dbAddr + 0x17);
                                if (curSubState <= 2) {
                                    *reinterpret_cast<int8_t*>(dbAddr + 0x17) = 2;
                                    *reinterpret_cast<int16_t*>(dbAddr + 0x1e) = 0;
                                }
                                // Invocar AimGun + FireGun nativos de CTaskSimpleGangDriveBy con cadencia segura para garantizar disparo real
                                // aun si CCamera::Process revierte el modo de cámara del vehículo
                                void* dbWepInfo = *reinterpret_cast<void**>(dbAddr + 0x40);
                                if (dbWepInfo != nullptr) {
                                    if (gtaSymbols_.ctaskSimpleGangDriveByAimGunFn) {
                                        gtaSymbols_.ctaskSimpleGangDriveByAimGunFn(secAttackTask, localPed);
                                    }
                                    int64_t fireInterval = (me.weaponId == 28 || me.weaponId == 29 || me.weaponId == 32) ? 5 : 10;
                                    if (gtaSymbols_.ctaskSimpleGangDriveByFireGunFn &&
                                        (tickCount - lastPassengerFireTick_) >= fireInterval) {
                                        gtaSymbols_.ctaskSimpleGangDriveByFireGunFn(secAttackTask, localPed);
                                        lastPassengerFireTick_ = tickCount;
                                    }
                                }
                                me.animGroupId |= 0x06u;
                                me.isAiming = 1u;
                                pendingWeaponFireBroadcast_.store(true, std::memory_order_relaxed);
                            }
                        }
                        me.isDriveByActive = (secAttackTask != nullptr);
                    }
                } else {
                    localPassengerDriveByFiring_.store(false, std::memory_order_relaxed);
                    me.isDriveByActive = false;
                }
            } else {
                localPassengerDriveByFiring_.store(false, std::memory_order_relaxed);
                me.isDriveByActive = false;
            }
        } else if (!me.isAttachedOnRoof) {
            localPassengerDriveByFiring_.store(false, std::memory_order_relaxed);
            me.seatIndex = -1;
            me.isDriveByActive = false;
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

        // Interceptar muerte del jugador local (evitar pantalla de Wasted que resetea la partida y evitar crash en CPlayerInfo::Process 0x4edcfc)
        int32_t localPedState = *reinterpret_cast<int32_t*>(localPedAddr + 0x54c);
        if (rawHealth <= 2.0f || localPedState == 54 /*PEDSTATE_DIE*/ || localPedState == 55 /*PEDSTATE_DEAD*/) {
            if (myVeh != nullptr) {
                warpPedOutOfVehicle(localPed);
                myVeh = nullptr;
                me.seatIndex = -1;
                me.vehicleNetId = 0;
            }
            *reinterpret_cast<float*>(localPedAddr + 0x6ac) = 100.0f;
            *reinterpret_cast<float*>(localPedAddr + 0x6b0) = std::max(100.0f, *reinterpret_cast<float*>(localPedAddr + 0x6b0));
            // Conservar chaleco (armor) durante el Respawn Táctico (RF-08)
            float preservedArmor = std::isfinite(rawArmor) ? std::clamp(rawArmor, 0.0f, 200.0f) : 0.0f;
            *reinterpret_cast<float*>(localPedAddr + 0x6b4) = preservedArmor;
            me.armor = static_cast<int>(preservedArmor);
            *reinterpret_cast<int32_t*>(localPedAddr + 0x54c) = 1; // PEDSTATE_IDLE
            if (gtaSymbols_.cworldPlayers != nullptr) {
                // CPlayerInfo::m_nPlayerState en +0x118 = 0 (PLAYERSTATE_PLAYING)
                *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(gtaSymbols_.cworldPlayers) + 0x118) = 0;
            }
            uintptr_t localIntel = *reinterpret_cast<uintptr_t*>(localPedAddr + 0x538);
            if (localIntel != 0 && gtaSymbols_.ctaskManagerSetTaskFn) {
                void* taskMgr = reinterpret_cast<void*>(localIntel + 0x08);
                for (int ts = 0; ts <= 3; ++ts) {
                    if (*reinterpret_cast<void**>(localIntel + 0x08 + ts * 8) != nullptr) {
                        gtaSymbols_.ctaskManagerSetTaskFn(taskMgr, nullptr, ts, false);
                    }
                }
                if (*reinterpret_cast<void**>(localIntel + 0x28) == nullptr &&
                    gtaSymbols_.ctaskOperatorNewFn && gtaSymbols_.ctaskSimplePlayerOnFootCtorFn) {
                    void* defTask = gtaSymbols_.ctaskOperatorNewFn(0x70);
                    if (defTask) {
                        gtaSymbols_.ctaskSimplePlayerOnFootCtorFn(defTask);
                        gtaSymbols_.ctaskManagerSetTaskFn(taskMgr, defTask, 4, false);
                    }
                }
            }
            CVector respawnTarget{myPos.x + 2.5f, myPos.y + 2.5f, myPos.z + 0.6f};
            if (hasCustomRespawnPos_) {
                respawnTarget = customRespawnPos_;
            } else {
                for (int r = 0; r < kMaxCoopPlayers; ++r) {
                    if (r != localIdx && players_[r].isActive && players_[r].hasRealGtaCoords) {
                        respawnTarget = CVector{players_[r].x + 1.8f, players_[r].y + 1.8f, players_[r].z + 0.35f};
                        break;
                    }
                }
            }
            if (gtaSymbols_.cpedTeleportFn) {
                gtaSymbols_.cpedTeleportFn(localPed, respawnTarget, 0);
            }
            me.x = respawnTarget.x;
            me.y = respawnTarget.y;
            me.z = respawnTarget.z;
            me.health = 100;
            LOGI("Co-op Tactical Respawn prevented death/crash on localPed=%p -> (%.1f, %.1f, %.1f)",
                 localPed, respawnTarget.x, respawnTarget.y, respawnTarget.z);
        }

        // Protección permanente contra crash 0x4edcfc en CPlayerInfo::Process:
        // jamás permitir que los 5 slots primarios (0..4) del CTaskManager de localPed queden simultáneamente en nullptr
        {
            uintptr_t localIntel = *reinterpret_cast<uintptr_t*>(localPedAddr + 0x538);
            if (localIntel != 0) {
                bool hasAnyPrimaryTask = false;
                for (int ts = 0; ts <= 4; ++ts) {
                    if (*reinterpret_cast<void**>(localIntel + 0x08 + ts * 8) != nullptr) {
                        hasAnyPrimaryTask = true;
                        break;
                    }
                }
                if (!hasAnyPrimaryTask && gtaSymbols_.ctaskOperatorNewFn && gtaSymbols_.ctaskManagerSetTaskFn) {
                    if (gtaSymbols_.ctaskSimplePlayerOnFootCtorFn) {
                        void* defTask = gtaSymbols_.ctaskOperatorNewFn(0x70);
                        if (defTask) {
                            gtaSymbols_.ctaskSimplePlayerOnFootCtorFn(defTask);
                            gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(localIntel + 0x08), defTask, 4, false);
                        }
                    } else if (gtaSymbols_.ctaskSimpleStandStillCtorFn) {
                        void* defTask = gtaSymbols_.ctaskOperatorNewFn(0x28);
                        if (defTask) {
                            gtaSymbols_.ctaskSimpleStandStillCtorFn(defTask, 999999, true, false, 8.0f);
                            gtaSymbols_.ctaskManagerSetTaskFn(reinterpret_cast<void*>(localIntel + 0x08), defTask, 4, false);
                        }
                    }
                }
            }
        }

        // Publicar y aplicar la Skin elegida del jugador local usando la secuencia exacta de 09C7 (CHANGE_PLAYER_MODEL)
        // IMPORTANTE: jamás destruir/reconstruir el RpClump de localPed durante una misión activa ($ONMISSION != 0) ni en interiores
        // (m_nAreaCode != 0 en +0x4b), para no borrar animaciones de misión como "Big Smoke" ni tildar cámaras fijas
        me.skinModelId = config_.localSkinModelId;
        uint8_t localAreaCode = *reinterpret_cast<uint8_t*>(localPedAddr + 0x4b);
        if (!myVeh && !onMissionNowPre && localAreaCode == 0 && tickCount >= 60) {
            int16_t curLocalModel = *reinterpret_cast<int16_t*>(localPedAddr + 0x32);
            if (config_.localSkinModelId > 0) {
                bool localSkinLoaded = false;
                int32_t targetLocalModel = resolvePlayerSkinModelId(
                    config_.localSkinModelId, localIdx, (tickCount % 5 == 0), localSkinLoaded
                );
                if (targetLocalModel > 0 && localSkinLoaded &&
                    (curLocalModel != targetLocalModel || localAppliedSkinReqId_ != config_.localSkinModelId)) {
                    uintptr_t vtbl = *reinterpret_cast<uintptr_t*>(localPedAddr);
                    if (vtbl != 0) {
                        uint32_t savedAnimGroup = *reinterpret_cast<uint32_t*>(localPedAddr + 0x630);
                        using FnDeleteRwObject = void (*)(void*);
                        using FnVtblSetModelIndex = void (*)(void*, uint32_t);
                        auto deleteRwObjFn = *reinterpret_cast<FnDeleteRwObject*>(vtbl + 0x48);
                        auto setModelIdxFn = *reinterpret_cast<FnVtblSetModelIndex*>(vtbl + 0x30);
                        if (deleteRwObjFn && setModelIdxFn) {
                            deleteRwObjFn(localPed);
                            *reinterpret_cast<uint16_t*>(localPedAddr + 0x32) = 0xFFFF;
                            setModelIdxFn(localPed, static_cast<uint32_t>(targetLocalModel));
                            *reinterpret_cast<uint32_t*>(localPedAddr + 0x630) = savedAnimGroup;
                            localAppliedSkinModel_ = targetLocalModel;
                            localAppliedSkinReqId_ = config_.localSkinModelId;
                            LOGI("Applied 09C7 Local Player Skin on CPlayerPed=%p (reqSkin=%d -> model=%d)",
                                 localPed, config_.localSkinModelId, targetLocalModel);
                        }
                    }
                }
            } else if ((curLocalModel != 0 || localAppliedSkinReqId_ != 0) && gtaSymbols_.isModelLoaded(0)) {
                uintptr_t vtbl = *reinterpret_cast<uintptr_t*>(localPedAddr);
                if (vtbl != 0) {
                    uint32_t savedAnimGroup = *reinterpret_cast<uint32_t*>(localPedAddr + 0x630);
                    using FnDeleteRwObject = void (*)(void*);
                    using FnVtblSetModelIndex = void (*)(void*, uint32_t);
                    auto deleteRwObjFn = *reinterpret_cast<FnDeleteRwObject*>(vtbl + 0x48);
                    auto setModelIdxFn = *reinterpret_cast<FnVtblSetModelIndex*>(vtbl + 0x30);
                    if (deleteRwObjFn && setModelIdxFn) {
                        deleteRwObjFn(localPed);
                        *reinterpret_cast<uint16_t*>(localPedAddr + 0x32) = 0xFFFF;
                        setModelIdxFn(localPed, 0u);
                        *reinterpret_cast<uint32_t*>(localPedAddr + 0x630) = savedAnimGroup;
                        if (g_origCClothesRebuildPlayer) {
                            g_origCClothesRebuildPlayer(localPed, false);
                        }
                        localAppliedSkinModel_ = 0;
                        localAppliedSkinReqId_ = 0;
                    }
                }
            }
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
                    if (gtaSymbols_.ccheatMoneyArmourHealthCheatFn) {
                        gtaSymbols_.ccheatMoneyArmourHealthCheatFn();
                    } else if (gtaSymbols_.ccheatHealthCheatFn) {
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
                    bool invokedDedicated = false;
                    if (cheatCmd == "AIWPRTON" && gtaSymbols_.ccheatTankCheatFn) {
                        gtaSymbols_.ccheatTankCheatFn();
                        invokedDedicated = true;
                    } else if (cheatCmd == "JUMPJET" && gtaSymbols_.ccheatFlyboyCheatFn) {
                        gtaSymbols_.ccheatFlyboyCheatFn();
                        invokedDedicated = true;
                    } else if (cheatCmd == "OHDUDE" && gtaSymbols_.ccheatApacheCheatFn) {
                        gtaSymbols_.ccheatApacheCheatFn();
                        invokedDedicated = true;
                    } else if (cheatCmd == "PDNEJOH" && gtaSymbols_.ccheatStockCarCheatFn) {
                        gtaSymbols_.ccheatStockCarCheatFn();
                        invokedDedicated = true;
                    } else if (cheatCmd == "FOURWHEELFUN" && gtaSymbols_.ccheatQuadCheatFn) {
                        gtaSymbols_.ccheatQuadCheatFn();
                        invokedDedicated = true;
                    }
                    if (!invokedDedicated) {
                        if (gtaSymbols_.ccheatVehicleCheatFn) {
                            gtaSymbols_.ccheatVehicleCheatFn(vehModel);
                        } else if (gtaSymbols_.cstreamingRequestModelFn) {
                            // Carga asíncrona segura (RF-03 / RNF-01): jamás llamar LoadAllRequestedModels dentro de CWorld::Process
                            gtaSymbols_.cstreamingRequestModelFn(vehModel, 2);
                            pendingVehicleSpawnModel_ = vehModel;
                        }
                    }
                } else if (cheatCmd == "AFZLLQLL") {
                    if (gtaSymbols_.ccheatSunnyCheatFn) gtaSymbols_.ccheatSunnyCheatFn();
                    if (gtaSymbols_.oldWeatherType) *gtaSymbols_.oldWeatherType = 0;
                    if (gtaSymbols_.newWeatherType) *gtaSymbols_.newWeatherType = 0;
                    lastWorldCheatSynced_ = cheatCmd;
                    pendingWorldCheatBroadcast_.store(true, std::memory_order_relaxed);
                } else if (cheatCmd == "AUIFRVQS") {
                    if (gtaSymbols_.ccheatRainCheatFn) gtaSymbols_.ccheatRainCheatFn();
                    if (gtaSymbols_.oldWeatherType) *gtaSymbols_.oldWeatherType = 8;
                    if (gtaSymbols_.newWeatherType) *gtaSymbols_.newWeatherType = 8;
                    lastWorldCheatSynced_ = cheatCmd;
                    pendingWorldCheatBroadcast_.store(true, std::memory_order_relaxed);
                } else if (cheatCmd == "IOJUFZN") {
                    if (gtaSymbols_.ccheatRiotCheatFn) gtaSymbols_.ccheatRiotCheatFn();
                    lastWorldCheatSynced_ = cheatCmd;
                    pendingWorldCheatBroadcast_.store(true, std::memory_order_relaxed);
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
                // IMPORTANTE: Jamás poner *onAMissionFlagOffset = 0 aquí para no abortar la misión inicial de las BMX en uno de los teléfonos
                float targetX = 967.2f;
                float targetY = config_.isHost ? -1107.8f : -1105.9f;
                float targetZ = 23.98f;
                if (gtaSymbols_.cpedTeleportFn) {
                    gtaSymbols_.cpedTeleportFn(localPed, CVector{targetX, targetY, targetZ}, 0);
                }
                if (!config_.isHost && gtaSymbols_.ccarCtrlCreateCarForScriptFn && gtaSymbols_.isModelLoaded(481)) {
                    void* guestBmx = gtaSymbols_.ccarCtrlCreateCarForScriptFn(481, CVector{969.2f, -1105.9f, 23.98f}, 0);
                    if (guestBmx) {
                        *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(guestBmx) + 0x610) = 2; // MISSION_VEHICLE
                    }
                }
                me.x = targetX;
                me.y = targetY;
                me.z = targetZ;
                initialAlleyFreeRoamDone_ = true;
                initialSyncTeleportDone_ = config_.isHost;
                LOGI("Positioned %s CJ in open alley courtyard at (%.1f, %.1f, %.1f) preserving $ONMISSION=%d!",
                     config_.isHost ? "HOST" : "GUEST", targetX, targetY, targetZ, onMission ? 1 : 0);
            }
        }

        // Si el compañero acaba de activar el inicio de misión en la señal roja y estamos cerca (< 25m) pero a un paso del borde,
        // sincronizar nuestra posición exacta con la señal roja para que ambos entren juntos a la misión en el mismo frame
        if (pendingMissionMeetupTeleport_.exchange(false) && !onMission && !myVeh && gtaSymbols_.cpedTeleportFn) {
            float mdx = mission_.checkpointX - myPos.x;
            float mdy = mission_.checkpointY - myPos.y;
            float mDistSq = mdx * mdx + mdy * mdy;
            if (mDistSq > (0.5f * 0.5f) && mDistSq < (25.0f * 25.0f) && std::fabs(mission_.checkpointX) > 1.0f) {
                CVector mPos{mission_.checkpointX, mission_.checkpointY, mission_.checkpointZ};
                gtaSymbols_.cpedTeleportFn(localPed, mPos, 0);
                me.x = mPos.x;
                me.y = mPos.y;
                me.z = mPos.z;
                LOGI("Pulled local player into mission trigger at (%.1f, %.1f, %.1f) so both enter mission together!",
                     mPos.x, mPos.y, mPos.z);
            }
        }
        if (onMission && !lastLocalOnMission_) {
            mission_.onMissionFlag = 1;
            mission_.checkpointX = me.x;
            mission_.checkpointY = me.y;
            mission_.checkpointZ = me.z;
        }
        lastLocalOnMission_ = onMission;

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
                                for (int pIdx = 0; pIdx < kMaxCoopPlayers; ++pIdx) {
                                    if (!players_[pIdx].isActive || !players_[pIdx].hasRealGtaCoords) continue;
                                    float dx = px - players_[pIdx].x;
                                    float dy = py - players_[pIdx].y;
                                    float dz = pz - players_[pIdx].z;
                                    float candDSq = dx * dx + dy * dy + dz * dz;
                                    dSq = std::min(dSq, candDSq);
                                }
                                if (dSq > (130.0f * 130.0f)) continue;

                                // Bono fuerte de histéresis (0.15x distSq) para peatones ya sincronizados para evitar que desaparezcan al caminar/conducir junto a ellos
                                for (int prevK = 0; prevK < kMaxSyncedWorldPeds; ++prevK) {
                                    if (worldState_.peds[prevK].active && worldState_.peds[prevK].hostPoolSlot == static_cast<int16_t>(s)) {
                                        dSq *= 0.15f;
                                        break;
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

                                // Durante una misión activa ($ONMISSION != 0), no replicar vehículos de misión (m_nCreatedBy == 2)
                                // salvo que un jugador cooperativo esté conduciéndolo, para no desincronizar/abortar la misión del Invitado
                                uint8_t vCreatedBy = *reinterpret_cast<uint8_t*>(vAddr + 0x610);
                                if (onMission && vCreatedBy == 2 && pVeh != myVeh) {
                                    bool partnerInThisVeh = false;
                                    for (int r = 0; r < kMaxCoopPlayers; ++r) {
                                        if (r != localIdx && players_[r].isActive && players_[r].vehicleNetId == (1000 + s)) {
                                            partnerInThisVeh = true;
                                            break;
                                        }
                                    }
                                    if (!partnerInThisVeh) continue;
                                }

                                // Si un compañero remoto conduce este vehículo, aplicar primero su daño sincronizado sobre el vehículo del Host
                                for (int r = 0; r < kMaxCoopPlayers; ++r) {
                                    if (r != localIdx && players_[r].isActive && players_[r].seatIndex == 0 &&
                                        players_[r].vehicleNetId == (1000 + s)) {
                                        applyVehicleDamageState(
                                            pVeh, players_[r].vehicleHealth,
                                            players_[r].damageWheels, players_[r].damageDoorsLow,
                                            players_[r].damageDoorsHigh, players_[r].damageLights,
                                            players_[r].damagePanels
                                        );
                                        break;
                                    }
                                }

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
                                for (int pIdx = 0; pIdx < kMaxCoopPlayers; ++pIdx) {
                                    if (!players_[pIdx].isActive || !players_[pIdx].hasRealGtaCoords) continue;
                                    float dx = vxPos - players_[pIdx].x;
                                    float dy = vyPos - players_[pIdx].y;
                                    float dz = vzPos - players_[pIdx].z;
                                    float candDSq = dx * dx + dy * dy + dz * dz;
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

                                // Bono fuerte de histéresis (0.15x distSq) para vehículos ya sincronizados para evitar que desaparezcan o cambien de slot
                                for (int prevV = 0; prevV < kMaxSyncedWorldVehicles; ++prevV) {
                                    if (worldState_.vehicles[prevV].active && worldState_.vehicles[prevV].hostPoolSlot == static_cast<int16_t>(s)) {
                                        dSq *= 0.15f;
                                        break;
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
                                ve.vehicleHealth = vHealth;
                                ve.damageWheels = *reinterpret_cast<uint32_t*>(vAddr + 0x75c);
                                ve.damageDoorsLow = *reinterpret_cast<uint32_t*>(vAddr + 0x760);
                                ve.damageDoorsHigh = *reinterpret_cast<uint16_t*>(vAddr + 0x764);
                                ve.damageLights = *reinterpret_cast<uint32_t*>(vAddr + 0x768);
                                ve.damagePanels = *reinterpret_cast<uint32_t*>(vAddr + 0x76c);

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
            // Sincronizar hora del Host en el Guest SIN sobrescribir clockSeconds ni provocar micro-saltos de minuto:
            // Sobrescribir ms_nGameClockSeconds/Minutes cada frame dispara múltiples rollovers de minuto en CClock::Update()
            // y acelera CPlayerInfo::WorkOutEnergyFromHunger() / descenso de stats (Grasa, Músculo, Resistencia).
            // Solo ajustar si el desfase supera >= 2 minutos de juego.
            if (gtaSymbols_.clockHours && gtaSymbols_.clockMinutes) {
                int hostTotalMin = static_cast<int>(worldState_.clockHours) * 60 + static_cast<int>(worldState_.clockMinutes);
                int localTotalMin = static_cast<int>(*gtaSymbols_.clockHours) * 60 + static_cast<int>(*gtaSymbols_.clockMinutes);
                int diffMin = std::abs(hostTotalMin - localTotalMin);
                if (diffMin > 720) diffMin = 1440 - diffMin;
                if (diffMin >= 2) {
                    *gtaSymbols_.clockHours = worldState_.clockHours;
                    *gtaSymbols_.clockMinutes = worldState_.clockMinutes;
                }
            }
            if (gtaSymbols_.oldWeatherType) *gtaSymbols_.oldWeatherType = worldState_.oldWeather;
            if (gtaSymbols_.newWeatherType) *gtaSymbols_.newWeatherType = worldState_.newWeather;

            // Calcular distancia al Host: si el Host se alejó en un vehículo (> 55m), mantener tráfico local natural
            // en el Guest para que siga viendo bien todo su entorno
            float hostDx = players_[0].x - me.x;
            float hostDy = players_[0].y - me.y;
            float distToHostSq = players_[0].hasRealGtaCoords ? (hostDx * hostDx + hostDy * hostDy) : 0.0f;
            bool hostSeparatedInVeh = (distToHostSq > (55.0f * 55.0f));

            if (gtaSymbols_.dontCreateRandomCops) *gtaSymbols_.dontCreateRandomCops = 1;
            if (gtaSymbols_.dontCreateRandomGangMembers) *gtaSymbols_.dontCreateRandomGangMembers = 1;
            if (gtaSymbols_.pedDensityMultiplier) *gtaSymbols_.pedDensityMultiplier = 1.0f;
            if (gtaSymbols_.maxNumberOfPedsInUse) *gtaSymbols_.maxNumberOfPedsInUse = 20;
            if (gtaSymbols_.carDensityMultiplier) *gtaSymbols_.carDensityMultiplier = hostSeparatedInVeh ? 0.8f : 0.0f;
            if (gtaSymbols_.maxNumberOfCarsInUse) *gtaSymbols_.maxNumberOfCarsInUse = hostSeparatedInVeh ? 8 : 0;

            // IMPORTANTE: NUNCA invocar CStreaming::LoadAllRequestedModels desde CWorld::Process para evitar
            // condición de carrera con CdStreamThread (OS_FileSetPosition). CStreaming::Update carga los modelos
            // solicitados por RequestModel de forma asíncrona y segura en el siguiente frame.
            bool canRequestStreamingThisTick = (tickCount % 5 == 0);

            // 1. Instanciar y actualizar en el Guest los peatones reales sincronizados desde el Host (sólo si están a <= 110m del Guest
            //    para jamás instanciar MISSION_CHAR en sectores descargados del mapa que degradarían el streaming del entorno)
            for (int k = 0; k < kMaxSyncedWorldPeds; ++k) {
                const SyncedWorldPedEntry& wp = worldState_.peds[k];
                float wpDx = wp.x - me.x;
                float wpDy = wp.y - me.y;
                bool validPedEntry = (wp.active != 0 && wp.modelId > 0 && wp.modelId < 265 &&
                                      (wpDx * wpDx + wpDy * wpDy) <= (110.0f * 110.0f));
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
                        // Si el peatón está lejos (> 60m), descartarlo bajo tierra; si está a la vista, dejar que siga caminando naturalmente sin desaparecer de golpe
                        uintptr_t wMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(wPed) + 0x18);
                        if (wMat != 0 && gtaSymbols_.cpedTeleportFn) {
                            float pdx = *reinterpret_cast<float*>(wMat + 0x30) - me.x;
                            float pdy = *reinterpret_cast<float*>(wMat + 0x34) - me.y;
                            if ((pdx * pdx + pdy * pdy) > (60.0f * 60.0f)) {
                                gtaSymbols_.cpedTeleportFn(wPed, CVector{0.0f, 0.0f, -500.0f}, 0);
                            }
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
                                gtaSymbols_.cpedSetCharCreatedByFn(newWPed, 2 /*MISSION_CHAR*/);
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
                    uintptr_t wPedAddr = reinterpret_cast<uintptr_t>(wPed);
                    float localCurHp = *reinterpret_cast<float*>(wPedAddr + 0x6ac);
                    int32_t localPedSt = *reinterpret_cast<int32_t*>(wPedAddr + 0x54c);
                    // Preservar el daño real que hace el Invitado al disparar: jamás curar de vuelta a wp.health
                    // y liberar el peatón si el Invitado lo mató para que muera y caiga naturalmente
                    if (localCurHp <= 1.0f || localPedSt == 54 /*PEDSTATE_DIE*/ || localPedSt == 55 /*PEDSTATE_DEAD*/) {
                        if (gtaSymbols_.cpedSetCharCreatedByFn) {
                            gtaSymbols_.cpedSetCharCreatedByFn(wPed, 1 /*RANDOM_CHAR*/);
                        }
                        worldSyncPedPtr_[k] = nullptr;
                        worldSyncPedHandle_[k] = -1;
                        worldSyncPedHostSlot_[k] = -1;
                        worldSyncPedModel_[k] = -1;
                        continue;
                    }
                    float effectiveHp = std::min(localCurHp, static_cast<float>(wp.health));
                    if (wp.moveState >= 4 && (nowMs - worldState_.lastNetworkUpdateMs) < 120) {
                        worldState_.peds[k].x += wp.vx * 0.5f;
                        worldState_.peds[k].y += wp.vy * 0.5f;
                    }
                    applySmoothPedTransformAndAnim(
                        wPed, worldState_.peds[k].x, worldState_.peds[k].y, wp.z,
                        wp.vx, wp.vy, wp.vz, wp.heading,
                        static_cast<int>(wp.moveState), effectiveHp
                    );
                }
            }

            // 2. Instanciar y actualizar en el Guest los vehículos sincronizados desde el Host (sólo si están a <= 115m del Guest
            //    para no poblar sectores lejanos descargados cuando el compañero se aleja en otro vehículo)
            for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                const SyncedWorldVehicleEntry& ve = worldState_.vehicles[v];
                float veDx = ve.x - me.x;
                float veDy = ve.y - me.y;
                bool validVehEntry = (ve.active != 0 && ve.modelId >= 400 && ve.modelId <= 611 &&
                                      (veDx * veDx + veDy * veDy) <= (115.0f * 115.0f));
                if (validVehEntry && (worldSyncVehPtr_[v] == nullptr || worldSyncVehHostSlot_[v] != ve.hostPoolSlot)) {
                    for (int otherV = 0; otherV < kMaxSyncedWorldVehicles; ++otherV) {
                        if (otherV != v && worldSyncVehPtr_[otherV] != nullptr &&
                            worldSyncVehHostSlot_[otherV] == ve.hostPoolSlot) {
                            std::swap(worldSyncVehPtr_[v], worldSyncVehPtr_[otherV]);
                            std::swap(worldSyncVehHandle_[v], worldSyncVehHandle_[otherV]);
                            std::swap(worldSyncVehHostSlot_[v], worldSyncVehHostSlot_[otherV]);
                            std::swap(worldSyncVehModel_[v], worldSyncVehModel_[otherV]);
                            std::swap(worldSyncVehDriverPtr_[v], worldSyncVehDriverPtr_[otherV]);
                            std::swap(worldSyncVehDriverHandle_[v], worldSyncVehDriverHandle_[otherV]);
                            std::swap(worldSyncVehHadDriver_[v], worldSyncVehHadDriver_[otherV]);
                            std::swap(worldSyncVehSpawnedByUs_[v], worldSyncVehSpawnedByUs_[otherV]);
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
                        worldSyncVehHostSlot_[v] = -1;
                        worldSyncVehModel_[v] = -1;
                        worldSyncVehDriverPtr_[v] = nullptr;
                        worldSyncVehDriverHandle_[v] = -1;
                        worldSyncVehHadDriver_[v] = false;
                    }
                }

                // Si el slot v contiene actualmente myVeh pero ve corresponde a OTRO vehículo distinto,
                // jamás sobrescribir el hostPoolSlot ni el color de myVeh con otro vehículo: mover myVeh a un slot libre o saltar
                if (wVeh && wVeh == myVeh && (!validVehEntry || worldSyncVehModel_[v] != ve.modelId ||
                    (worldSyncVehHostSlot_[v] >= 0 && worldSyncVehHostSlot_[v] != ve.hostPoolSlot))) {
                    int movedTo = -1;
                    for (int freeV = v + 1; freeV < kMaxSyncedWorldVehicles; ++freeV) {
                        if (worldSyncVehPtr_[freeV] == nullptr) {
                            std::swap(worldSyncVehPtr_[v], worldSyncVehPtr_[freeV]);
                            std::swap(worldSyncVehHandle_[v], worldSyncVehHandle_[freeV]);
                            std::swap(worldSyncVehHostSlot_[v], worldSyncVehHostSlot_[freeV]);
                            std::swap(worldSyncVehModel_[v], worldSyncVehModel_[freeV]);
                            std::swap(worldSyncVehDriverPtr_[v], worldSyncVehDriverPtr_[freeV]);
                            std::swap(worldSyncVehDriverHandle_[v], worldSyncVehDriverHandle_[freeV]);
                            std::swap(worldSyncVehHadDriver_[v], worldSyncVehHadDriver_[freeV]);
                            std::swap(worldSyncVehSpawnedByUs_[v], worldSyncVehSpawnedByUs_[freeV]);
                            movedTo = freeV;
                            break;
                        }
                    }
                    if (movedTo >= 0) {
                        wVeh = nullptr;
                    } else {
                        continue;
                    }
                }

                bool exactVehModelLoaded = validVehEntry && gtaSymbols_.isModelLoaded(ve.modelId);
                if (validVehEntry && !exactVehModelLoaded && canRequestStreamingThisTick && gtaSymbols_.cstreamingRequestModelFn) {
                    gtaSymbols_.cstreamingRequestModelFn(ve.modelId, 2);
                }

                if (!validVehEntry || (wVeh && (worldSyncVehModel_[v] != ve.modelId || worldSyncVehHostSlot_[v] != ve.hostPoolSlot) && wVeh != myVeh)) {
                    if (wVeh && wVeh != myVeh && wVeh != localRoofVehPtr_) {
                        uintptr_t wvMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(wVeh) + 0x18);
                        float vdx = wvMat ? (*reinterpret_cast<float*>(wvMat + 0x30) - me.x) : 999.0f;
                        float vdy = wvMat ? (*reinterpret_cast<float*>(wvMat + 0x34) - me.y) : 999.0f;
                        // Si el vehículo que spawneamos ya quedó lejos (> 50m), evictarlo; si sigue en pantalla (<= 50m),
                        // liberarlo como RANDOM_VEHICLE para que no desaparezca bruscamente cuando el compañero se aleja en auto
                        if (worldSyncVehSpawnedByUs_[v] && (vdx * vdx + vdy * vdy) > (50.0f * 50.0f)) {
                            if (worldSyncVehDriverPtr_[v] && gtaSymbols_.cpoolsGetPedFn && worldSyncVehDriverHandle_[v] > 0 &&
                                gtaSymbols_.cpoolsGetPedFn(worldSyncVehDriverHandle_[v]) == worldSyncVehDriverPtr_[v]) {
                                evictNpcPedFromWorld(worldSyncVehDriverPtr_[v], localPed);
                            }
                            evictVehicleFromWorld(wVeh, localPed);
                        } else if (worldSyncVehSpawnedByUs_[v]) {
                            *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(wVeh) + 0x610) = 1; // RANDOM_VEHICLE
                            if (worldSyncVehDriverPtr_[v] && gtaSymbols_.cpedSetCharCreatedByFn) {
                                gtaSymbols_.cpedSetCharCreatedByFn(worldSyncVehDriverPtr_[v], 1 /*RANDOM_CHAR*/);
                            }
                        }
                        wVeh = nullptr;
                        worldSyncVehPtr_[v] = nullptr;
                        worldSyncVehHandle_[v] = -1;
                        worldSyncVehHostSlot_[v] = -1;
                        worldSyncVehModel_[v] = -1;
                        worldSyncVehDriverPtr_[v] = nullptr;
                        worldSyncVehDriverHandle_[v] = -1;
                        worldSyncVehHadDriver_[v] = false;
                        worldSyncVehSpawnedByUs_[v] = false;
                    }
                    if (!validVehEntry) continue;
                }

                if (!wVeh) {
                    if (!exactVehModelLoaded) continue;
                    // Primero verificar si ya existe en el mundo local un vehículo del MISMO modelo cerca (incluyendo myVeh)
                    // y adoptarlo en lugar de spawnear un segundo vehículo encima que haría explotar al primero.
                    // IMPORTANTE: jamás adoptar un MISSION_VEHICLE (+0x610 == 2) ajeno (como la BMX de la misión) para nunca alterarlo ni destruirlo.
                    if (gtaSymbols_.cpoolsVehiclePool) {
                        void* vPoolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsVehiclePool);
                        if (vPoolPtr) {
                            uintptr_t vPoolAddr = reinterpret_cast<uintptr_t>(vPoolPtr);
                            uint8_t* vObjects = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x00);
                            uint8_t* vByteMap = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x08);
                            int32_t vPoolSize = *reinterpret_cast<int32_t*>(vPoolAddr + 0x10);
                            if (vObjects && vByteMap && vPoolSize > 0 && vPoolSize <= 256) {
                                void* adoptableVeh = nullptr;
                                float bestAdoptDistSq = 22.0f * 22.0f;
                                for (int32_t s = 0; s < vPoolSize; ++s) {
                                    if ((vByteMap[s] & 0x80) != 0) continue;
                                    void* candVeh = reinterpret_cast<void*>(vObjects + static_cast<size_t>(s) * 0xc68);
                                    bool alreadyManaged = false;
                                    for (int sv = 0; sv < kMaxSyncedWorldVehicles; ++sv) {
                                        if (candVeh == worldSyncVehPtr_[sv]) {
                                            alreadyManaged = true;
                                            break;
                                        }
                                    }
                                    if (alreadyManaged) continue;
                                    uintptr_t cvAddr = reinterpret_cast<uintptr_t>(candVeh);
                                    uint8_t cvCreatedBy = *reinterpret_cast<uint8_t*>(cvAddr + 0x610);
                                    if (cvCreatedBy == 2 && candVeh != myVeh) continue; // Proteger vehículos de misión (ej. BMX)
                                    int16_t cvModel = *reinterpret_cast<int16_t*>(cvAddr + 0x32);
                                    uintptr_t cvMat = *reinterpret_cast<uintptr_t*>(cvAddr + 0x18);
                                    if (cvMat != 0 && cvModel == ve.modelId) {
                                        float cdx = *reinterpret_cast<float*>(cvMat + 0x30) - ve.x;
                                        float cdy = *reinterpret_cast<float*>(cvMat + 0x34) - ve.y;
                                        float dSq = cdx * cdx + cdy * cdy;
                                        if (dSq < bestAdoptDistSq) {
                                            bestAdoptDistSq = dSq;
                                            adoptableVeh = candVeh;
                                        }
                                    }
                                }
                                if (adoptableVeh != nullptr) {
                                    worldSyncVehPtr_[v] = adoptableVeh;
                                    worldSyncVehHandle_[v] = gtaSymbols_.cpoolsGetVehicleRefFn ? gtaSymbols_.cpoolsGetVehicleRefFn(adoptableVeh) : -1;
                                    worldSyncVehHostSlot_[v] = ve.hostPoolSlot;
                                    worldSyncVehModel_[v] = ve.modelId;
                                    worldSyncVehDriverPtr_[v] = nullptr;
                                    worldSyncVehDriverHandle_[v] = -1;
                                    worldSyncVehHadDriver_[v] = (adoptableVeh == myVeh);
                                    worldSyncVehSpawnedByUs_[v] = false;
                                    wVeh = adoptableVeh;
                                }
                            }
                        }
                    }

                    if (!wVeh) {
                        bool occupiedByCoopPartner = false;
                        for (int r = 0; r < kMaxCoopPlayers; ++r) {
                            if (r != localIdx && players_[r].isActive && players_[r].vehicleNetId == (1000 + ve.hostPoolSlot)) {
                                occupiedByCoopPartner = true;
                                break;
                            }
                        }
                        // Para vehículos NPC (no conducidos por un compañero): evitar spawnear autos justo delante si vamos conduciendo,
                        // pero permitir spawnear vehículos estacionados o tráfico cercano cuando el jugador va a pie (overlapsAnyExistingVeh ya protege 6m)
                        if (!occupiedByCoopPartner) {
                            if ((tickCount - lastGuestVehSpawnTick_) < 20) continue;
                            float vdx = ve.x - me.x;
                            float vdy = ve.y - me.y;
                            float vDistSq = vdx * vdx + vdy * vdy;
                            float mySpdSq = me.vx * me.vx + me.vy * me.vy;
                            if (myVeh != nullptr) {
                                if (vDistSq < (14.0f * 14.0f)) continue;
                                if (mySpdSq > (0.04f * 0.04f)) {
                                    float dotFwd = vdx * me.vx + vdy * me.vy;
                                    if (dotFwd > 0.0f && vDistSq < (32.0f * 32.0f)) continue;
                                }
                            } else {
                                if (vDistSq < (7.5f * 7.5f)) continue;
                            }
                        }
                        // Jamás spawnear un vehículo encima de otro vehículo ya existente en el pool
                        bool overlapsAnyExistingVeh = false;
                        if (gtaSymbols_.cpoolsVehiclePool) {
                            void* vPoolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsVehiclePool);
                            if (vPoolPtr) {
                                uintptr_t vPoolAddr = reinterpret_cast<uintptr_t>(vPoolPtr);
                                uint8_t* vObjects = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x00);
                                uint8_t* vByteMap = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x08);
                                int32_t vPoolSize = *reinterpret_cast<int32_t*>(vPoolAddr + 0x10);
                                bool isTwoWheeler = (ve.modelId == 481 || ve.modelId == 509 || ve.modelId == 510 ||
                                                     ve.modelId == 448 || ve.modelId == 461 || ve.modelId == 462 ||
                                                     ve.modelId == 463 || ve.modelId == 468 || ve.modelId == 471 ||
                                                     ve.modelId == 521 || ve.modelId == 522 || ve.modelId == 523 ||
                                                     ve.modelId == 581 || ve.modelId == 586);
                                float minClearance = isTwoWheeler ? 1.8f : 6.0f;
                                if (vObjects && vByteMap && vPoolSize > 0 && vPoolSize <= 256) {
                                    for (int32_t s = 0; s < vPoolSize; ++s) {
                                        if ((vByteMap[s] & 0x80) != 0) continue;
                                        void* candVeh = reinterpret_cast<void*>(vObjects + static_cast<size_t>(s) * 0xc68);
                                        uintptr_t cvMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(candVeh) + 0x18);
                                        if (cvMat == 0) continue;
                                        float mdx = *reinterpret_cast<float*>(cvMat + 0x30) - ve.x;
                                        float mdy = *reinterpret_cast<float*>(cvMat + 0x34) - ve.y;
                                        if ((mdx * mdx + mdy * mdy) < (minClearance * minClearance)) {
                                            overlapsAnyExistingVeh = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (overlapsAnyExistingVeh) continue;

                        int16_t spawnVehModel = ve.modelId;
                        if (gtaSymbols_.ccarCtrlCreateCarForScriptFn) {
                            void* newVeh = gtaSymbols_.ccarCtrlCreateCarForScriptFn(spawnVehModel, CVector{ve.x, ve.y, ve.z}, 0);
                            if (newVeh) {
                                lastGuestVehSpawnTick_ = tickCount;
                                uintptr_t newVehAddr = reinterpret_cast<uintptr_t>(newVeh);
                                if (ve.packedColors != 0) {
                                    *reinterpret_cast<uint32_t*>(newVehAddr + 0x574) = ve.packedColors;
                                }
                                *reinterpret_cast<uint8_t*>(newVehAddr + 0x610) = 2; // MISSION_VEHICLE
                                applyVehicleDamageState(
                                    newVeh, ve.vehicleHealth, ve.damageWheels,
                                    ve.damageDoorsLow, ve.damageDoorsHigh, ve.damageLights, ve.damagePanels
                                );
                                worldSyncVehPtr_[v] = newVeh;
                                worldSyncVehHandle_[v] = gtaSymbols_.cpoolsGetVehicleRefFn ? gtaSymbols_.cpoolsGetVehicleRefFn(newVeh) : -1;
                                worldSyncVehHostSlot_[v] = ve.hostPoolSlot;
                                worldSyncVehModel_[v] = spawnVehModel;
                                worldSyncVehDriverPtr_[v] = nullptr;
                                worldSyncVehDriverHandle_[v] = -1;
                                worldSyncVehHadDriver_[v] = false;
                                worldSyncVehSpawnedByUs_[v] = true;
                                wVeh = newVeh;
                            }
                        }
                    }
                }

                if (wVeh) {
                    uintptr_t wVehAddr = reinterpret_cast<uintptr_t>(wVeh);
                    int16_t actualWModel = *reinterpret_cast<int16_t*>(wVehAddr + 0x32);
                    if (actualWModel != ve.modelId) {
                        continue; // Protección estricta: nunca aplicar estado de un modelo distinto
                    }
                    worldSyncVehHostSlot_[v] = ve.hostPoolSlot;
                    worldSyncVehModel_[v] = actualWModel;
                    *reinterpret_cast<uint8_t*>(wVehAddr + 0x610) = 2; // MISSION_VEHICLE
                    if (wVeh != myVeh || me.seatIndex > 0) {
                        float localVehHp = *reinterpret_cast<float*>(wVehAddr + 0x634);
                        float effectiveVehHp = (std::isfinite(localVehHp) && localVehHp > 0.0f)
                            ? std::min(localVehHp, ve.vehicleHealth)
                            : ve.vehicleHealth;
                        applyVehicleDamageState(
                            wVeh, effectiveVehHp, ve.damageWheels,
                            ve.damageDoorsLow, ve.damageDoorsHigh, ve.damageLights, ve.damagePanels
                        );
                    }
                    if (wVeh != myVeh && ve.packedColors != 0) {
                        *reinterpret_cast<uint32_t*>(wVehAddr + 0x574) = ve.packedColors;
                    }

                    uintptr_t wvMat = *reinterpret_cast<uintptr_t*>(wVehAddr + 0x18);
                    float wvx = wvMat ? *reinterpret_cast<float*>(wvMat + 0x30) : ve.x;
                    float wvy = wvMat ? *reinterpret_cast<float*>(wvMat + 0x34) : ve.y;
                    float dxLoc = wvx - myPos.x;
                    float dyLoc = wvy - myPos.y;
                    float localDistSq = dxLoc * dxLoc + dyLoc * dyLoc;
                    bool localCarjackingThisVeh = (localEnteringAsDriver && localDistSq < (8.5f * 8.5f));
                    bool localEnteringThisAsPass = (localAlreadyEnteringAsPassenger && localDistSq < (9.5f * 9.5f));

                    // Garantizar que todo vehículo NPC tenga a lo sumo 1 conductor instanciado (nunca re-spawnear tras ser bajado)
                    if (wVeh != myVeh) {
                        bool remDrivingThisVeh = false;
                        for (int r = 0; r < kMaxCoopPlayers; ++r) {
                            if (r != localIdx && players_[r].isActive && players_[r].seatIndex == 0) {
                                if (players_[r].vehicleNetId == (1000 + ve.hostPoolSlot)) {
                                    remDrivingThisVeh = true;
                                    break;
                                }
                                if (players_[r].vehicleModelId == ve.modelId) {
                                    float rdx = players_[r].x - wvx;
                                    float rdy = players_[r].y - wvy;
                                    if ((rdx * rdx + rdy * rdy) < (12.0f * 12.0f)) {
                                        remDrivingThisVeh = true;
                                        break;
                                    }
                                }
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
                            // Jamás expulsar/eliminar un personaje de misión (m_nCreatedBy == 2) salvo que sea el conductor NPC
                            // que nosotros mismos creamos para tráfico sincronizado (worldSyncVehDriverPtr_[v])
                            uint8_t curDrvCreatedBy = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(curDrv) + 0x548);
                            bool isExternalMissionChar = (curDrvCreatedBy == 2 && curDrv != worldSyncVehDriverPtr_[v]);
                            if (!isRemPlayer && !isExternalMissionChar) {
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
                    // aplicar la posición sincronizada del Host/conductor sin relinkear CWorld si localPed está subiendo o viajando dentro
                    if ((wVeh != myVeh && !localCarjackingThisVeh) || me.seatIndex > 0) {
                        bool isLocalInOrEntering = (wVeh == myVeh) || localEnteringThisAsPass || (wVeh == localRoofVehPtr_);
                        applySmoothVehicleTransform(wVeh, ve, isLocalInOrEntering, worldState_.lastNetworkUpdateMs);
                        if (wVeh == myVeh && me.seatIndex > 0 && gtaSymbols_.cpedSetPedPositionInCarFn) {
                            gtaSymbols_.cpedSetPedPositionInCarFn(localPed);
                        }
                    }
                }
            }

            // Limpiar cada 15 frames únicamente la zona subterránea de descarte (-600..-400Z);
            // jamás llamar RemoveAllRandomPeds ni ClearCarsFromArea alrededor del jugador para que no desaparezcan peatones ni vehículos
            if (tickCount % 15 == 0) {
                if (gtaSymbols_.cworldClearCarsFromAreaFn) {
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
            if (!peerRecentlyActive) {
                // RNF-03: Limpieza automática de entidades remotas y blips tras 20 s sin reporte (y reenganche automático al volver)
                if (remoteBlipHandle_[i] > 0 && gtaSymbols_.cradarClearBlipFn) {
                    gtaSymbols_.cradarClearBlipFn(remoteBlipHandle_[i]);
                }
                remoteBlipHandle_[i] = -1;
                remoteBlipColor_[i] = -1;
                if (remotePedPtr_[i] != nullptr) {
                    if (!gtaSymbols_.cpoolsGetPedFn || remotePedHandle_[i] <= 0 ||
                        gtaSymbols_.cpoolsGetPedFn(remotePedHandle_[i]) == remotePedPtr_[i]) {
                        evictNpcPedFromWorld(remotePedPtr_[i], localPed);
                    }
                    remotePedPtr_[i] = nullptr;
                    remotePedHandle_[i] = -1;
                    remotePedSpawnedModel_[i] = 0;
                    remotePedAppliedSkinId_[i] = 0;
                    remotePedEquippedWeapon_[i] = -1;
                }
                if (rem.lastNetworkUpdateMs > 0) {
                    rem.isActive = false;
                }
                continue;
            }
            if (!rem.hasRealGtaCoords) continue;

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
                bool remSkinLoaded = false;
                int32_t desiredRemModel = resolvePlayerSkinModelId(rem.skinModelId, i, true, remSkinLoaded);
                if (desiredRemModel > 0 && remSkinLoaded) {
                    spawnModel = static_cast<uint32_t>(desiredRemModel);
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
                        // En GTA SA arm64 v2.10: cuando friendly = 0, CRadar::Draw3dMarkers (cono 3D sobre la cabeza)
                        // usa desiredSlotColor (0..6). Usamos BLIP_DISPLAY_MARKER_ONLY (1) cuando DrawRotatingRadarSprite
                        // está activo para que en el minimapa se dibuje únicamente la "gota" (RADAR_SPRITE_CENTRE) coloreada.
                        uint32_t slotColor = desiredSlotColor;
                        uint8_t isFriendly = 0u;
                        int blipDisplayMode = (gtaSymbols_.cradarDrawRotatingRadarSpriteFn && gtaSymbols_.cradarBlipSprites)
                            ? 1 /*BLIP_DISPLAY_MARKER_ONLY*/
                            : 3 /*BLIP_DISPLAY_BOTH*/;
                        remoteBlipHandle_[i] = gtaSymbols_.cradarSetEntityBlipFn(
                            2 /*BLIP_CHAR*/, handle, slotColor, blipDisplayMode, nullptr
                        );
                        if (remoteBlipHandle_[i] > 0) {
                            if (gtaSymbols_.cradarChangeBlipDisplayFn) {
                                gtaSymbols_.cradarChangeBlipDisplayFn(remoteBlipHandle_[i], blipDisplayMode);
                            }
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

                // Sincronizar modelo de skin del compañero remoto (soportando modelos normales y personajes especiales de historia)
                // usando la secuencia segura DeleteRwObject + m_nModelIndex=0xFFFF + SetModelIndex para que el mesh 3D cambie siempre
                if (rem.skinModelId > 0 && !remCurrentlyInVeh) {
                    bool remSkinLoaded = false;
                    int32_t targetRemModel = resolvePlayerSkinModelId(rem.skinModelId, i, (tickCount % 5 == 0), remSkinLoaded);
                    if (targetRemModel > 0 && remSkinLoaded &&
                        (remotePedSpawnedModel_[i] != targetRemModel || remotePedAppliedSkinId_[i] != rem.skinModelId)) {
                        uintptr_t rVtbl = *reinterpret_cast<uintptr_t*>(remPedAddr);
                        if (rVtbl != 0) {
                            uint32_t savedRemAnimGrp = *reinterpret_cast<uint32_t*>(remPedAddr + 0x630);
                            using FnDeleteRwObject = void (*)(void*);
                            using FnVtblSetModelIndex = void (*)(void*, uint32_t);
                            auto deleteRwObjFn = *reinterpret_cast<FnDeleteRwObject*>(rVtbl + 0x48);
                            auto setModelIdxFn = *reinterpret_cast<FnVtblSetModelIndex*>(rVtbl + 0x30);
                            if (deleteRwObjFn && setModelIdxFn) {
                                deleteRwObjFn(ped);
                                *reinterpret_cast<uint16_t*>(remPedAddr + 0x32) = 0xFFFF;
                                setModelIdxFn(ped, static_cast<uint32_t>(targetRemModel));
                                *reinterpret_cast<uint32_t*>(remPedAddr + 0x630) = savedRemAnimGrp;
                                remotePedSpawnedModel_[i] = targetRemModel;
                                remotePedAppliedSkinId_[i] = rem.skinModelId;
                                remotePedEquippedWeapon_[i] = -1; // Re-equipar arma tras cambio de modelo 3D
                                LOGI("Updated 3D Remote Player P%d Skin (ped=%p, reqSkin=%d -> model=%d, isModelLoaded=1)",
                                     i + 1, ped, rem.skinModelId, targetRemModel);
                            }
                        }
                    }
                }

                // Sincronizar arma equipada en la mano 3D del compañero remoto (tanto a pie como asomado por la ventana como pasajero)
                if (!remCurrentlyInVeh || rem.seatIndex > 0) {
                    int32_t desiredWep = std::clamp(rem.weaponId, 0, 46);
                    if (rem.seatIndex > 0 && rem.isDriveByActive && (desiredWep < 22 || desiredWep > 34)) {
                        desiredWep = 28; // Micro Uzi por defecto si el pasajero está en modo Drive-By
                    }
                    if (remotePedEquippedWeapon_[i] != desiredWep) {
                        if (desiredWep == 0) {
                            if (gtaSymbols_.cpedSetCurrentWeaponFn) {
                                gtaSymbols_.cpedSetCurrentWeaponFn(ped, 0);
                            }
                            remotePedEquippedWeapon_[i] = 0;
                        } else if (gtaSymbols_.cweaponInfoGetWeaponInfoFn && gtaSymbols_.cpedGiveWeaponFn && gtaSymbols_.cpedSetCurrentWeaponFn) {
                            void* wepInfo = gtaSymbols_.cweaponInfoGetWeaponInfoFn(desiredWep, 1);
                            int32_t wepModelId = wepInfo ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(wepInfo) + 0x0c) : -1;
                            if (wepModelId > 0 && !gtaSymbols_.isModelLoaded(wepModelId)) {
                                if ((tickCount % 5 == 0) && gtaSymbols_.cstreamingRequestModelFn) {
                                    gtaSymbols_.cstreamingRequestModelFn(wepModelId, 2);
                                }
                            } else {
                                gtaSymbols_.cpedGiveWeaponFn(ped, desiredWep, 9999, true);
                                gtaSymbols_.cpedSetCurrentWeaponFn(ped, desiredWep);
                                if (wepModelId > 0 && *reinterpret_cast<void**>(remPedAddr + 0x650) == nullptr &&
                                    gtaSymbols_.cpedAddWeaponModelFn) {
                                    gtaSymbols_.cpedAddWeaponModelFn(ped, wepModelId);
                                }
                                remotePedEquippedWeapon_[i] = desiredWep;
                            }
                        }
                    }
                }

                float remDistToMeSq = (targetRemX - me.x) * (targetRemX - me.x) +
                                      (targetRemY - me.y) * (targetRemY - me.y);

                // Si el compañero remoto está en un sector del mapa muy lejano (> 150m), NO mantener su vehículo anclado
                // como MISSION_VEHICLE para no saturar ni desincronizar el CStreaming del entorno local
                if (remDistToMeSq > (150.0f * 150.0f)) {
                    if (remCurrentlyInVeh) {
                        warpPedOutOfVehicle(ped);
                        remCurrentlyInVeh = false;
                        remCurrentVeh = nullptr;
                    }
                    if (remotePartnerVehPtr_[i] != nullptr) {
                        if (gtaSymbols_.cpoolsGetVehicleFn && remotePartnerVehHandle_[i] > 0 &&
                            gtaSymbols_.cpoolsGetVehicleFn(remotePartnerVehHandle_[i]) == remotePartnerVehPtr_[i]) {
                            if (remotePartnerVehPtr_[i] != myVeh) {
                                evictVehicleFromWorld(remotePartnerVehPtr_[i], localPed);
                            }
                        }
                        remotePartnerVehPtr_[i] = nullptr;
                        remotePartnerVehHandle_[i] = -1;
                    }
                    applySmoothPedTransformAndAnim(
                        ped, targetRemX, targetRemY, targetRemZ,
                        rem.vx, rem.vy, rem.vz, rem.heading,
                        static_cast<int>(rem.animId), 200.0f, rem.lastNetworkUpdateMs,
                        rem.isCrouching != 0
                    );
                    continue;
                }

                if (rem.seatIndex >= 0) {
                    // Buscar el CVehicle compartido en el mundo local donde viaja el compañero remoto
                    // verificando SIEMPRE que el modelo 3D coincida con rem.vehicleModelId (nunca poner en bicicleta si va en ambulancia)
                    void* targetVeh = nullptr;
                    int wantedHostSlot = (rem.vehicleNetId >= 1000) ? (rem.vehicleNetId - 1000) : -1;
                    int16_t expectedVehModel = (rem.vehicleModelId >= 400 && rem.vehicleModelId <= 611)
                        ? static_cast<int16_t>(rem.vehicleModelId)
                        : -1;

                    if (!config_.isHost && wantedHostSlot >= 0) {
                        for (int v = 0; v < kMaxSyncedWorldVehicles; ++v) {
                            if (worldSyncVehPtr_[v] != nullptr && worldSyncVehHostSlot_[v] == wantedHostSlot) {
                                int16_t candModel = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(worldSyncVehPtr_[v]) + 0x32);
                                if (expectedVehModel < 0 || candModel == expectedVehModel) {
                                    targetVeh = worldSyncVehPtr_[v];
                                }
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
                                void* cand = reinterpret_cast<void*>(vObjects + static_cast<size_t>(wantedHostSlot) * 0xc68);
                                int16_t candModel = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(cand) + 0x32);
                                if (expectedVehModel < 0 || candModel == expectedVehModel) {
                                    targetVeh = cand;
                                }
                            }
                        }
                    }

                    // Sólo asociar myVeh si el compañero es pasajero nuestro (rem.seatIndex > 0) o nosotros somos pasajeros suyos (me.seatIndex > 0);
                    // jamás secuestrar myVeh si ambos vamos como conductores de vehículos independientes (rem.seatIndex == 0 && me.seatIndex == 0)
                    if (!targetVeh && myVeh && (rem.seatIndex > 0 || me.seatIndex > 0)) {
                        int16_t myVehModel = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(myVeh) + 0x32);
                        if (expectedVehModel < 0 || myVehModel == expectedVehModel) {
                            uintptr_t myVehMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(myVeh) + 0x18);
                            float vx = myVehMat ? *reinterpret_cast<float*>(myVehMat + 0x30) : me.x;
                            float vy = myVehMat ? *reinterpret_cast<float*>(myVehMat + 0x34) : me.y;
                            float dx = vx - targetRemX;
                            float dy = vy - targetRemY;
                            if ((dx * dx + dy * dy) < (25.0f * 25.0f) ||
                                (rem.vehicleNetId > 0 && rem.vehicleNetId == me.vehicleNetId)) {
                                targetVeh = myVeh;
                            }
                        }
                    }

                    // Reutilizar primero el vehículo ya instanciado para este compañero si sigue válido en el pool y coincide el modelo
                    if (!targetVeh && remotePartnerVehPtr_[i] != nullptr && gtaSymbols_.cpoolsGetVehicleFn && remotePartnerVehHandle_[i] > 0) {
                        if (gtaSymbols_.cpoolsGetVehicleFn(remotePartnerVehHandle_[i]) == remotePartnerVehPtr_[i]) {
                            int16_t savedModel = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(remotePartnerVehPtr_[i]) + 0x32);
                            if (expectedVehModel < 0 || savedModel == expectedVehModel) {
                                targetVeh = remotePartnerVehPtr_[i];
                            }
                        } else {
                            remotePartnerVehPtr_[i] = nullptr;
                            remotePartnerVehHandle_[i] = -1;
                        }
                    }

                    // Buscar en CPools::ms_pVehiclePool el vehículo más cercano con el mismo modelo (sin robar myVeh cuando ambos conducen)
                    if (!targetVeh && gtaSymbols_.cpoolsVehiclePool) {
                        void* vPoolPtr = *reinterpret_cast<void**>(gtaSymbols_.cpoolsVehiclePool);
                        if (vPoolPtr) {
                            uintptr_t vPoolAddr = reinterpret_cast<uintptr_t>(vPoolPtr);
                            uint8_t* vObjects = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x00);
                            uint8_t* vByteMap = *reinterpret_cast<uint8_t**>(vPoolAddr + 0x08);
                            int32_t vPoolSize = *reinterpret_cast<int32_t*>(vPoolAddr + 0x10);
                            float bestDistSq = (rem.seatIndex == 0) ? (10.0f * 10.0f) : (24.0f * 24.0f);
                            if (vObjects && vByteMap && vPoolSize > 0 && vPoolSize <= 256) {
                                for (int32_t s = 0; s < vPoolSize; ++s) {
                                    if ((vByteMap[s] & 0x80) != 0) continue;
                                    void* cand = reinterpret_cast<void*>(vObjects + static_cast<size_t>(s) * 0xc68);
                                    if (cand == myVeh && rem.seatIndex == 0 && me.seatIndex == 0) continue;
                                    uintptr_t cvAddr = reinterpret_cast<uintptr_t>(cand);
                                    if (rem.seatIndex == 0) {
                                        void* candDrv = *reinterpret_cast<void**>(cvAddr + 0x5a0);
                                        if (candDrv == localPed) continue;
                                    }
                                    int16_t cvModel = *reinterpret_cast<int16_t*>(cvAddr + 0x32);
                                    if (expectedVehModel >= 400 && cvModel != expectedVehModel) continue;
                                    uintptr_t cMat = *reinterpret_cast<uintptr_t*>(cvAddr + 0x18);
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
                        }
                    }

                    // Si el compañero subió a un vehículo (ej. bici o ambulancia en el Guest) que aún no existe localmente,
                    // instanciar ese modelo exacto con cooldown de 90 ticks para jamás spawnear autos repetidos
                    if (!targetVeh && expectedVehModel >= 400 && rem.seatIndex == 0 &&
                        (tickCount - remoteLastVehSpawnTick_[i]) >= 90) {
                        if (!gtaSymbols_.isModelLoaded(expectedVehModel)) {
                            if ((tickCount % 5 == 0) && gtaSymbols_.cstreamingRequestModelFn) {
                                gtaSymbols_.cstreamingRequestModelFn(expectedVehModel, 2);
                            }
                        } else if (gtaSymbols_.ccarCtrlCreateCarForScriptFn) {
                            bool tooCloseToMyVeh = false;
                            if (myVeh != nullptr) {
                                uintptr_t myVMat = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(myVeh) + 0x18);
                                if (myVMat != 0) {
                                    float mdx = *reinterpret_cast<float*>(myVMat + 0x30) - targetRemX;
                                    float mdy = *reinterpret_cast<float*>(myVMat + 0x34) - targetRemY;
                                    bool isTwoWheeler = (expectedVehModel == 481 || expectedVehModel == 509 || expectedVehModel == 510 ||
                                                         expectedVehModel == 448 || expectedVehModel == 461 || expectedVehModel == 462 ||
                                                         expectedVehModel == 463 || expectedVehModel == 468 || expectedVehModel == 471 ||
                                                         expectedVehModel == 521 || expectedVehModel == 522 || expectedVehModel == 523 ||
                                                         expectedVehModel == 581 || expectedVehModel == 586);
                                    float minClear = isTwoWheeler ? 1.6f : 3.8f;
                                    if ((mdx * mdx + mdy * mdy) < (minClear * minClear)) tooCloseToMyVeh = true;
                                }
                            }
                            if (!tooCloseToMyVeh) {
                                void* spawnedPartnerVeh = gtaSymbols_.ccarCtrlCreateCarForScriptFn(
                                    expectedVehModel, CVector{targetRemX, targetRemY, targetRemZ}, 0
                                );
                                if (spawnedPartnerVeh) {
                                    remoteLastVehSpawnTick_[i] = tickCount;
                                    remotePartnerVehPtr_[i] = spawnedPartnerVeh;
                                    remotePartnerVehHandle_[i] = gtaSymbols_.cpoolsGetVehicleRefFn ? gtaSymbols_.cpoolsGetVehicleRefFn(spawnedPartnerVeh) : -1;
                                    uintptr_t svAddr = reinterpret_cast<uintptr_t>(spawnedPartnerVeh);
                                    *reinterpret_cast<uint8_t*>(svAddr + 0x610) = 2; // MISSION_VEHICLE
                                    applyVehicleDamageState(
                                        spawnedPartnerVeh, rem.vehicleHealth, rem.damageWheels,
                                        rem.damageDoorsLow, rem.damageDoorsHigh, rem.damageLights, rem.damagePanels
                                    );
                                    targetVeh = spawnedPartnerVeh;
                                }
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
                            applyVehicleDamageState(
                                targetVeh, rem.vehicleHealth, rem.damageWheels,
                                rem.damageDoorsLow, rem.damageDoorsHigh, rem.damageLights, rem.damagePanels
                            );
                            if ((targetVeh != myVeh || me.seatIndex > 0) && (config_.isHost || wantedHostSlot < 0)) {
                                float cH = std::cos(rem.heading);
                                float sH = std::sin(rem.heading);
                                SyncedWorldVehicleEntry remVe{};
                                remVe.packedColors = 0; // No sobrescribir el color real del vehículo
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
                                remVe.vehicleHealth = rem.vehicleHealth;
                                remVe.damageWheels = rem.damageWheels;
                                remVe.damageDoorsLow = rem.damageDoorsLow;
                                remVe.damageDoorsHigh = rem.damageDoorsHigh;
                                remVe.damageLights = rem.damageLights;
                                remVe.damagePanels = rem.damagePanels;
                                applySmoothVehicleTransform(targetVeh, remVe, targetVeh == myVeh || targetVeh == localRoofVehPtr_, rem.lastNetworkUpdateMs);
                                if (targetVeh == myVeh && me.seatIndex > 0 && gtaSymbols_.cpedSetPedPositionInCarFn) {
                                    gtaSymbols_.cpedSetPedPositionInCarFn(localPed);
                                }
                            }
                        } else if (rem.isAttachedOnRoof || rem.seatIndex >= 4) {
                            // Compañero remoto subido en el techo del vehículo
                            if (remCurrentlyInVeh) {
                                warpPedOutOfVehicle(ped);
                            }
                            uintptr_t tvMat = *reinterpret_cast<uintptr_t*>(tvAddr + 0x18);
                            if (tvMat != 0) {
                                float rx = *reinterpret_cast<float*>(tvMat + 0x30) + *reinterpret_cast<float*>(tvMat + 0x20) * 1.18f;
                                float ry = *reinterpret_cast<float*>(tvMat + 0x34) + *reinterpret_cast<float*>(tvMat + 0x24) * 1.18f;
                                float rz = *reinterpret_cast<float*>(tvMat + 0x38) + *reinterpret_cast<float*>(tvMat + 0x28) * 1.18f;
                                applySmoothPedTransformAndAnim(
                                    ped, rx, ry, rz,
                                    rem.vx, rem.vy, rem.vz, rem.heading,
                                    1, 200.0f, rem.lastNetworkUpdateMs,
                                    rem.isCrouching != 0
                                );
                            }
                        } else {
                            // Compañero remoto como pasajero (o ambos en el mismo auto donde localPed es conductor)
                            int seatSlot = std::clamp<int>((rem.seatIndex > 0) ? (rem.seatIndex - 1) : 0, 0, 2);
                            void* curSeatPed = *reinterpret_cast<void**>(tvAddr + 0x5a8 + static_cast<size_t>(seatSlot) * 8);
                            if (!remCurrentlyInVeh || remCurrentVeh != targetVeh || curSeatPed != ped) {
                                if (remCurrentlyInVeh && remCurrentVeh != targetVeh) {
                                    warpPedOutOfVehicle(ped);
                                }
                                warpPedIntoVehiclePassenger(ped, targetVeh, seatSlot, localPed, true);
                            }
                            if (gtaSymbols_.cpedSetPedPositionInCarFn) {
                                gtaSymbols_.cpedSetPedPositionInCarFn(ped);
                            }
                            // Activar CTaskSimpleGangDriveBy en el CCivilianPed remoto (modo 4 = AI_ALL_DIRECTIONS)
                            // con punto objetivo proyectado para que en la pantalla del conductor el acompañante saque
                            // medio cuerpo por la ventanilla y dispare como los miembros de la pandilla
                            uintptr_t remIntel = *reinterpret_cast<uintptr_t*>(remPedAddr + 0x538);
                            if (remIntel != 0 && (rem.isDriveByActive || (rem.weaponId >= 22 && rem.weaponId <= 34))) {
                                float aimDist = 35.0f;
                                CVector aimTarget{
                                    targetRemX - std::sin(rem.heading) * aimDist,
                                    targetRemY + std::cos(rem.heading) * aimDist,
                                    targetRemZ + 0.5f
                                };
                                void* remSecTask = *reinterpret_cast<void**>(remIntel + 0x30);
                                if (remSecTask == nullptr &&
                                    gtaSymbols_.ctaskOperatorNewFn &&
                                    gtaSymbols_.ctaskSimpleGangDriveByCtorFn &&
                                    gtaSymbols_.ctaskManagerSetTaskSecondaryFn) {
                                    void* dbTask = gtaSymbols_.ctaskOperatorNewFn(0x60);
                                    if (dbTask) {
                                        bool seatRHS = (seatSlot == 0 || seatSlot == 2);
                                        gtaSymbols_.ctaskSimpleGangDriveByCtorFn(dbTask, nullptr, &aimTarget, 1000.0f, 100, 4, seatRHS);
                                        gtaSymbols_.ctaskManagerSetTaskSecondaryFn(reinterpret_cast<void*>(remIntel + 0x08), dbTask, 0);
                                        remSecTask = *reinterpret_cast<void**>(remIntel + 0x30);
                                    }
                                }
                                if (remSecTask != nullptr) {
                                    uintptr_t rdbAddr = reinterpret_cast<uintptr_t>(remSecTask);
                                    *reinterpret_cast<CVector*>(rdbAddr + 0x50) = aimTarget;
                                    if ((rem.animGroupId & 0x06u) != 0 || rem.isAiming != 0) {
                                        if (*reinterpret_cast<int8_t*>(rdbAddr + 0x17) <= 2) {
                                            *reinterpret_cast<int8_t*>(rdbAddr + 0x17) = 2;
                                            *reinterpret_cast<int16_t*>(rdbAddr + 0x1e) = 0;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        applySmoothPedTransformAndAnim(
                            ped, targetRemX, targetRemY, targetRemZ,
                            rem.vx, rem.vy, rem.vz, rem.heading,
                            static_cast<int>(rem.animId), 200.0f, rem.lastNetworkUpdateMs,
                            rem.isCrouching != 0
                        );
                    }
                } else {
                    if (remCurrentlyInVeh) {
                        warpPedOutOfVehicle(ped);
                    }
                    applySmoothPedTransformAndAnim(
                        ped, targetRemX, targetRemY, targetRemZ,
                        rem.vx, rem.vy, rem.vz, rem.heading,
                        static_cast<int>(rem.animId), 200.0f, rem.lastNetworkUpdateMs,
                        rem.isCrouching != 0
                    );
                    void* remClump = *reinterpret_cast<void**>(remPedAddr + 0x20);
                    if (remClump != nullptr) {
                        bool isJumpNow = (rem.isJumping != 0);
                        if (isJumpNow && !remoteWasJumping_[i]) {
                            safeBlendAnimation(remClump, 0 /*ASSOCGRP_DEFAULT*/, 116 /*JUMP_launch*/, 8.0f);
                        } else if (isJumpNow && (tickCount % 8 == 0)) {
                            safeBlendAnimation(remClump, 0 /*ASSOCGRP_DEFAULT*/, 118 /*JUMP_glide*/, 4.0f);
                        } else if (!isJumpNow && remoteWasJumping_[i]) {
                            safeBlendAnimation(remClump, 0 /*ASSOCGRP_DEFAULT*/, 119 /*JUMP_land*/, 8.0f);
                        }
                        remoteWasJumping_[i] = isJumpNow;
                        // Reproducir animaciones 3D de golpe/ataque cuerpo a cuerpo o disparo de forma segura (verificando CAnimBlock::bLoaded)
                        if (rem.animGroupId != 0 && (tickCount % 10 == 0)) {
                            if ((rem.animGroupId & 0x06u) != 0 && rem.weaponId >= 22 && gtaSymbols_.cweaponInfoGetWeaponInfoFn) {
                                void* wepInfo = gtaSymbols_.cweaponInfoGetWeaponInfoFn(rem.weaponId, 1);
                                int32_t wepAnimGrp = wepInfo ? *reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(wepInfo) + 0x1c) : 0;
                                if (wepAnimGrp > 0) {
                                    safeBlendAnimation(remClump, wepAnimGrp, 0xe1 /*225 = ANIM_WEAPON_FIRE*/, 8.0f);
                                }
                            } else if ((rem.animGroupId & 0x01u) != 0) {
                                safeBlendAnimation(remClump, 0 /*ASSOCGRP_DEFAULT*/, 41 /*ANIM_DEFAULT_FIGHTA_1*/, 8.0f);
                            }
                        }
                    }
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

                    bool sendWorldCheatNow = pendingWorldCheatBroadcast_.exchange(false);
                    bool sendWeaponFireNow = pendingWeaponFireBroadcast_.exchange(false);
                    WorldCheatSyncPacket cheatPkt{};
                    if (sendWorldCheatNow) {
                        std::string cheatCopy;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            cheatCopy = lastWorldCheatSynced_;
                        }
                        if (!cheatCopy.empty()) {
                            cheatPkt = buildWorldCheatSyncPacket(mySlot, cheatCopy, CHEAT_WORLD_SHARED);
                        } else {
                            sendWorldCheatNow = false;
                        }
                    }
                    WeaponFirePacket firePkt{};
                    if (sendWeaponFireNow) {
                        int idx = std::clamp(mySlot, 1, kMaxCoopPlayers) - 1;
                        float targX = players_[idx].x - std::sin(players_[idx].heading) * 35.0f;
                        float targY = players_[idx].y + std::cos(players_[idx].heading) * 35.0f;
                        float targZ = players_[idx].z + 0.6f;
                        firePkt = buildWeaponFirePacket(mySlot, players_[idx].weaponId, targX, targY, targZ);
                    }

                    for (const auto& dest : peersCopy) {
                        ::sendto(sock, &pedPkt, sizeof(pedPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        int idx = std::clamp(mySlot, 1, kMaxCoopPlayers) - 1;
                        if (players_[idx].vehicleNetId > 0) {
                            VehicleSyncPacket vehPkt = buildVehicleSyncPacket(mySlot);
                            ::sendto(sock, &vehPkt, sizeof(vehPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        }
                        if (config_.isHost || mission_.onMissionFlag != 0) {
                            ::sendto(sock, &misPkt, sizeof(misPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        }
                        if (config_.isHost) {
                            ::sendto(sock, &worldPkt, sizeof(worldPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        }
                        if ((gtaSymbols_.cutsceneRunning && *gtaSymbols_.cutsceneRunning != 0) || cutsceneSkipVotesMask_ != 0) {
                            CutsceneSyncPacket csPkt = buildCutsceneSyncPacket(mySlot);
                            ::sendto(sock, &csPkt, sizeof(csPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        }
                        if (sendWorldCheatNow) {
                            ::sendto(sock, &cheatPkt, sizeof(cheatPkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                        }
                        if (sendWeaponFireNow) {
                            ::sendto(sock, &firePkt, sizeof(firePkt), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
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
                    if ((gtaSymbols_.cutsceneRunning && *gtaSymbols_.cutsceneRunning != 0) || cutsceneSkipVotesMask_ != 0) {
                        CutsceneSyncPacket csPkt = buildCutsceneSyncPacket(mySlot);
                        sendRawUdpPacket(sock, subnetBroadcastStr, config_.udpCoopPort, &csPkt, sizeof(csPkt));
                        if (subnetBroadcastStr != "255.255.255.255") {
                            sendRawUdpPacket(sock, "255.255.255.255", config_.udpCoopPort, &csPkt, sizeof(csPkt));
                        }
                    }
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
        pkt.independentCamera =
            ((p.cameraMode == 0) ? 1u : 0u) |
            ((p.isCrouching != 0) ? 2u : 0u) |
            static_cast<uint8_t>((packedColor & 0x0Fu) << 2) |
            ((p.isJumping != 0) ? 64u : 0u) |
            ((p.isAiming != 0) ? 128u : 0u);
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
        pkt.vehicleHealth = p.vehicleHealth;
        pkt.damageWheels = p.damageWheels;
        pkt.damageDoorsLow = p.damageDoorsLow;
        pkt.damageDoorsHigh = p.damageDoorsHigh;
        pkt.damageLights = p.damageLights;
        pkt.damagePanels = p.damagePanels;
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
                p.isCrouching = ((ped->independentCamera & 2u) != 0) ? 1u : 0u;
                p.isJumping = ((ped->independentCamera & 64u) != 0) ? 1u : 0u;
                p.isAiming = ((ped->independentCamera & 128u) != 0) ? 1u : 0u;
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
                p.vehicleHealth = veh->vehicleHealth;
                p.damageWheels = veh->damageWheels;
                p.damageDoorsLow = veh->damageDoorsLow;
                p.damageDoorsHigh = veh->damageDoorsHigh;
                p.damageLights = veh->damageLights;
                p.damagePanels = veh->damagePanels;

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
                if (cs->skipVotesMask != 0) {
                    cutsceneSkipVotesMask_ |= cs->skipVotesMask;
                    lastCutsceneVoteMs_ = nowMs;
                }
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
                bool newlyStartedMission = (!lastLocalOnMission_ && ms->onMissionFlag != 0 &&
                                            std::fabs(ms->checkpointPos.x) > 1.0f);
                mission_.onMissionFlag = ms->onMissionFlag;
                std::memcpy(mission_.missionId, ms->missionId, sizeof(mission_.missionId));
                mission_.missionId[sizeof(mission_.missionId) - 1] = '\0';
                mission_.checkpointIndex = ms->checkpointIndex;
                mission_.totalCheckpoints = ms->totalCheckpoints;
                if (std::fabs(ms->checkpointPos.x) > 1.0f || std::fabs(ms->checkpointPos.y) > 1.0f) {
                    mission_.checkpointX = ms->checkpointPos.x;
                    mission_.checkpointY = ms->checkpointPos.y;
                    mission_.checkpointZ = ms->checkpointPos.z;
                }
                mission_.checkpointRadius = ms->checkpointRadius;
                if (newlyStartedMission) {
                    pendingMissionMeetupTeleport_.store(true);
                }
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
            code == "ROCKETMAN" || code == "AEZAKMI" || code == "OSRBLHH" ||
            code == "ASNAEB" || code == "BAGUVIX" || code == "FULLCLIP") {
            return static_cast<int>(CHEAT_LOCAL_ONLY);
        }
        if (code == "AIWPRTON" || code == "JUMPJET" || code == "OHDUDE" ||
            code == "PDNEJOH" || code == "FOURWHEELFUN") {
            return static_cast<int>(CHEAT_VEHICLE_SPAWN);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        lastWorldCheatSynced_ = code;
        pendingWorldCheatBroadcast_.store(true, std::memory_order_relaxed);
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

    void onRadarDrawBlips(float /*circleSize*/) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!gtaSymbols_.cradarDrawRotatingRadarSpriteFn ||
            !gtaSymbols_.cradarBlipSprites ||
            !gtaSymbols_.cradarTransformRealWorldPointToRadarSpaceFn ||
            !gtaSymbols_.cradarLimitRadarPointFn ||
            !gtaSymbols_.cradarTransformRadarPointToScreenSpaceFn) {
            return;
        }

        int localIdx = std::clamp(config_.localPlayerSlot, 1, kMaxCoopPlayers) - 1;
        void* centreSprite = reinterpret_cast<uint8_t*>(gtaSymbols_.cradarBlipSprites) + 0x10; // &CRadar::RadarBlipSprites[2] (RADAR_SPRITE_CENTRE "gota")

        bool isPauseMap = false;
        if (gtaSymbols_.frontEndMenuManager != nullptr) {
            isPauseMap = (*reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(gtaSymbols_.frontEndMenuManager) + 0x8c) != 0);
        }
        float camOrient = (!isPauseMap && gtaSymbols_.libBase != 0)
            ? *reinterpret_cast<float*>(gtaSymbols_.libBase + 0xc24830)
            : 0.0f;

        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            if (i == localIdx) continue;
            const PlayerState3D& rem = players_[i];
            if (!rem.isActive) continue;

            float remX = rem.x;
            float remY = rem.y;
            float remHeadingRad = rem.heading * (3.1415926535f / 180.0f);

            void* ped = remotePedPtr_[i];
            if (ped != nullptr) {
                uintptr_t pedAddr = reinterpret_cast<uintptr_t>(ped);
                uint8_t pedFlags = *reinterpret_cast<uint8_t*>(pedAddr + 0x585);
                bool inVeh = ((pedFlags & 1u) != 0);
                void* veh = inVeh ? *reinterpret_cast<void**>(pedAddr + 0x708) : nullptr;
                uintptr_t entityAddr = veh ? reinterpret_cast<uintptr_t>(veh) : pedAddr;
                uintptr_t mat = *reinterpret_cast<uintptr_t*>(entityAddr + 0x18);
                if (mat != 0) {
                    remX = *reinterpret_cast<float*>(mat + 0x30);
                    remY = *reinterpret_cast<float*>(mat + 0x34);
                    float upX = *reinterpret_cast<float*>(mat + 0x10);
                    float upY = *reinterpret_cast<float*>(mat + 0x14);
                    if (std::fabs(upX) > 1e-4f || std::fabs(upY) > 1e-4f) {
                        remHeadingRad = std::atan2(-upX, upY);
                    }
                }
            }

            CVector2D worldPt{remX, remY};
            CVector2D radarPt{0.0f, 0.0f};
            CVector2D screenPt{0.0f, 0.0f};
            gtaSymbols_.cradarTransformRealWorldPointToRadarSpaceFn(&radarPt, &worldPt);
            gtaSymbols_.cradarLimitRadarPointFn(&radarPt);
            gtaSymbols_.cradarTransformRadarPointToScreenSpaceFn(&screenPt, &radarPt);

            uint32_t desiredSlotColor = (rem.markerColor <= 6)
                ? static_cast<uint32_t>(rem.markerColor)
                : ((i == 0) ? 1u : 0u);

            GtaCRGBA rgba{34, 197, 94, 255};
            if (gtaSymbols_.cradarGetRadarTraceColourFn) {
                uint32_t traceCol = gtaSymbols_.cradarGetRadarTraceColourFn(desiredSlotColor, 1u, 0u);
                rgba.r = static_cast<uint8_t>((traceCol >> 24) & 0xFFu);
                rgba.g = static_cast<uint8_t>((traceCol >> 16) & 0xFFu);
                rgba.b = static_cast<uint8_t>((traceCol >> 8) & 0xFFu);
                rgba.a = 255u;
            } else {
                switch (desiredSlotColor) {
                    case 0: rgba = {239, 68, 68, 255}; break;   // Rojo
                    case 1: rgba = {34, 197, 94, 255}; break;   // Verde
                    case 2: rgba = {56, 189, 248, 255}; break;  // Azul
                    case 3: rgba = {248, 250, 252, 255}; break; // Blanco
                    case 4: rgba = {234, 179, 8, 255}; break;   // Amarillo
                    case 5: rgba = {168, 85, 247, 255}; break;  // Morado
                    case 6: rgba = {6, 182, 212, 255}; break;   // Celeste
                    default: break;
                }
            }

            float angle = remHeadingRad - (camOrient + 3.1415926535f);
            gtaSymbols_.cradarDrawRotatingRadarSpriteFn(centreSprite, screenPt.x, screenPt.y, angle, 1.0f, &rgba);
        }
#endif
    }

    void onLocateCharCommand(void* selfScript, int commandId) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (!selfScript || !gtaSymbols_.scriptParams) return;
        bool onMission = (gtaSymbols_.onAMissionFlagOffset && *gtaSymbols_.onAMissionFlagOffset != 0) ||
                         (gtaSymbols_.isPlayerOnAMissionFn && gtaSymbols_.isPlayerOnAMissionFn());

        auto* spInt = reinterpret_cast<int32_t*>(gtaSymbols_.scriptParams);
        auto* spFlt = reinterpret_cast<float*>(gtaSymbols_.scriptParams);
        bool is3D = (static_cast<uint32_t>(commandId - 0xFE) <= 5u);
        int32_t bSphere = is3D ? spInt[7] : spInt[5];
        float targetX = spFlt[1];
        float targetY = spFlt[2];
        float rx = is3D ? spFlt[4] : spFlt[3];
        float ry = is3D ? spFlt[5] : spFlt[4];
        bool isSmallMissionTrigger = (std::fabs(rx) > 0.1f && std::fabs(rx) <= 2.5f &&
                                      std::fabs(ry) > 0.1f && std::fabs(ry) <= 2.5f);
        if (bSphere == 0 && !isSmallMissionTrigger) return;

        // Jamás interceptar LOCATE_CHAR_* mientras una misión ya está activa ($ONMISSION != 0)
        // ni cuando el compañero ya disparó el inicio de la misión (mission_.onMissionFlag != 0),
        // para nunca tildar cámaras fijas ni bloquear al segundo jugador al entrar a la misma misión
        if (onMission || mission_.onMissionFlag != 0) return;

        uintptr_t scriptAddr = reinterpret_cast<uintptr_t>(selfScript);
        uint8_t condResult = *reinterpret_cast<uint8_t*>(scriptAddr + 0x115);
        uint8_t notFlag = *reinterpret_cast<uint8_t*>(scriptAddr + 0x122);
        // En CRunningScript::UpdateCompareFlag: m_bCondResult ya fue XOReado con m_bNotFlag.
        // Por lo tanto, el jugador local está físicamente dentro del marcador rojo cuando ((condResult != 0) ^ (notFlag != 0)) es true.
        bool localInsideMarker = ((condResult != 0) != (notFlag != 0));
        if (!localInsideMarker) return;

        float reqRx = std::max(std::fabs(rx), 1.5f) + 1.5f;
        float reqRy = std::max(std::fabs(ry), 1.5f) + 1.5f;

        uint64_t nowMs = currentTimeMillis();
        int localIdx = std::clamp(config_.localPlayerSlot, 1, kMaxCoopPlayers) - 1;
        bool hasAnyActivePartner = false;
        bool allPartnersInside = true;

        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            if (i == localIdx) continue;
            const PlayerState3D& rem = players_[i];
            if (!rem.isActive || !rem.hasRealGtaCoords) continue;
            if (rem.lastNetworkUpdateMs == 0 || (nowMs - rem.lastNetworkUpdateMs) > 15000) continue;
            hasAnyActivePartner = true;
            float dx = std::fabs(rem.x - targetX);
            float dy = std::fabs(rem.y - targetY);
            if (dx > reqRx || dy > reqRy) {
                allPartnersInside = false;
                break;
            }
        }

        // Si hay compañero cooperativo conectado y AÚN NO está dentro de la señal roja,
        // mantener el marcador rojo dibujado pero reportar "aún no dentro" al script SCM para esperar al compañero
        if (hasAnyActivePartner && !allPartnersInside) {
            *reinterpret_cast<uint8_t*>(scriptAddr + 0x115) = (notFlag != 0) ? 1u : 0u;
        }
#endif
    }

    bool canStartMissionTogether() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (mission_.onMissionFlag != 0) return true;
        uint64_t nowMs = currentTimeMillis();
        int localIdx = std::clamp(config_.localPlayerSlot, 1, kMaxCoopPlayers) - 1;
        const PlayerState3D& me = players_[localIdx];
        if (!me.hasRealGtaCoords) return true;

        bool hasAnyActivePartner = false;
        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            if (i == localIdx) continue;
            const PlayerState3D& rem = players_[i];
            if (!rem.isActive || !rem.hasRealGtaCoords) continue;
            if (rem.lastNetworkUpdateMs == 0 || (nowMs - rem.lastNetworkUpdateMs) > 15000) continue;
            hasAnyActivePartner = true;
            float dx = rem.x - me.x;
            float dy = rem.y - me.y;
            float dz = rem.z - me.z;
            // Exigir que todos los jugadores cooperativos estén literalmente juntos dentro de la señal roja (< 3.2m) antes de iniciar la misión
            if ((dx * dx + dy * dy) > (3.2f * 3.2f) || std::fabs(dz) > 2.5f) {
                return false;
            }
        }
        return !hasAnyActivePartner || true;
#else
        return true;
#endif
    }

    bool onCutsceneSkipButtonCheck(bool localPressed) {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        uint64_t nowMs = currentTimeMillis();
        int localIdx = std::clamp(config_.localPlayerSlot, 1, kMaxCoopPlayers) - 1;
        uint8_t myBit = static_cast<uint8_t>(1U << localIdx);
        if (localPressed) {
            cutsceneSkipVotesMask_ |= myBit;
            lastCutsceneVoteMs_ = nowMs;
        }
        uint8_t activeMask = myBit;
        for (int i = 0; i < kMaxCoopPlayers; ++i) {
            if (i != localIdx && players_[i].isActive && players_[i].lastNetworkUpdateMs > 0 &&
                (nowMs - players_[i].lastNetworkUpdateMs) < 15000) {
                activeMask |= static_cast<uint8_t>(1U << i);
            }
        }
        // Si todos los jugadores activos ya dieron su aprobación (o si estás solo), permitir el salto simultáneo
        // y mantener el broadcast de votos 1.2s para que el otro teléfono reciba con certeza el bit unánime
        if ((cutsceneSkipVotesMask_ & activeMask) == activeMask) {
            if (cutsceneSkipApprovedUntilMs_ < nowMs) {
                cutsceneSkipApprovedUntilMs_ = nowMs + 1200;
                LOGI("Unanimous cutscene skip approved (votes=0x%x / active=0x%x)!", cutsceneSkipVotesMask_, activeMask);
            }
            return true;
        }
        return false;
#else
        return localPressed;
#endif
    }

    void onCutsceneSkipRequested() {
#if defined(__unix__) || defined(__ANDROID__) || defined(__linux__)
        if (onCutsceneSkipButtonCheck(true)) {
            if (g_origCCutsceneMgrSkipCutscene) {
                g_origCCutsceneMgrSkipCutscene();
            }
        }
#endif
    }

    bool shouldForcePassengerFire(void* ped) const {
        return ped != nullptr &&
               ped == localPassengerDriveByPed_.load(std::memory_order_relaxed) &&
               localPassengerDriveByFiring_.load(std::memory_order_relaxed);
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
            int spawnedOrLocalModel = (i + 1 == config_.localPlayerSlot) ? localAppliedSkinModel_ : remotePedSpawnedModel_[i];
            bool specialLoaded = (spawnedOrLocalModel >= 290 && spawnedOrLocalModel <= 299 &&
                                  gtaSymbols_.cstreamingHasSpecialCharLoadedFn &&
                                  gtaSymbols_.cstreamingHasSpecialCharLoadedFn(spawnedOrLocalModel - 290));
            int loaded = (specialLoaded || (safeSkin > 0 && gtaSymbols_.isModelLoaded(safeSkin)) || safeSkin == 0) ? 1 : 0;
            fprintf(logf, "Slot P%d: active=%d | reqSkinModelId=%d | safePedModelId=%d | isModelLoaded=%d | remotePedSpawnedModel=%d | wep=%d | pedPtr=%p | pos=(%.1f, %.1f, %.1f)\n",
                    i + 1, players_[i].isActive ? 1 : 0, reqSkin, safeSkin, loaded,
                    spawnedOrLocalModel, players_[i].weaponId, remotePedPtr_[i], players_[i].x, players_[i].y, players_[i].z);
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

    static const char* getSpecialCharDffName(int32_t modelId) {
        switch (modelId) {
            case 1:   return "truth";
            case 2:   return "maccer";
            case 3:   return "andre";
            case 4:   return "bbthin";
            case 5:   return "bb";
            case 6:   return "emmet";
            case 8:   return "janitor";
            case 42:  return "jethro";
            case 65:  return "kendl";
            case 86:  return "ryder3";
            case 149: return "smokev";
            case 265: return "tenpen";
            case 266: return "pulaski";
            case 267: return "hern";
            case 268: return "dwaine";
            case 269: return "smoke";
            case 270: return "sweet";
            case 271: return "ryder";
            case 272: return "forelli";
            case 273: return "tbone";
            case 289: return "zero";
            case 290: return "rose";
            case 291: return "paul";
            case 292: return "cesar";
            case 293: return "ogloc";
            case 294: return "wuzimu";
            case 295: return "torino";
            case 296: return "jizzy";
            case 297: return "maddogg";
            case 298: return "cat";
            case 299: return "claude";
            default:  return nullptr;
        }
    }

    int32_t resolvePlayerSkinModelId(int32_t reqSkinId, int playerIdx, bool canRequestStreaming, bool& outLoaded) {
        outLoaded = false;
        if (reqSkinId <= 0) {
            outLoaded = true;
            return 0;
        }
        int clampedIdx = std::clamp(playerIdx, 0, kMaxCoopPlayers - 1);
        const char* specialName = getSpecialCharDffName(reqSkinId);
        if (specialName != nullptr &&
            gtaSymbols_.cstreamingRequestSpecialCharFn &&
            gtaSymbols_.cstreamingHasSpecialCharLoadedFn) {
            // Usar slots especiales dedicados 6..9 (modelos 296..299) para P1..P4 sin colisionar con actores de misión (0..5)
            int specialSlot = 6 + clampedIdx;
            int32_t specialModelId = 290 + specialSlot;
            if (specialSlotRequestedSkin_[clampedIdx] != reqSkinId) {
                gtaSymbols_.cstreamingRequestSpecialCharFn(specialSlot, specialName, 2);
                specialSlotRequestedSkin_[clampedIdx] = reqSkinId;
                return specialModelId;
            }
            bool loaded = gtaSymbols_.cstreamingHasSpecialCharLoadedFn(specialSlot) &&
                          gtaSymbols_.isValidModelInfo(specialModelId);
            if (!loaded && canRequestStreaming) {
                gtaSymbols_.cstreamingRequestSpecialCharFn(specialSlot, specialName, 2);
            }
            outLoaded = loaded;
            return specialModelId;
        }

        int32_t safeModel = resolveSafePedModelId(reqSkinId);
        if (safeModel > 0 && !gtaSymbols_.isValidModelInfo(safeModel)) {
            safeModel = 105;
        }
        if (safeModel <= 0) {
            outLoaded = true;
            return 0;
        }
        if (gtaSymbols_.isModelLoaded(safeModel)) {
            outLoaded = true;
        } else if (canRequestStreaming && gtaSymbols_.cstreamingRequestModelFn) {
            gtaSymbols_.cstreamingRequestModelFn(safeModel, 2);
        }
        return safeModel;
    }

    static int32_t resolveSafePedModelId(int32_t modelId) {
        if (modelId <= 0) return 0;
        switch (modelId) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 8:
            case 42:
            case 65:
            case 86: return 7;     // Special unallocated slots -> male01 (7)
            case 74:
            case 119:
            case 208: return 105;  // Missing peds.ide IDs -> FAM1 (105)
            case 149:
            case 269: return 107;  // Big Smoke (149 commented out in peds.ide) -> FAM3 Grove Homie (107)
            case 265: return 280;  // Tenpenny -> LAPD1 (280)
            case 266: return 280;  // Pulaski -> LAPD1 (280)
            case 267: return 280;  // Hernandez -> LAPD1 (280)
            case 268: return 50;   // Dwaine -> Mechanic (50)
            case 270: return 105;  // Sweet -> Grove Homie FAM1 (105)
            case 271: return 106;  // Ryder -> Grove Homie FAM2 (106)
            case 272: return 112;  // Forelli -> Mafia MAFFY (112)
            case 273: return 107;  // T-Bone -> FAM3 (107)
            case 289: return 170;  // Zero -> SOMYST (170)
            case 290: return 187;  // Ken Rosenberg -> SOFYBU (187)
            case 291: return 29;   // Kent Paul -> WMIDRUG (29)
            case 292: return 115;  // Cesar -> Azteca VLA2 (115)
            case 293: return 107;  // OG Loc -> Grove FAM3 (107)
            case 294: return 118;  // Woozie -> Triad TRIADB (118)
            case 295: return 165;  // Mike Toreno -> WMOMIB (165)
            case 296: return 23;   // Jizzy -> BMYDJ (23)
            case 297: return 22;   // Madd Dogg -> BMYST (22)
            case 298: return 192;  // Catalina -> WFYPRO (192)
            case 299: return 100;  // Claude Speed -> BIKERA (100)
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
    std::atomic<bool> pendingMissionMeetupTeleport_{false};
    std::atomic<bool> localPassengerDriveByFiring_{false};
    std::atomic<void*> localPassengerDriveByPed_{nullptr};
    std::atomic<bool> pendingWorldCheatBroadcast_{false};
    std::atomic<bool> pendingWeaponFireBroadcast_{false};
    bool lastLocalOnMission_ = false;
    int32_t localAppliedSkinModel_ = 0;
    int32_t localAppliedSkinReqId_ = 0;
    int32_t remotePedAppliedSkinId_[kMaxCoopPlayers] = {0, 0, 0, 0};
    std::thread udpWorkerThread_;
    GtaSaNativeSymbols gtaSymbols_;
    BridgeConfig config_;
    PlayerState3D players_[kMaxCoopPlayers];
    MissionSyncState mission_;
    WorldSyncState worldState_;
    uint8_t cutsceneSkipVotesMask_ = 0;
    uint64_t cutsceneSkipApprovedUntilMs_ = 0;
    uint64_t lastCutsceneVoteMs_ = 0;
    bool cutsceneSkipAlreadyTriggered_ = false;
    int64_t lastPassengerFireTick_ = 0;
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
    int32_t specialSlotRequestedSkin_[kMaxCoopPlayers] = {-1, -1, -1, -1};
    bool remoteWasJumping_[kMaxCoopPlayers] = {false, false, false, false};
    int32_t remotePedEquippedWeapon_[kMaxCoopPlayers] = {-1, -1, -1, -1};
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
    bool worldSyncVehSpawnedByUs_[kMaxSyncedWorldVehicles] = {false};
    void* localRoofVehPtr_ = nullptr;
    int64_t localRoofAttachTick_ = 0;
    int64_t lastGuestVehSpawnTick_ = 0;
    int64_t lastLocalSpareBikeTick_ = 0;
    void* remotePartnerVehPtr_[kMaxCoopPlayers] = {nullptr};
    int remotePartnerVehHandle_[kMaxCoopPlayers] = {-1, -1, -1, -1};
    int64_t remoteLastVehSpawnTick_[kMaxCoopPlayers] = {0, 0, 0, 0};

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

static void Hooked_CClothes_RebuildPlayer(void* playerPed, bool bIgnoreFatAndMuscle) {
    if (!playerPed) return;
    int16_t modelIdx = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(playerPed) + 0x32);
    if (modelIdx != 0) {
        return; // Proteger skin personalizada en localPed contra reconstrucción de ropa de CJ
    }
    if (g_origCClothesRebuildPlayer) {
        g_origCClothesRebuildPlayer(playerPed, bIgnoreFatAndMuscle);
    }
}

static void Hooked_CClothes_RebuildPlayerIfNeeded(void* playerPed) {
    if (!playerPed) return;
    int16_t modelIdx = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(playerPed) + 0x32);
    if (modelIdx != 0) {
        return;
    }
    if (g_origCClothesRebuildPlayerIfNeeded) {
        g_origCClothesRebuildPlayerIfNeeded(playerPed);
    }
}

static void Hooked_CClothes_RebuildCutscenePlayer(void* playerPed, int modelId) {
    if (!playerPed) return;
    int16_t modelIdx = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(playerPed) + 0x32);
    if (modelIdx != 0 && modelId == 0) {
        return;
    }
    if (g_origCClothesRebuildCutscenePlayer) {
        g_origCClothesRebuildCutscenePlayer(playerPed, modelId);
    }
}

static void Hooked_CRadar_DrawBlips(float circleSize) {
    if (g_origCRadarDrawBlips) {
        g_origCRadarDrawBlips(circleSize);
    }
    CoopEngineBridge::instance().onRadarDrawBlips(circleSize);
}

static void Hooked_CRunningScript_LocateCharCommand(void* selfScript, int commandId) {
    if (g_origCRunningScriptLocateCharCommand) {
        g_origCRunningScriptLocateCharCommand(selfScript, commandId);
    }
    CoopEngineBridge::instance().onLocateCharCommand(selfScript, commandId);
}

static bool Hooked_CPlayerPed_CanPlayerStartMission(void* selfPlayerPed) {
    bool orig = g_origCPlayerPedCanPlayerStartMission
        ? g_origCPlayerPedCanPlayerStartMission(selfPlayerPed)
        : true;
    if (!orig) return false;
    return CoopEngineBridge::instance().canStartMissionTogether();
}

static void Hooked_CCutsceneMgr_SkipCutscene() {
    CoopEngineBridge::instance().onCutsceneSkipRequested();
}

static bool Hooked_CCutsceneMgr_IsSkipButtonPressed() {
    bool localPressed = g_origCCutsceneMgrIsSkipPressed
        ? g_origCCutsceneMgrIsSkipPressed()
        : false;
    return CoopEngineBridge::instance().onCutsceneSkipButtonCheck(localPressed);
}

static uint8_t Hooked_CPad_GetWeapon(void* selfPad, void* ped, bool bCheckWeapon) {
    if (CoopEngineBridge::instance().shouldForcePassengerFire(ped)) {
        return 1u;
    }
    return g_origCPadGetWeapon ? g_origCPadGetWeapon(selfPad, ped, bCheckWeapon) : 0u;
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
