package com.example

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.example.bridge.CoopNativeBridge
import com.example.bridge.Player3DTelemetry
import com.example.installer.DiagnosticItemStatus
import com.example.installer.DiagnosticVerdict
import com.example.installer.ModDiagnosticEngine
import com.example.installer.ModFileManager
import com.example.installer.ModInstallationStatus
import com.example.model.CharacterSkin
import com.example.model.NetworkConnectionMode
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [36])
class ModDiagnosticEngineTest {

    @Test
    fun testModFileGenerationAndIntegrity() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val manager = ModFileManager(context)

        val success = manager.installModFiles()
        assertTrue("Mod files should be generated successfully", success)

        val modDir = File(context.getExternalFilesDir(null), "CoopAndreas_Mod")
        assertTrue("Mod folder must exist", modDir.exists())

        val required = listOf(
            "coop_andreas_main.csa",
            "coop_mission_sync.csa",
            "coop_freeroam_bridge.csa",
            "coop_campaign.csa",
            "coop_cam.csa",
            "coop_spawner.csa",
            "coop_config.ini",
            "SAMP/settings.ini",
            "coop_telemetry.bin",
            "libCoopAndreasBridge.so",
            "coop_texts.fxt",
            "GTASAsf8.b"
        )

        for (filename in required) {
            val f = File(modDir, filename)
            assertTrue("File $filename must exist", f.exists())
            assertTrue("File $filename must not be 0 bytes", f.length() > 0)
        }

        val ini = File(modDir, "coop_config.ini").readText()
        assertTrue("Must contain [NETWORK]", ini.contains("[NETWORK]", ignoreCase = true))
        assertTrue("Must contain [PLAYER_IDENTITY]", ini.contains("[PLAYER_IDENTITY]", ignoreCase = true))
        assertTrue("Must contain Is_Host", ini.contains("Is_Host"))
        assertTrue("Must contain Coop_UDP_Port = 6767", ini.contains("6767"))
        assertTrue("Must contain Voice_UDP_Port = 8990", ini.contains("8990"))
        assertTrue("Must contain Discovery_UDP_Port = 8988", ini.contains("8988"))

        val sampIni = File(modDir, "SAMP/settings.ini").readText()
        assertTrue("SAMP settings.ini must contain [client]", sampIni.contains("[client]"))
        assertTrue("SAMP settings.ini must contain coop_port = 6767", sampIni.contains("coop_port = 6767"))
        assertTrue("SAMP settings.ini must contain voice_port = 8990", sampIni.contains("voice_port = 8990"))
        assertTrue("SAMP settings.ini must contain independent_camera = 1", sampIni.contains("independent_camera = 1"))
    }

    @Test
    fun testHostVsGuestAndOnlineIniConfigIntegrity() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val manager = ModFileManager(context)

        // 1. Configure as Host
        manager.setHostDeviceMode(true)
        manager.setMySkin(CharacterSkin.CJ)
        manager.setPartnerSkin(CharacterSkin.SWEET)
        manager.installModFiles()

        val modDir = File(context.getExternalFilesDir(null), "CoopAndreas_Mod")
        var ini = File(modDir, "coop_config.ini").readText()
        assertTrue("Host config must have Is_Host = 1", ini.contains("Is_Host = 1") || ini.contains("Is_Host=1"))
        assertTrue("Host config must have Role = HOST", ini.contains("Role = HOST"))

        // 2. Configure as Guest with Host IP in LAN mode
        manager.setHostDeviceMode(false)
        manager.setNetworkConnectionMode(NetworkConnectionMode.LAN_WIFI)
        manager.setTargetHostIp("192.168.1.45")
        manager.setMySkin(CharacterSkin.SWEET)
        manager.setPartnerSkin(CharacterSkin.CJ)
        manager.installModFiles()

        ini = File(modDir, "coop_config.ini").readText()
        assertTrue("Guest config must have Is_Host = 0", ini.contains("Is_Host = 0") || ini.contains("Is_Host=0"))
        assertTrue("Guest config must have Role = GUEST", ini.contains("Role = GUEST"))
        assertTrue("Guest config must target Host IP", ini.contains("192.168.1.45"))

        // 3. Configure One-Click Pre-Launch Rewrite in Online Mode
        manager.rewriteConfigAndInjectBeforeLaunch(
            roomCode = "GROVE-777",
            isHost = false,
            hostIpAddress = "127.0.0.1",
            networkMode = NetworkConnectionMode.ONLINE_REMOTE,
            onMissionFlag = 1,
            activeMissionId = "ls_04"
        )
        ini = File(modDir, "coop_config.ini").readText()
        assertTrue("Must contain Room_Code = GROVE-777", ini.contains("GROVE-777"))
        assertTrue("Must contain ONLINE_REMOTE mode", ini.contains("ONLINE_REMOTE"))
        assertTrue("Must contain Current_OnMission = 1", ini.contains("Current_OnMission = 1"))
    }

    @Test
    fun testNativeBridgeCustomOpcodesAndMissionSync() {
        CoopNativeBridge.initBridge(isHost = true, hostIp = "0.0.0.0", roomCode = "GROVE-999", localSlot = 1, maxPlayers = 4)
        CoopNativeBridge.syncMissionState(onMissionFlag = 1, missionId = "ls_02", checkpointIndex = 2, cpX = 2500f, cpY = -1680f, cpZ = 14f)

        // Verify custom sa_sbl_coopandreas opcodes 0D00 - 0D04
        assertTrue(CoopNativeBridge.executeCustomOpcode(CoopNativeBridge.OPCODE_COOP_SYNC_LOCAL_PLAYER, 1, 2495f, -1687f, 13.5f))
        assertTrue(CoopNativeBridge.executeCustomOpcode(CoopNativeBridge.OPCODE_COOP_APPLY_REMOTE_PED_STATE, 2))
        assertTrue(CoopNativeBridge.executeCustomOpcode(CoopNativeBridge.OPCODE_COOP_SYNC_VEHICLE_DRIVEBY, 2))
        assertTrue(CoopNativeBridge.executeCustomOpcode(CoopNativeBridge.OPCODE_COOP_SYNC_MISSION_CHECKPOINT, 1))
        assertTrue(CoopNativeBridge.executeCustomOpcode(CoopNativeBridge.OPCODE_COOP_SYNC_WEAPON_FIRE, 2, 2510f, -1680f, 14f))

        CoopNativeBridge.updateLocalPlayer(
            Player3DTelemetry(
                slot = 2,
                skinId = 270,
                x = 2510.5f,
                y = -1675.2f,
                z = 13.8f,
                heading = 90f,
                vehicleNetId = 101,
                seatIndex = 1,
                isDriveByActive = true,
                independentCamera = true,
                onMissionFlag = 1,
                missionId = "ls_02",
                checkpointIndex = 2
            )
        )

        val packet = CoopNativeBridge.buildTelemetryPacket(2)
        assertTrue("Telemetry packet must start with COOP_TELEM_V2|", packet.startsWith("COOP_TELEM_V2|"))

        val parsed = CoopNativeBridge.receiveRemotePacket(packet)
        assertNotNull("Parsed telemetry must not be null", parsed)
        assertEquals(2, parsed?.slot)
        assertEquals(270, parsed?.skinId)
        assertEquals(true, parsed?.isDriveByActive)
        assertEquals(true, parsed?.independentCamera)
        assertEquals(1, parsed?.onMissionFlag)
        assertEquals(2, parsed?.checkpointIndex)
    }

    @Test
    fun testDiagnosticDetectsMissingHostIpForGuest() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val engine = ModDiagnosticEngine(context)

        val guestStatusNoIp = ModInstallationStatus(
            isHostDevice = false,
            targetHostIp = "0.0.0.0",
            localIpAddress = "192.168.1.100",
            networkConnectionMode = NetworkConnectionMode.LAN_WIFI
        )

        val report = engine.runFullDiagnostics(guestStatusNoIp)
        assertNotNull("Report must not be null", report)
        assertTrue(
            "Verdict should indicate mismatch or pending copy",
            report.verdict == DiagnosticVerdict.CONFIG_MISMATCH || report.verdict == DiagnosticVerdict.PENDING_COPY_TO_GTA
        )
    }

    @Test
    fun testDiagnosticEngineFullRun() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val manager = ModFileManager(context)
        manager.executeOneClickInstall(roomCode = "GROVE-404", isHost = true)

        val report = manager.runAutoDiagnostics()
        assertNotNull("Diagnostic report should be created", report)
        assertTrue("Timestamp should not be empty", report.timestamp.isNotEmpty())
        assertTrue("Checks list should not be empty", report.checks.isNotEmpty())
        assertTrue("File integrity details should exist", report.fileIntegrityDetails.isNotEmpty())

        val genCheck = report.checks.firstOrNull { it.title.contains("2. Archivos") }
        assertNotNull("Must contain step 2 check", genCheck)
        assertEquals(DiagnosticItemStatus.SUCCESS, genCheck?.status)
    }
}
