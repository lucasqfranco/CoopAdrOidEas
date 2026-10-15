package com.example.model

enum class CampaignAct(val title: String, val subtitle: String, val icon: String) {
    ACT_1("Act I: Los Santos", "Grove Street Families & Los Santos Hoods", "🌴"),
    ACT_2("Act II: The Badlands", "Red County, Mount Chiliad & The Truth", "🌲"),
    ACT_3("Act III: San Fierro", "Garages, Triads & Loco Syndicate", "🌁"),
    ACT_4("Act IV: Bone County", "Desert Airstrip & Secret Area 69", "🏜️"),
    ACT_5("Act V: Las Venturas", "Casinos, Mob Heists & The Strip", "🎰"),
    ACT_6("Act VI: Return to LS", "Riot in Los Santos & End of the Line", "🔥")
}

data class MissionCheckpoint(
    val id: String,
    val description: String,
    val role1Task: String,
    val role2Task: String,
    val role3Task: String = "Provide perimeter security",
    val role4Task: String = "Tactical support & transport"
)

enum class MissionDifficulty {
    EASY, MEDIUM, HARD, EXTREME
}

data class Mission(
    val id: String,
    val title: String,
    val bossOrGiver: String,
    val act: CampaignAct,
    val synopsis: String,
    val coOpRole1: String, // e.g. "Primary Driver & Wheelman"
    val coOpRole2: String, // e.g. "Drive-By Shooter & Heavy Weapons"
    val coOpRole3: String = "Flanker & Recon",
    val coOpRole4: String = "Demolitions / Aerial Cover",
    val difficulty: MissionDifficulty,
    val cashReward: Int,
    val respectReward: Int,
    val unlockedVehiclesOrGifts: String,
    val checkpoints: List<MissionCheckpoint>,
    val isCompleted: Boolean = false,
    val isAvailable: Boolean = true
)

enum class SessionStatus {
    LOBBY,
    BRIEFING,
    IN_PROGRESS,
    CHECKPOINT_REACHED,
    MISSION_PASSED,
    MISSION_FAILED
}

enum class NetworkConnectionMode(val title: String, val shortLabel: String, val description: String) {
    LAN_WIFI(
        "Modo LAN (Wi-Fi / Hotspot Local)",
        "LAN Wi-Fi",
        "Auto-descubrimiento UDP (8988), Control TCP (8989) y Telemetría/Voz UDP (8990)"
    ),
    ONLINE_REMOTE(
        "Modo Online a Distancia (Código de Sala)",
        "Online (Room Code)",
        "Emparejamiento por código de sala (STUN + WebSocket/Firestore Relay) sin abrir puertos"
    )
}

enum class CharacterSkin(val displayName: String, val tag: String, val avatarColorHex: Long) {
    CJ("Carl 'CJ' Johnson", "Grove Street", 0xFF22C55E),
    SWEET("Sean 'Sweet' Johnson", "Grove Boss", 0xFF15803D),
    RYDER("Lance 'Ryder' Wilson", "Old School Grove", 0xFF166534),
    BIG_SMOKE("Melvin 'Big Smoke'", "Front Street", 0xFFEAB308),
    CESAR("Cesar Vialpando", "Varrios Los Aztecas", 0xFF0284C7),
    KENDL("Kendl Johnson", "Families Diplomat", 0xFFEC4899),
    THE_TRUTH("The Truth", "Badlands Mystic", 0xFF84CC16),
    WU_ZI_MU("Wu Zi Mu 'Woozie'", "Mountain Cloud Boys", 0xFF6366F1),
    MIKE_TORENO("Mike Toreno", "Undercover Agent", 0xFF64748B),
    CATALINA("Catalina", "Fern Ridge Outlaw", 0xFFDC2626)
}

enum class PlayerCoopRole(val title: String, val icon: String, val description: String) {
    DRIVER("Driver / Wheelman", "🚗", "Navigates escapes, high-speed chases, and vehicle evasions"),
    GUNNER("Drive-By Gunner", "🎯", "Target acquisition, passenger shooting, and heavy combat"),
    INFILTRATOR("Infiltrator / Pointman", "🥷", "Silent takedowns, lockpicking, and objective retrieval"),
    SNIPER("Overwatch / Sniper", "🔭", "Elevated high-ground coverage and target suppression"),
    HEAVY("Heavy Weapons / Demolition", "💥", "RPG coverage, barricade breaches, and crowd control")
}

data class CoopPlayer(
    val id: String,
    val name: String,
    val skin: CharacterSkin,
    val role: PlayerCoopRole,
    val isHost: Boolean = false,
    val isReady: Boolean = false,
    val pingMs: Int = 18,
    val health: Int = 100,
    val armor: Int = 100,
    val wantedLevel: Int = 0,
    val isSpeaking: Boolean = false,
    val isMuted: Boolean = false,
    val currentWeapon: String = "Micro SMG (Tec-9)",
    val currentVehicle: String = "Green Greenwood",
    val posX: Float = 2495.0f,
    val posY: Float = -1687.0f,
    val posZ: Float = 13.5f,
    val heading: Float = 0.0f,
    val vehicleNetId: Int = 101,
    val seatIndex: Int = 0, // -1 = a pie, 0 = conductor, 1..3 = pasajero
    val isDriveByActive: Boolean = false,
    val hasIndependentCamera: Boolean = true
)

enum class TacticalPingType(val label: String, val icon: String) {
    REGROUP("Regroup at my position!", "📍"),
    GET_IN("Get in the vehicle, let's roll!", "🚗"),
    ENEMY_AHEAD("Ambush ahead, watch corners!", "⚠️"),
    NEED_BACKUP("I'm taking heavy fire, cover me!", "🛡️"),
    HOLD_FIRE("Hold fire, wait for signal!", "🤫"),
    SNIPER_UP("Enemy sniper spotted on roof!", "🎯"),
    FOLLOW_TRAIN("All we had to do was follow the damn train!", "🚂"),
    RESPECT_BOOST("Grove Street 4 Life!", "👊")
}

data class TacticalPing(
    val id: String,
    val senderName: String,
    val type: TacticalPingType,
    val timestampMs: Long
)
