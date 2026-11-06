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

enum class SkinCategory(val title: String) {
    PROTAGONISTS("Protagonistas y Aliados"),
    GANGS("Pandillas"),
    FORCES_VILLAINS("Fuerzas y Villanos")
}

enum class CharacterSkin(
    val displayName: String,
    val tag: String,
    val avatarColorHex: Long,
    val modelId: Int,
    val pedFallbackModelId: Int = modelId,
    val category: SkinCategory = SkinCategory.PROTAGONISTS
) {
    // Protagonistas y Aliados
    CJ("Carl 'CJ' Johnson", "Grove Street", 0xFF22C55E, 0, 0, SkinCategory.PROTAGONISTS),
    SWEET("Sean 'Sweet' Johnson", "Grove Boss", 0xFF15803D, 270, 105, SkinCategory.PROTAGONISTS),
    RYDER("Lance 'Ryder' Wilson", "Old School Grove", 0xFF166534, 271, 106, SkinCategory.PROTAGONISTS),
    BIG_SMOKE("Melvin 'Big Smoke'", "Front Street", 0xFFEAB308, 269, 149, SkinCategory.PROTAGONISTS),
    BIG_SMOKE_VEST("Big Smoke Chaleco", "Crack Palace", 0xFFCA8A04, 149, 149, SkinCategory.PROTAGONISTS),
    CESAR("Cesar Vialpando", "Varrios Los Aztecas", 0xFF0284C7, 292, 115, SkinCategory.PROTAGONISTS),
    WU_ZI_MU("Wu Zi Mu 'Woozie'", "Mountain Cloud Boys", 0xFF6366F1, 294, 118, SkinCategory.PROTAGONISTS),
    THE_TRUTH("The Truth", "Badlands Mystic", 0xFF84CC16, 1, 1, SkinCategory.PROTAGONISTS),
    MIKE_TORENO("Mike Toreno", "Undercover Agent", 0xFF64748B, 295, 165, SkinCategory.PROTAGONISTS),
    CATALINA("Catalina", "Fern Ridge Outlaw", 0xFFDC2626, 298, 193, SkinCategory.PROTAGONISTS),
    KENDL("Kendl Johnson", "Families Diplomat", 0xFFEC4899, 193, 193, SkinCategory.PROTAGONISTS),
    CLAUDE_SPEED("Claude Speed", "GTA III Legend", 0xFF334155, 299, 100, SkinCategory.PROTAGONISTS),
    ZERO("Zero", "RC Shop Nerd", 0xFF0EA5E9, 289, 170, SkinCategory.PROTAGONISTS),
    OG_LOC("OG Loc", "Burger Shot Rapper", 0xFFF97316, 293, 107, SkinCategory.PROTAGONISTS),
    MADD_DOGG("Madd Dogg", "Los Santos Rapper", 0xFFD97706, 297, 22, SkinCategory.PROTAGONISTS),
    MACCER("Maccer", "The Gurning Chimps", 0xFFA855F7, 2, 2, SkinCategory.PROTAGONISTS),
    KENT_PAUL("Kent Paul", "Music Manager", 0xFF38BDF8, 291, 29, SkinCategory.PROTAGONISTS),
    KEN_ROSENBERG("Ken Rosenberg", "Caligula's Lawyer", 0xFFF43F5E, 290, 187, SkinCategory.PROTAGONISTS),

    // Pandillas
    HOMIE_GROVE_1("Homie Grove 1", "Grove Street", 0xFF22C55E, 105, 105, SkinCategory.GANGS),
    HOMIE_GROVE_2("Homie Grove 2", "Grove Street", 0xFF16A34A, 106, 106, SkinCategory.GANGS),
    HOMIE_GROVE_3("Homie Grove 3", "Grove Street", 0xFF15803D, 107, 107, SkinCategory.GANGS),
    BALLAS_1("Ballas 1", "Rollin Heights", 0xFF9333EA, 102, 102, SkinCategory.GANGS),
    BALLAS_2("Ballas 2", "Front Yard Ballas", 0xFF7E22CE, 103, 103, SkinCategory.GANGS),
    VAGOS_1("Vagos 1", "Los Santos Vagos", 0xFFFACC15, 108, 108, SkinCategory.GANGS),
    AZTECA_1("Azteca 1", "Varrios Los Aztecas", 0xFF06B6D4, 114, 114, SkinCategory.GANGS),
    TRIAD("Triad", "San Fierro Triads", 0xFFEF4444, 117, 117, SkinCategory.GANGS),
    DA_NANG_BOY("Da Nang Boy", "Da Nang Boys", 0xFF84CC16, 121, 121, SkinCategory.GANGS),
    MAFIA_RUSA("Mafia Rusa", "Russian Mafia", 0xFF94A3B8, 111, 111, SkinCategory.GANGS),
    BIKER("Biker", "San Andreas Bikers", 0xFF78350F, 100, 100, SkinCategory.GANGS),

    // Fuerzas y Villanos
    TENPENNY("Tenpenny", "C.R.A.S.H.", 0xFF1E3A8A, 265, 280, SkinCategory.FORCES_VILLAINS),
    PULASKI("Pulaski", "C.R.A.S.H.", 0xFF1D4ED8, 266, 280, SkinCategory.FORCES_VILLAINS),
    HERNANDEZ("Hernandez", "C.R.A.S.H.", 0xFF2563EB, 267, 280, SkinCategory.FORCES_VILLAINS),
    LSPD_COP("Policía LSPD", "Los Santos PD", 0xFF3B82F6, 280, 280, SkinCategory.FORCES_VILLAINS),
    SWAT_TACTICAL("SWAT Táctico", "Fuerzas Especiales", 0xFF0F172A, 285, 285, SkinCategory.FORCES_VILLAINS),
    FBI_AGENT("Agente FBI", "Federal Agent", 0xFF1E293B, 286, 286, SkinCategory.FORCES_VILLAINS),
    ARMY_SOLDIER("Soldado Ejército", "Area 69 Military", 0xFF3F6212, 287, 287, SkinCategory.FORCES_VILLAINS),
    PARAMEDIC("Médico", "Los Santos EMS", 0xFFEF4444, 274, 274, SkinCategory.FORCES_VILLAINS),
    FIREFIGHTER("Bombero", "LSFD Rescue", 0xFFF97316, 277, 277, SkinCategory.FORCES_VILLAINS)
}

enum class PlayerMarkerColor(val radarColorId: Int, val displayName: String, val colorHex: Long) {
    GREEN(1, "Verde", 0xFF22C55E),
    RED(0, "Rojo", 0xFFEF4444),
    BLUE(2, "Azul", 0xFF38BDF8),
    YELLOW(4, "Amarillo", 0xFFEAB308),
    PURPLE(5, "Morado", 0xFFA855F7),
    CYAN(6, "Celeste", 0xFF06B6D4),
    WHITE(3, "Blanco", 0xFFF8FAFC)
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
    val seatIndex: Int = 0, // -1 = a pie, 0 = conductor, 1..3 = pasajero, 4..6 = Vehicle Surf (techo/chasis)
    val isDriveByActive: Boolean = false,
    val isAttachedOnRoof: Boolean = false,
    val hasIndependentCamera: Boolean = true
) {
    val wantedStars: Int get() = wantedLevel.coerceIn(0, 6)
}

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
