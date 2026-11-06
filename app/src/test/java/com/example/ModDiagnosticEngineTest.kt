package com.example

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.example.bridge.CheatSyncCategory
import com.example.bridge.CoopNativeBridge
import com.example.data.CampaignMissionRepository
import com.example.model.SessionStatus
import com.example.network.CoopNetworkManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import java.nio.ByteBuffer
import java.nio.ByteOrder
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
@Config(sdk = [34])
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

    @Test
    fun testSkinSelectionAndCustomSkinIdReflectedInIniTelemetryAndCsaBytecode() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val manager = ModFileManager(context)

        for (skin in listOf(CharacterSkin.HOMIE_GROVE_1, CharacterSkin.SWAT_TACTICAL, CharacterSkin.RYDER)) {
            manager.setMySkin(skin)
            manager.installModFiles()

            val modDir = File(context.getExternalFilesDir(null), "CoopAndreas_Mod")
            val ini = File(modDir, "coop_config.ini").readText()
            val sampIni = File(modDir, "SAMP/settings.ini").readText()
            assertTrue("coop_config.ini must contain Player1_Skin_ID = ${skin.modelId}", ini.contains("Player1_Skin_ID = ${skin.modelId}"))
            assertTrue("SAMP/settings.ini must contain skin_id = ${skin.modelId}", sampIni.contains("skin_id = ${skin.modelId}"))

            val binBytes = File(modDir, "coop_telemetry.bin").readBytes()
            val buf = ByteBuffer.wrap(binBytes).order(ByteOrder.LITTLE_ENDIAN)
            assertEquals("Telemetry slot 1 skinId must match ${skin.modelId}", skin.modelId, buf.getInt(28))

            val csaBytes = File(modDir, "coop_andreas_main.csa").readBytes()
            val expectedModelLo = (skin.modelId and 0xFF).toByte()
            val expectedModelHi = ((skin.modelId ushr 8) and 0xFF).toByte()
            var found09C7WithSkin = false
            for (i in 0 until csaBytes.size - 6) {
                if (csaBytes[i] == 0xC7.toByte() && csaBytes[i + 1] == 0x09.toByte() &&
                    csaBytes[i + 2] == 0x04.toByte() && csaBytes[i + 3] == 0x00.toByte() &&
                    csaBytes[i + 4] == 0x05.toByte() &&
                    csaBytes[i + 5] == expectedModelLo && csaBytes[i + 6] == expectedModelHi
                ) {
                    found09C7WithSkin = true
                    break
                }
            }
            assertTrue("Bytecode .csa must contain opcode 09C7 with modelId=${skin.modelId}", found09C7WithSkin)
        }

        for (customId in listOf(105, 285)) {
            manager.setCustomSkinId(customId)
            manager.installModFiles()

            val modDir = File(context.getExternalFilesDir(null), "CoopAndreas_Mod")
            val ini = File(modDir, "coop_config.ini").readText()
            val sampIni = File(modDir, "SAMP/settings.ini").readText()
            assertTrue("coop_config.ini must contain Player1_Skin_ID = $customId", ini.contains("Player1_Skin_ID = $customId"))
            assertTrue("SAMP/settings.ini must contain skin_id = $customId", sampIni.contains("skin_id = $customId"))

            val binBytes = File(modDir, "coop_telemetry.bin").readBytes()
            val buf = ByteBuffer.wrap(binBytes).order(ByteOrder.LITTLE_ENDIAN)
            assertEquals("Telemetry slot 1 skinId must match customSkinId=$customId", customId, buf.getInt(28))

            val csaBytes = File(modDir, "coop_andreas_main.csa").readBytes()
            val lo = (customId and 0xFF).toByte()
            val hi = ((customId ushr 8) and 0xFF).toByte()
            var found09C7 = false
            for (i in 0 until csaBytes.size - 6) {
                if (csaBytes[i] == 0xC7.toByte() && csaBytes[i + 1] == 0x09.toByte() &&
                    csaBytes[i + 5] == lo && csaBytes[i + 6] == hi
                ) {
                    found09C7 = true
                    break
                }
            }
            assertTrue("Bytecode .csa must contain 09C7 for customSkinId=$customId", found09C7)
        }
    }

    @Test
    fun testJoinInProgressReceivesHostCoordinatesAndActiveMission() {
        val testScope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val hostNet = CoopNetworkManager(testScope)
        val targetMission = CampaignMissionRepository.allMissions.first { it.id == "ls_04" }

        hostNet.selectMission(targetMission)
        hostNet.startMission()
        CoopNativeBridge.updateLocalPlayer(
            Player3DTelemetry(
                slot = 1,
                skinId = CharacterSkin.CJ.modelId,
                x = 1812.5f,
                y = -1920.0f,
                z = 13.6f,
                onMissionFlag = 1,
                missionId = targetMission.id,
                checkpointIndex = 1
            )
        )
        CoopNativeBridge.syncMissionState(1, targetMission.id, 1, 1812.5f, -1920.0f, 13.6f)
        assertEquals(SessionStatus.IN_PROGRESS, hostNet.session.value.status)

        val guestNet = CoopNetworkManager(testScope)
        guestNet.setHostMode(false)
        val joined = guestNet.joinRoom(hostNet.session.value.roomCode, "127.0.0.1")
        assertTrue("Guest joinRoom in progress should succeed", joined)
        assertEquals(SessionStatus.IN_PROGRESS, guestNet.session.value.status)
        assertEquals(targetMission.id, guestNet.session.value.currentMission.id)
        assertEquals(1, guestNet.session.value.onMissionFlag)

        val hostPlayer = guestNet.session.value.players.first { it.isHost }
        val guestPlayer = guestNet.session.value.players.first { !it.isHost }
        assertEquals(1812.5f, hostPlayer.posX, 0.05f)
        assertEquals(-1920.0f, hostPlayer.posY, 0.05f)
        assertEquals(13.6f, hostPlayer.posZ, 0.05f)
        assertEquals(1814.0f, guestPlayer.posX, 0.05f)
        assertEquals(-1920.0f, guestPlayer.posY, 0.05f)

        hostNet.release()
        guestNet.release()
    }

    @Test
    fun testFourPlayersWantedStarsCutsceneWorldCheatAndAntiSoftlock() {
        CoopNativeBridge.initBridge(isHost = true, hostIp = "0.0.0.0", roomCode = "GROVE-4P", localSlot = 1, maxPlayers = 4)
        CoopNativeBridge.resetCutsceneVotes()

        for (slot in 1..4) {
            val rawStars = slot * 2
            CoopNativeBridge.updateLocalPlayer(
                Player3DTelemetry(
                    slot = slot,
                    skinId = 104 + slot,
                    x = 2490f + slot * 2f,
                    y = -1680f,
                    z = 14.0f,
                    wantedStars = rawStars,
                    vehicleNetId = 1001,
                    seatIndex = if (slot == 4) 4 else (slot - 1),
                    isDriveByActive = slot in 2..3,
                    isAttachedOnRoof = (slot == 4)
                )
            )
            val pkt = CoopNativeBridge.buildTelemetryPacket(slot)
            val parsed = CoopNativeBridge.receiveRemotePacket(pkt)
            assertNotNull("Slot $slot packet must parse", parsed)
            assertEquals(slot, parsed?.slot)
            assertEquals(rawStars.coerceIn(0, 6), parsed?.wantedStars)
            if (slot == 4) {
                assertTrue("Slot 4 on roof must preserve isAttachedOnRoof", parsed?.isAttachedOnRoof == true)
            }
        }

        val csPkt1 = CoopNativeBridge.buildCutsceneSyncPacket(slot = 1, cutsceneActive = true)
        val csPkt2 = CoopNativeBridge.buildCutsceneSyncPacket(slot = 2, cutsceneActive = true)
        assertTrue(csPkt1.startsWith("PKT_CUTSCENE_SYNC|"))
        val maskAfter2 = CoopNativeBridge.receiveCutsceneSyncPacket(csPkt2)
        assertEquals(0x03, maskAfter2 and 0x03)

        assertEquals(CheatSyncCategory.LOCAL_ONLY, CoopNativeBridge.classifyAndSyncCheat("HESOYAM", 1))
        val worldCheatPkt = CoopNativeBridge.buildWorldCheatSyncPacket(slot = 1, cheatCode = "PLEASANTLYWARM")
        assertTrue(worldCheatPkt.startsWith("PKT_WORLD_CHEAT_SYNC|"))
        assertEquals(CheatSyncCategory.WORLD_SHARED, CoopNativeBridge.receiveWorldCheatSyncPacket(worldCheatPkt))
        assertEquals("PLEASANTLYWARM", CoopNativeBridge.getLastSyncedWorldCheat())

        val zBefore = CoopNativeBridge.getSlotTelemetry(1)?.z ?: 14.0f
        assertTrue(CoopNativeBridge.triggerAntiSoftlock(1))
        val after = CoopNativeBridge.getSlotTelemetry(1)
        assertNotNull(after)
        assertEquals(zBefore + 1.2f, after!!.z, 0.01f)
        assertTrue(after.independentCamera)
    }
}
