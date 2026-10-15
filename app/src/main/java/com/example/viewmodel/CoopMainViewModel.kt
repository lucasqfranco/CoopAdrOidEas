package com.example.viewmodel

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.audio.VoiceChatEngine
import com.example.bridge.CoopNativeBridge
import com.example.data.GameInstallManager
import com.example.installer.DiagnosticVerdict
import com.example.installer.ModFileManager
import com.example.model.CampaignAct
import com.example.model.CharacterSkin
import com.example.model.GameEdition
import com.example.model.Mission
import com.example.model.NetworkConnectionMode
import com.example.model.PlayerCoopRole
import com.example.model.RadioFilterEffect
import com.example.model.TacticalPingType
import com.example.model.VoiceTransmissionMode
import com.example.network.CoopNetworkManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class AppNavTab(val title: String, val iconName: String) {
    LOBBY("Lobby Co-op", "hub"),
    CAMPAIGN("Campaña", "map"),
    VOICE("Radio Comms", "mic"),
    INSTALLATION("Instalación", "settings")
}

class CoopMainViewModel(application: Application) : AndroidViewModel(application) {

    val gameInstallManager = GameInstallManager(application)
    val voiceChatEngine = VoiceChatEngine(application, viewModelScope)
    val coopNetworkManager = CoopNetworkManager(viewModelScope)

    val modFileManager = ModFileManager(application)
    val modStatus = modFileManager.status

    val gameFileIntegrityService = com.example.installer.GameFileIntegrityService(application)
    val fileIntegrityScan = gameFileIntegrityService.scanProgress

    val integrityReport = gameInstallManager.integrityReport
    val saveSlots = gameInstallManager.saveSlots
    val activeGameEdition = gameInstallManager.activeSelectedEdition

    val voiceState = voiceChatEngine.voiceState
    val sessionState = coopNetworkManager.session

    private val _currentTab = MutableStateFlow(AppNavTab.LOBBY)
    val currentTab: StateFlow<AppNavTab> = _currentTab.asStateFlow()

    private val _selectedAct = MutableStateFlow(CampaignAct.ACT_1)
    val selectedAct: StateFlow<CampaignAct> = _selectedAct.asStateFlow()

    private val _selectedMissionDetail = MutableStateFlow<Mission?>(null)
    val selectedMissionDetail: StateFlow<Mission?> = _selectedMissionDetail.asStateFlow()

    private val _isOverlayModeActive = MutableStateFlow(false)
    val isOverlayModeActive: StateFlow<Boolean> = _isOverlayModeActive.asStateFlow()

    private val _showPassBanner = MutableStateFlow(false)
    val showPassBanner: StateFlow<Boolean> = _showPassBanner.asStateFlow()

    private val _bannerRespect = MutableStateFlow(50)
    val bannerRespect: StateFlow<Int> = _bannerRespect.asStateFlow()

    private val _bannerCash = MutableStateFlow(2500)
    val bannerCash: StateFlow<Int> = _bannerCash.asStateFlow()

    private val _hasAudioPermission = MutableStateFlow(false)
    val hasAudioPermission: StateFlow<Boolean> = _hasAudioPermission.asStateFlow()

    private val _notificationMessage = MutableStateFlow<String?>(null)
    val notificationMessage: StateFlow<String?> = _notificationMessage.asStateFlow()

    private val _isCheckingDiagnostics = MutableStateFlow(false)
    val isCheckingDiagnostics: StateFlow<Boolean> = _isCheckingDiagnostics.asStateFlow()

    fun setNavTab(tab: AppNavTab) {
        _currentTab.value = tab
        if (tab == AppNavTab.INSTALLATION || tab == AppNavTab.LOBBY) {
            runAutoDiagnostics(silent = true)
        }
    }

    init {
        val detectedIp = modFileManager.getDeviceIpAddress()
        coopNetworkManager.updateLocalIp(detectedIp)

        // Conectar motor de voz (UDP 8990 + Relay Online) con el gestor de red P2P
        coopNetworkManager.onVoiceFrameReceived = { frame ->
            voiceChatEngine.onIncomingVoicePacket(frame)
        }
        coopNetworkManager.onClosestPeerDistanceUpdated = { distMeters ->
            voiceChatEngine.updateProximityDistance(distMeters)
        }
        // Cuando el Host toca "Empezar Co-op", el invitado actualiza coop_config.ini e inicia el Intent de GTA SA automáticamente
        coopNetworkManager.onRemoteStartGameReceived = {
            launchGame()
        }
        syncVoiceTransportConfig()

        runAutoDiagnostics(silent = true)

        viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                delay(10000)
                try {
                    modFileManager.runAutoDiagnostics()
                } catch (e: Exception) {
                    // ignore
                }
            }
        }
    }

    private fun syncVoiceTransportConfig() {
        val sess = sessionState.value
        val peerIps = buildList {
            if (sess.hostIpAddress.isNotBlank()) add(sess.hostIpAddress)
            sess.discoveredHostOnWifi?.ip?.let { add(it) }
        }
        voiceChatEngine.configureNetworkTransport(
            mode = sess.networkMode,
            roomCode = sess.roomCode,
            playerId = if (sess.isHost) "p1" else "p2",
            lanPeerIps = peerIps,
            onSendOnlineRelayFrame = { wireFrame ->
                coopNetworkManager.sendOnlineRelayFrame(wireFrame)
            }
        )
    }

    fun setNetworkConnectionMode(mode: NetworkConnectionMode) {
        coopNetworkManager.setNetworkConnectionMode(mode)
        modFileManager.setNetworkConnectionMode(mode)
        syncVoiceTransportConfig()
        showNotification("Modo de red: ${mode.title}")
    }

    fun setSelectedAct(act: CampaignAct) {
        _selectedAct.value = act
    }

    fun openMissionDetail(mission: Mission) {
        _selectedMissionDetail.value = mission
    }

    fun closeMissionDetail() {
        _selectedMissionDetail.value = null
    }

    fun setAudioPermissionGranted(granted: Boolean) {
        _hasAudioPermission.value = granted
        if (granted) {
            voiceChatEngine.startVoiceService(true)
        }
    }

    fun setLobbyRole(asHost: Boolean) {
        coopNetworkManager.setHostMode(asHost)
        modFileManager.setHostDeviceMode(asHost)
        syncVoiceTransportConfig()
        if (asHost) {
            showNotification("Modo seleccionado: 👑 ANFITRIÓN (HOST)")
        } else {
            showNotification("Modo seleccionado: 🎮 INVITADO (HUÉSPED)")
        }
    }

    fun createRoom() {
        val code = coopNetworkManager.hostNewRoom()
        modFileManager.setRoomCode(code)
        syncVoiceTransportConfig()
        showNotification("Nueva sesión creada: $code (${sessionState.value.networkMode.shortLabel})")
    }

    fun connectToDiscoveredHost() {
        val discovered = sessionState.value.discoveredHostOnWifi
        if (discovered != null) {
            joinRoom(discovered.roomCode, discovered.ip)
        } else {
            showNotification("Buscando anfitrión en la red Wi-Fi (UDP 8988)...")
        }
    }

    fun joinRoom(code: String, hostIpInput: String? = null) {
        val cleanCode = code.trim().uppercase().ifBlank { sessionState.value.roomCode }
        val cleanIp = hostIpInput?.trim()?.ifBlank { null }
            ?: sessionState.value.discoveredHostOnWifi?.ip
            ?: if (sessionState.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) "127.0.0.1" else modStatus.value.localIpAddress

        modFileManager.setRoomCode(cleanCode)
        modFileManager.setTargetHostIp(cleanIp)
        modFileManager.setHostDeviceMode(false)

        val success = coopNetworkManager.joinRoom(cleanCode, hostIpInput)
        if (success) {
            voiceChatEngine.addLanPeerEndpoint(cleanIp)
            syncVoiceTransportConfig()
            val modeDesc = if (sessionState.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) {
                "Online por Código de Sala ($cleanCode)"
            } else {
                "LAN ($cleanIp:8989 / UDP 6767 & 8990)"
            }
            showNotification("Conectado como Huésped: $modeDesc")
        } else {
            showNotification("Ingresa el código de la sala o la IP del Host")
        }
    }

    fun addGuestPlayer(guestName: String? = null, skin: CharacterSkin? = null) {
        val added = coopNetworkManager.simulateGuestConnected(guestName, skin)
        if (added) {
            val count = sessionState.value.players.size
            modFileManager.setMaxPlayers(count)
            showNotification("¡Jugador $count conectado a la sala!")
        } else {
            showNotification("La sala ya tiene el máximo de 4 jugadores")
        }
    }

    fun removeGuestPlayer(id: String) {
        coopNetworkManager.removePlayer(id)
        val count = sessionState.value.players.size
        modFileManager.setMaxPlayers(count)
        showNotification("Jugador desconectado de la sala")
    }

    fun changeGuestSkin(id: String, skin: CharacterSkin) {
        coopNetworkManager.updatePlayerSkin(id, skin)
        when (id) {
            "p2" -> modFileManager.setPartnerSkin(skin)
            "p3" -> modFileManager.setPlayer3Skin(skin)
            "p4" -> modFileManager.setPlayer4Skin(skin)
        }
        showNotification("Skin actualizado para jugador: ${skin.displayName}")
    }

    fun setSingleDeviceMode(enabled: Boolean) {
        modFileManager.setSingleDeviceMode(enabled)
        showNotification(if (enabled) "Modo 1 Dispositivo con 2 Mandos ACTIVADO" else "Modo Multidispositivo (Cámara 3D Independiente) ACTIVADO")
    }

    fun setScreenSplitType(type: String) {
        modFileManager.setScreenSplitType(type)
        showNotification("Modo de pantalla: $type")
    }

    fun setFreeRoamAnySave(enabled: Boolean) {
        modFileManager.setFreeRoamAnySave(enabled)
        showNotification(if (enabled) "Modo Libre / Cargar Cualquier Partida ACTIVADO" else "Modo Misión de Campaña (\$ONMISSION Sync) ACTIVADO")
    }

    fun setMaxPlayers(count: Int) {
        modFileManager.setMaxPlayers(count)
        showNotification("Límite de jugadores fijado en: $count")
    }

    fun setCustomHostIp(ip: String) {
        modFileManager.setCustomHostIp(ip)
        coopNetworkManager.updateLocalIp(ip)
        syncVoiceTransportConfig()
        showNotification("IP del Servidor Host actualizada: $ip")
    }

    fun changeMyRole(role: PlayerCoopRole) {
        coopNetworkManager.updateMyRole(role)
    }

    fun changeMySkin(skin: CharacterSkin) {
        coopNetworkManager.updateMySkin(skin)
        modFileManager.setMySkin(skin)
        showNotification("Skin seleccionado: ${skin.displayName}")
    }

    fun toggleReady() {
        coopNetworkManager.toggleMyReady()
    }

    fun sendPing(type: TacticalPingType) {
        coopNetworkManager.sendTacticalPing(type)
    }

    fun chooseMissionForSession(mission: Mission) {
        coopNetworkManager.selectMission(mission)
        _selectedMissionDetail.value = null
        _currentTab.value = AppNavTab.LOBBY
        showNotification("Misión sincronizada (\$ONMISSION=1): ${mission.title}")
    }

    fun startMission() {
        coopNetworkManager.startMission()
        launchGame()
    }

    fun advanceCheckpoint() {
        coopNetworkManager.advanceCheckpoint()
    }

    fun completeMission() {
        val mission = sessionState.value.currentMission
        _bannerRespect.value = mission.respectReward
        _bannerCash.value = mission.cashReward
        _showPassBanner.value = true

        coopNetworkManager.passMission()
        gameInstallManager.syncMissionProgressToSave(
            mission.title,
            mission.respectReward,
            mission.cashReward
        )
        showNotification("Progreso sincronizado en ranura GTASAsf8.b")
    }

    fun dismissPassBanner() {
        _showPassBanner.value = false
        coopNetworkManager.resetToLobby()
    }

    fun failMission() {
        coopNetworkManager.failMission()
        showNotification("Misión fallida. Reiniciando checkpoint...")
    }

    fun onPttPressed() {
        voiceChatEngine.onPttPressed()
    }

    fun onPttReleased() {
        voiceChatEngine.onPttReleased()
    }

    fun toggleMute() {
        voiceChatEngine.toggleMute()
    }

    fun toggleDeafen() {
        voiceChatEngine.toggleDeafen()
    }

    fun setTransmissionMode(mode: VoiceTransmissionMode) {
        voiceChatEngine.setTransmissionMode(mode)
    }

    fun setRadioFilter(filter: RadioFilterEffect) {
        voiceChatEngine.setRadioFilter(filter)
    }

    fun setVadThreshold(thresh: Int) {
        voiceChatEngine.setVadThreshold(thresh)
    }

    fun setMasterVolume(vol: Int) {
        voiceChatEngine.setMasterVolume(vol)
    }

    fun toggleProximity() {
        voiceChatEngine.toggleProximity()
    }

    fun selectGameEdition(edition: GameEdition) {
        gameInstallManager.selectEdition(edition)
        showNotification("Perfil de instalación: ${edition.title}")
    }

    fun backupSaveSlot(slot: Int) {
        gameInstallManager.backupSaveSlot(slot)
        showNotification("Copia de seguridad creada para Ranura $slot")
    }

    fun oneClickInstallAndLaunchGame(launchAfterInstall: Boolean = false, customSafUri: Uri? = null) {
        viewModelScope.launch(Dispatchers.IO) {
            val sess = sessionState.value
            val targetIp = if (sess.isHost) {
                modStatus.value.localIpAddress
            } else {
                sess.hostIpAddress.ifBlank { modStatus.value.targetHostIp }
            }

            val result = modFileManager.executeOneClickInstall(
                roomCode = sess.roomCode,
                isHost = sess.isHost,
                targetHostIp = targetIp,
                networkMode = sess.networkMode,
                customSafUri = customSafUri
            )

            CoopNativeBridge.initBridge(
                isHost = sess.isHost,
                hostIp = result.activeHostIp,
                roomCode = result.activeRoomCode,
                localSlot = if (sess.isHost) 1 else 2,
                maxPlayers = modStatus.value.maxPlayers
            )

            withContext(Dispatchers.Main) {
                showNotification(result.message)
                if (launchAfterInstall) {
                    if (sess.isHost) {
                        coopNetworkManager.broadcastStartGame()
                    }
                    val launched = gameInstallManager.launchGameIntent()
                    _isOverlayModeActive.value = true
                    if (!launched) {
                        showNotification("Archivos inyectados (IP/Sala reescritas). Abriendo HUD Co-op...")
                    }
                }
            }
        }
    }

    fun launchGame() {
        viewModelScope.launch(Dispatchers.IO) {
            val sess = sessionState.value
            val targetIp = if (sess.isHost) {
                modStatus.value.localIpAddress
            } else {
                sess.hostIpAddress.ifBlank { modStatus.value.targetHostIp }
            }

            modFileManager.rewriteConfigAndInjectBeforeLaunch(
                roomCode = sess.roomCode,
                isHost = sess.isHost,
                hostIpAddress = targetIp,
                networkMode = sess.networkMode,
                onMissionFlag = sess.onMissionFlag,
                activeMissionId = sess.currentMission.id
            )

            withContext(Dispatchers.Main) {
                if (sess.isHost) {
                    coopNetworkManager.broadcastStartGame()
                }
                val launched = gameInstallManager.launchGameIntent()
                _isOverlayModeActive.value = true
                if (!launched) {
                    showNotification("IP y Sala (${sess.roomCode}) inyectadas. Abriendo overlay companion...")
                }
            }
        }
    }

    fun toggleOverlayMode() {
        _isOverlayModeActive.value = !_isOverlayModeActive.value
    }

    fun installModToGame() {
        viewModelScope.launch(Dispatchers.IO) {
            val sess = sessionState.value
            val result = modFileManager.executeOneClickInstall(
                roomCode = sess.roomCode,
                isHost = sess.isHost,
                targetHostIp = sess.hostIpAddress.ifBlank { modStatus.value.targetHostIp },
                networkMode = sess.networkMode
            )
            if (result.success) {
                if (sess.isHost) {
                    coopNetworkManager.broadcastStartGame()
                }
                showNotification("¡Archivos del Mod y Puente C++ inyectados con éxito!")
            } else {
                showNotification("Error al instalar archivos del mod")
            }
        }
    }

    fun installDirectToAndroidUnprotected() {
        viewModelScope.launch(Dispatchers.IO) {
            val count = modFileManager.installDirectToAndroidUnprotected()
            if (count > 0) {
                showNotification("¡$count archivos inyectados directamente en Android_unprotected!")
            } else {
                showNotification("Archivos copiados en Android_unprotected")
            }
        }
    }

    fun autoInstallCleoAndMod(forceOffline: Boolean = false, customUrl: String? = null) {
        viewModelScope.launch(Dispatchers.IO) {
            showNotification(if (forceOffline) "⚡ Ejecutando Auto-Instalación 1 Clic..." else "🌐 Descargando e inyectando CLEO + Puente C++...")
            val result = modFileManager.downloadAndInstallCleoFull(
                customZipUrl = customUrl,
                forceOffline = forceOffline
            )
            result.onSuccess { summary ->
                showNotification("🎉 ${summary.message}")
            }.onFailure { err ->
                showNotification("Error instalando CLEO: ${err.message}")
            }
        }
    }

    fun uninstallModFromGame() {
        viewModelScope.launch(Dispatchers.IO) {
            val success = modFileManager.uninstallModFiles()
            if (success) {
                showNotification("Mod CoopAndreas desinstalado. Tus otros mods quedaron intactos.")
            } else {
                showNotification("Error al desinstalar el mod")
            }
        }
    }

    fun setMyModSkin(skin: CharacterSkin) {
        modFileManager.setMySkin(skin)
        coopNetworkManager.updateMySkin(skin)
        showNotification("Tu Skin en el juego 3D: ${skin.displayName}")
    }

    fun setPartnerModSkin(skin: CharacterSkin) {
        modFileManager.setPartnerSkin(skin)
        showNotification("Skin del compañero en el juego 3D: ${skin.displayName}")
    }

    fun setModCameraMode(mode: String) {
        modFileManager.setCameraMode(mode)
        showNotification("Modo de cámara: $mode")
    }

    fun toggleModSeparateVehicles(enabled: Boolean) {
        modFileManager.toggleSeparateVehicles(enabled)
        showNotification(if (enabled) "Vehículos independientes ACTIVADOS" else "Vehículo compartido (conductor + pasajeros drive-by)")
    }

    fun setModConnectionMode(mode: String) {
        modFileManager.setConnectionMode(mode)
        showNotification("Modo de conexión: $mode")
    }

    fun getShareModZipIntent(): android.content.Intent? {
        return modFileManager.shareModZip()
    }

    fun openInZArchiver() {
        val success = modFileManager.openZArchiverAppDirect()
        if (success) {
            showNotification("Abriendo ZArchiver directamente...")
        } else {
            showNotification("ZArchiver no detectado. Usa el botón de 1 Clic arriba.")
        }
    }

    fun isShizukuInstalled(): Boolean {
        return modFileManager.isShizukuInstalled()
    }

    fun openShizuku() {
        val success = modFileManager.openShizuku()
        if (success) {
            showNotification("Abriendo Shizuku...")
        } else {
            showNotification("Shizuku no detectado.")
        }
    }

    fun getPlayStoreZArchiverIntent(): android.content.Intent {
        return modFileManager.getPlayStoreZArchiverIntent()
    }

    fun saveToDownloads() {
        val pair = modFileManager.copyFilesAndFolderToDownloads()
        if (pair.first != null || pair.second != null) {
            showNotification("¡Listo! Carpeta 'CoopAndreas_Archivos_GTA' y ZIP guardados en Descargas.")
        } else {
            showNotification("Primero pulsa el botón de Instalación 1 Clic.")
        }
    }

    fun isZArchiverInstalled(): Boolean {
        return modFileManager.isZArchiverInstalled()
    }

    fun runAutoDiagnostics(silent: Boolean = false) {
        viewModelScope.launch(Dispatchers.IO) {
            _isCheckingDiagnostics.value = true
            try {
                val report = modFileManager.runAutoDiagnostics()
                if (!silent) {
                    val toastMsg = when (report.verdict) {
                        DiagnosticVerdict.READY_TO_PLAY -> "✅ Diagnóstico: ¡Puente C++ y Mod listos en GTA San Andreas!"
                        DiagnosticVerdict.PENDING_COPY_TO_GTA -> "⚡ Pulsa 'INSTALACIÓN 1-CLIC' para inyectar los archivos automáticamente"
                        DiagnosticVerdict.CONFIG_MISMATCH -> "⚠️ Diagnóstico: Revisa la IP del Host o usa el Modo Online por Sala"
                        DiagnosticVerdict.CLEO_MISSING_WARNING -> "ℹ️ Diagnóstico: Pulsa 1-Clic para inyectar CLEO y Bridge en mods/"
                        DiagnosticVerdict.GAME_NOT_INSTALLED -> "⚠️ Diagnóstico: No se detectó APK de GTA SA"
                    }
                    showNotification(toastMsg)
                }
            } catch (e: Exception) {
                if (!silent) {
                    showNotification("Error ejecutando diagnóstico: ${e.message}")
                }
            } finally {
                _isCheckingDiagnostics.value = false
            }
        }
    }

    fun installModToSafTree(treeUri: Uri) {
        viewModelScope.launch(Dispatchers.IO) {
            val count = modFileManager.installModToSafTree(treeUri)
            if (count > 0) {
                showNotification("¡Permiso SAF persistente guardado y $count archivos inyectados en GTA San Andreas!")
            } else {
                showNotification("Permiso SAF guardado. Pulsa Instalación 1-Clic para reintentar.")
            }
        }
    }

    fun showNotification(msg: String) {
        _notificationMessage.value = msg
    }

    fun clearNotification() {
        _notificationMessage.value = null
    }

    override fun onCleared() {
        super.onCleared()
        voiceChatEngine.release()
        coopNetworkManager.release()
    }
}
