package com.example.model

enum class GameEdition(
    val title: String,
    val packageName: String,
    val description: String,
    val supportedVersions: String
) {
    ORIGINAL_MOBILE(
        "GTA San Andreas (Classic Mobile)",
        "com.rockstargames.gtasa",
        "Classic mobile port (v1.08 / v2.00 / v2.10) with native CLEO/AML hook capability",
        "v1.08, v2.00, v2.10"
    ),
    DEFINITIVE_EDITION(
        "GTA San Andreas: The Definitive Edition",
        "com.rockstargames.gtasade",
        "Unreal Engine 4 remastered edition with updated touch controls",
        "v1.72+"
    ),
    NETFLIX_EDITION(
        "GTA San Andreas: Netflix Edition",
        "com.rockstargames.gtasa.netflix",
        "Netflix games distribution with classic lighting toggle",
        "v1.72.428"
    ),
    CUSTOM_CLEO(
        "Custom Modded / CLEO / AML Directory",
        "custom.gtasa.modded",
        "Custom local installation with user-defined textures, vehicles, and scripts",
        "Universal Modded"
    )
}

data class SaveGameSlot(
    val slotNumber: Int, // 1 to 8 (e.g. GTASAsf1.b)
    val fileName: String,
    val missionName: String,
    val completionPercentage: Float,
    val lastSavedTimestamp: String,
    val cashAmount: Int,
    val respectPercentage: Int,
    val isCoopTagged: Boolean = false,
    val backupAvailable: Boolean = true
)

data class InstallationIntegrityReport(
    val edition: GameEdition,
    val isAppDetected: Boolean,
    val detectedPath: String,
    val isNonInvasiveGuaranteed: Boolean = true,
    val areUserModsPreserved: Boolean = true,
    val texturesUntouched: Boolean = true,
    val vehiclesUntouched: Boolean = true,
    val cleoScriptsSafe: Boolean = true,
    val activeSavesCount: Int = 4,
    val safetyStatusNote: String = "Non-Invasive Bridge Active: Local game archives (.obb, .txd, .dff) are left 100% unaltered. Real-time telemetry is synced via external companion memory socket."
)
