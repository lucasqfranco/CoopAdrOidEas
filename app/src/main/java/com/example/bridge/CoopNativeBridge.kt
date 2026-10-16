package com.example.bridge

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap

data class Player3DTelemetry(
    val slot: Int = 1,
    val skinId: Int = 0,
    val x: Float = 2495.0f,
    val y: Float = -1687.0f,
    val z: Float = 13.5f,
    val heading: Float = 0.0f,
    val health: Int = 100,
    val armor: Int = 100,
    val weaponId: Int = 0,
    val vehicleNetId: Int = -1,
    val vehicleModelId: Int = 0,
    val seatIndex: Int = -1,
    val isDriveByActive: Boolean = false,
    val independentCamera: Boolean = true,
    val onMissionFlag: Int = 0,
    val missionId: String = "ls_01",
    val checkpointIndex: Int = 0
)

object CoopNativeBridge {
    const val OPCODE_COOP_SYNC_LOCAL_PLAYER = 0x0D00
    const val OPCODE_COOP_APPLY_REMOTE_PED_STATE = 0x0D01
    const val OPCODE_COOP_SYNC_VEHICLE_DRIVEBY = 0x0D02
    const val OPCODE_COOP_SYNC_MISSION_CHECKPOINT = 0x0D03
    const val OPCODE_COOP_SYNC_WEAPON_FIRE = 0x0D04

    private var isNativeLoaded = false
    private val playerSlots = ConcurrentHashMap<Int, Player3DTelemetry>()

    @Volatile private var currentIsHost: Boolean = true
    @Volatile private var currentHostIp: String = "0.0.0.0"
    @Volatile private var currentRoomCode: String = "GROVE-101"
    @Volatile private var currentLocalSlot: Int = 1
    @Volatile private var currentMaxPlayers: Int = 4
    @Volatile private var currentCameraIndependent: Boolean = true
    @Volatile private var currentOnMissionFlag: Int = 0
    @Volatile private var currentMissionId: String = "ls_01"
    @Volatile private var currentCheckpointIndex: Int = 0

    init {
        try {
            System.loadLibrary("CoopAndreasBridge")
            isNativeLoaded = true
        } catch (_: Throwable) {
            isNativeLoaded = false
        }
    }

    private external fun nativeInitBridge(
        isHost: Boolean,
        hostIp: String,
        roomCode: String,
        localSlot: Int,
        maxPlayers: Int
    ): Boolean

    private external fun nativeLoadIniConfigs(
        coopIniPath: String,
        sampIniPath: String
    ): Boolean

    private external fun nativeUpdateLocalPlayer(
        slot: Int,
        skinId: Int,
        x: Float,
        y: Float,
        z: Float,
        heading: Float,
        health: Int,
        armor: Int,
        weaponId: Int,
        vehicleNetId: Int,
        vehicleModelId: Int,
        seatIndex: Int,
        isDriveBy: Boolean
    )

    private external fun nativeExecuteCustomOpcode(
        opcode: Int,
        slot: Int,
        x: Float,
        y: Float,
        z: Float
    ): Boolean

    private external fun nativeWriteTelemetryBin(binFilePath: String): Boolean
    private external fun nativeBuildTelemetryPacket(slot: Int): String
    private external fun nativeReceiveRemotePacket(packetStr: String): String
    private external fun nativeSyncMissionState(
        onMissionFlag: Int,
        missionId: String,
        checkpointIndex: Int,
        cpX: Float,
        cpY: Float,
        cpZ: Float
    )
    private external fun nativeSetCameraIndependent(independent: Boolean)
    private external fun nativeGetPlayerStateJson(slot: Int): String
    private external fun nativeGetBridgeDiagnosticSummary(): String
    private external fun nativeShutdownBridge()

    fun initBridge(
        isHost: Boolean,
        hostIp: String,
        roomCode: String,
        localSlot: Int = 1,
        maxPlayers: Int = 4
    ): Boolean {
        currentIsHost = isHost
        currentHostIp = hostIp
        currentRoomCode = roomCode
        currentLocalSlot = localSlot
        currentMaxPlayers = maxPlayers
        if (!playerSlots.containsKey(localSlot)) {
            playerSlots[localSlot] = Player3DTelemetry(slot = localSlot)
        }
        if (isNativeLoaded) {
            try {
                return nativeInitBridge(isHost, hostIp, roomCode, localSlot, maxPlayers)
            } catch (_: Throwable) {}
        }
        return true
    }

    fun loadIniConfigs(coopIniPath: String, sampIniPath: String): Boolean {
        if (isNativeLoaded) {
            try {
                return nativeLoadIniConfigs(coopIniPath, sampIniPath)
            } catch (_: Throwable) {}
        }
        return File(coopIniPath).exists()
    }

    fun updateLocalPlayer(telemetry: Player3DTelemetry) {
        playerSlots[telemetry.slot] = telemetry
        if (isNativeLoaded) {
            try {
                nativeUpdateLocalPlayer(
                    telemetry.slot,
                    telemetry.skinId,
                    telemetry.x,
                    telemetry.y,
                    telemetry.z,
                    telemetry.heading,
                    telemetry.health,
                    telemetry.armor,
                    telemetry.weaponId,
                    telemetry.vehicleNetId,
                    telemetry.vehicleModelId,
                    telemetry.seatIndex,
                    telemetry.isDriveByActive
                )
            } catch (_: Throwable) {}
        }
    }

    fun executeCustomOpcode(
        opcode: Int,
        slot: Int,
        x: Float = 0f,
        y: Float = 0f,
        z: Float = 0f
    ): Boolean {
        if (opcode !in OPCODE_COOP_SYNC_LOCAL_PLAYER..OPCODE_COOP_SYNC_WEAPON_FIRE) {
            return false
        }
        if (isNativeLoaded) {
            try {
                return nativeExecuteCustomOpcode(opcode, slot, x, y, z)
            } catch (_: Throwable) {}
        }
        return true
    }

    fun writeTelemetryBinaryFile(targetFile: File): Boolean {
        if (isNativeLoaded) {
            try {
                if (nativeWriteTelemetryBin(targetFile.absolutePath)) {
                    return true
                }
            } catch (_: Throwable) {}
        }
        return try {
            targetFile.parentFile?.mkdirs()
            val buf = ByteBuffer.allocate(256).order(ByteOrder.LITTLE_ENDIAN)
            buf.putInt(0x504F4F43) // "COOP"
            buf.putInt(2)          // version 2
            buf.putInt(if (currentIsHost) 1 else 0)
            buf.putInt(currentOnMissionFlag)
            buf.putInt(currentCheckpointIndex)
            buf.putInt(if (currentCameraIndependent) 1 else 0)
            for (slot in 1..4) {
                val p = playerSlots[slot] ?: Player3DTelemetry(slot = slot)
                buf.putInt(p.slot)
                buf.putInt(p.skinId)
                buf.putFloat(p.x)
                buf.putFloat(p.y)
                buf.putFloat(p.z)
                buf.putFloat(p.heading)
                buf.putInt(p.health)
                buf.putInt(p.armor)
                buf.putInt(p.vehicleNetId)
                buf.putInt(p.seatIndex)
                buf.putInt(if (p.isDriveByActive) 1 else 0)
            }
            targetFile.writeBytes(buf.array())
            true
        } catch (_: Exception) {
            false
        }
    }

    fun buildTelemetryPacket(slot: Int): String {
        val p = playerSlots[slot] ?: Player3DTelemetry(
            slot = slot,
            independentCamera = currentCameraIndependent,
            onMissionFlag = currentOnMissionFlag,
            missionId = currentMissionId,
            checkpointIndex = currentCheckpointIndex
        )
        return String.format(
            Locale.US,
            "COOP_TELEM_V2|%d|%d|%.2f|%.2f|%.2f|%.1f|%d|%d|%d|%d|%d|%d|%d|%d|%s|%d",
            p.slot,
            p.skinId,
            p.x,
            p.y,
            p.z,
            p.heading,
            p.health,
            p.armor,
            p.weaponId,
            p.vehicleNetId,
            p.seatIndex,
            if (p.isDriveByActive) 1 else 0,
            if (p.independentCamera) 1 else 0,
            p.onMissionFlag,
            p.missionId,
            p.checkpointIndex
        )
    }

    fun receiveRemotePacket(packet: String): Player3DTelemetry? {
        if (!packet.startsWith("COOP_TELEM_V2|")) return null
        val parts = packet.trim().split("|")
        if (parts.size < 17) return null
        return try {
            val parsed = Player3DTelemetry(
                slot = parts[1].toIntOrNull() ?: 2,
                skinId = parts[2].toIntOrNull() ?: 0,
                x = parts[3].toFloatOrNull() ?: 2495f,
                y = parts[4].toFloatOrNull() ?: -1687f,
                z = parts[5].toFloatOrNull() ?: 13.5f,
                heading = parts[6].toFloatOrNull() ?: 0f,
                health = parts[7].toIntOrNull() ?: 100,
                armor = parts[8].toIntOrNull() ?: 100,
                weaponId = parts[9].toIntOrNull() ?: 0,
                vehicleNetId = parts[10].toIntOrNull() ?: -1,
                seatIndex = parts[11].toIntOrNull() ?: -1,
                isDriveByActive = (parts[12].toIntOrNull() ?: 0) == 1,
                independentCamera = (parts[13].toIntOrNull() ?: 1) == 1,
                onMissionFlag = parts[14].toIntOrNull() ?: 0,
                missionId = parts[15],
                checkpointIndex = parts[16].toIntOrNull() ?: 0
            )
            playerSlots[parsed.slot] = parsed
            if (isNativeLoaded) {
                try {
                    nativeReceiveRemotePacket(packet)
                } catch (_: Throwable) {}
            }
            parsed
        } catch (_: Exception) {
            null
        }
    }

    fun syncMissionState(
        onMissionFlag: Int,
        missionId: String,
        checkpointIndex: Int,
        cpX: Float = 2495f,
        cpY: Float = -1687f,
        cpZ: Float = 13.5f
    ) {
        currentOnMissionFlag = onMissionFlag
        currentMissionId = missionId
        currentCheckpointIndex = checkpointIndex
        if (isNativeLoaded) {
            try {
                nativeSyncMissionState(onMissionFlag, missionId, checkpointIndex, cpX, cpY, cpZ)
            } catch (_: Throwable) {}
        }
    }

    fun setCameraIndependent(independent: Boolean) {
        currentCameraIndependent = independent
        if (isNativeLoaded) {
            try {
                nativeSetCameraIndependent(independent)
            } catch (_: Throwable) {}
        }
    }

    fun getPlayerStateJson(slot: Int): String {
        if (isNativeLoaded) {
            try {
                return nativeGetPlayerStateJson(slot)
            } catch (_: Throwable) {}
        }
        val p = playerSlots[slot] ?: Player3DTelemetry(slot = slot)
        return """{"slot":${p.slot},"skinId":${p.skinId},"x":${p.x},"y":${p.y},"z":${p.z},"independentCamera":${p.independentCamera}}"""
    }

    fun getBridgeDiagnosticSummary(): String {
        if (isNativeLoaded) {
            try {
                return nativeGetBridgeDiagnosticSummary()
            } catch (_: Throwable) {}
        }
        return "Bridge JNI (Kotlin + NDK Fallback) | Room=$currentRoomCode | Camera3D=${if (currentCameraIndependent) "INDEPENDENT" else "SHARED"}"
    }

    fun shutdownBridge() {
        if (isNativeLoaded) {
            try {
                nativeShutdownBridge()
            } catch (_: Throwable) {}
        }
    }
}
