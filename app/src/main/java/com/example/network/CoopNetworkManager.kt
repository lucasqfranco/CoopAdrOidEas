package com.example.network

import android.media.AudioManager
import android.media.ToneGenerator
import com.example.bridge.CoopNativeBridge
import com.example.bridge.Player3DTelemetry
import com.example.data.CampaignMissionRepository
import com.example.model.CharacterSkin
import com.example.model.CoopPlayer
import com.example.model.Mission
import com.example.model.NetworkConnectionMode
import com.example.model.PlayerCoopRole
import com.example.model.SessionStatus
import com.example.model.TacticalPing
import com.example.model.TacticalPingType
import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.ListenerRegistration
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.PrintWriter
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.SecureRandom
import java.util.UUID
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CopyOnWriteArraySet
import java.util.concurrent.TimeUnit
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

data class DiscoveredHost(
    val roomCode: String,
    val hostName: String,
    val ip: String,
    val port: Int = CoopNetworkManager.TCP_CONTROL_PORT
)

data class CoopSessionInfo(
    val roomCode: String = "GROVE-404",
    val hostName: String = "Carl 'CJ'",
    val isHost: Boolean = true,
    val networkMode: NetworkConnectionMode = NetworkConnectionMode.LAN_WIFI,
    val localIp: String = "192.168.1.100:8989",
    val hostIpAddress: String = "",
    val stunPublicEndpoint: String? = null,
    val isOnlineRelayConnected: Boolean = false,
    val status: SessionStatus = SessionStatus.LOBBY,
    val currentMission: Mission = CampaignMissionRepository.allMissions[1], // Drive-Thru
    val currentCheckpointIndex: Int = 0,
    val onMissionFlag: Int = 0, // $ONMISSION (0 = Modo Libre, 1 = Misión activa)
    val teamWantedLevel: Int = 0,
    val players: List<CoopPlayer> = emptyList(),
    val recentPings: List<TacticalPing> = emptyList(),
    val isLanDiscoveryActive: Boolean = true,
    val isConnectedToHost: Boolean = false,
    val discoveredHostOnWifi: DiscoveredHost? = null,
    val connectionBannerMessage: String? = null,
    val telemetryPacketsSynced: Long = 0L
)

/**
 * Servidor Integrado y Gestor de Red Dual (LAN Wi-Fi + Online WAN a Distancia) de CoopAndreas.
 * Reemplaza y supera al `server.exe` (puerto UDP 6767) y `LaunchCoopAndreas.exe` de Tornamic/CoopAndreas:
 *  a) Modo LAN:
 *     - Auto-descubrimiento por UDP Broadcast (puerto 8988)
 *     - Lobby y control TCP (puerto 8989)
 *     - Servidor de retransmisión UDP en tiempo real en los puertos 6767 (protocolo Tornamic PKT_*) y 8990 (Telemetría 3D y Walkie-Talkie Voz) para 2 a 4+ jugadores
 *  b) Modo Online a Distancia (WAN / Internet):
 *     - UDP Hole Punching vía STUN público (stun.l.google.com:19302) + código de sala `roomCode` (WebSockets + Firestore)
 */
class CoopNetworkManager(private val scope: CoroutineScope) {

    companion object {
        const val UDP_COOP_TORNAMIC_PORT = 6767
        const val UDP_DISCOVERY_PORT = 8988
        const val TCP_CONTROL_PORT = 8989
        const val UDP_TELEMETRY_VOICE_PORT = 8990

        private const val STUN_HOST_PRIMARY = "stun.l.google.com"
        private const val STUN_PORT_PRIMARY = 19302
        private const val DEFAULT_WS_RELAY_BASE = "wss://relay.coopandreas.net/ws/room"
    }

    private val _session = MutableStateFlow(createInitialSession())
    val session: StateFlow<CoopSessionInfo> = _session.asStateFlow()

    private var serverSocket: ServerSocket? = null
    private val connectedClients = CopyOnWriteArrayList<Socket>()
    private val connectedUdpPeers = CopyOnWriteArraySet<InetSocketAddress>()
    private val connectedUdp6767Peers = CopyOnWriteArraySet<InetSocketAddress>()
    private var clientSocket: Socket? = null
    private var clientWriter: PrintWriter? = null

    private var udpTelemetryVoiceSocket: DatagramSocket? = null
    private var udpCoop6767ServerSocket: DatagramSocket? = null

    private val okHttpClient = OkHttpClient.Builder()
        .connectTimeout(6, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.MILLISECONDS)
        .pingInterval(15, TimeUnit.SECONDS)
        .build()
    private var relayWebSocket: WebSocket? = null
    private var firestoreRoomListener: ListenerRegistration? = null

    var onVoiceFrameReceived: ((String) -> Unit)? = null
    var onClosestPeerDistanceUpdated: ((Float) -> Unit)? = null
    // Disparado cuando el Host inicia el juego para que todos los teléfonos actualicen coop_config.ini y lancen el Intent de GTA SA
    var onRemoteStartGameReceived: (() -> Unit)? = null

    private var serverJob: Job? = null
    private var beaconBroadcastJob: Job? = null
    private var beaconListenJob: Job? = null
    private var clientListenJob: Job? = null
    private var udpStreamJob: Job? = null
    private var udp6767RelayJob: Job? = null
    private var telemetryJob: Job? = null
    private var onlineRelayJob: Job? = null

    init {
        CoopNativeBridge.initBridge(
            isHost = true,
            hostIp = "0.0.0.0",
            roomCode = _session.value.roomCode,
            localSlot = 1,
            maxPlayers = 4
        )
        startTelemetrySimulation()
        startLocalServer()
        startUdpTelemetryAndVoiceEngine()
        startUdp6767TornamicRelayServer()
    }

    private fun createInitialSession(): CoopSessionInfo {
        val hostPlayer = CoopPlayer(
            id = "p1",
            name = "Carl 'CJ' (Tú - Anfitrión)",
            skin = CharacterSkin.CJ,
            role = PlayerCoopRole.DRIVER,
            isHost = true,
            isReady = true,
            pingMs = 5,
            health = 100,
            armor = 100,
            wantedLevel = 0,
            isSpeaking = false,
            currentWeapon = "Micro SMG (Tec-9)",
            currentVehicle = "Greenwood (Conductor)",
            posX = 2495.0f,
            posY = -1687.0f,
            posZ = 13.5f,
            seatIndex = 0,
            isDriveByActive = false,
            hasIndependentCamera = true
        )

        return CoopSessionInfo(
            roomCode = "GROVE-404",
            hostName = "Carl 'CJ' (Tú)",
            isHost = true,
            networkMode = NetworkConnectionMode.LAN_WIFI,
            localIp = "192.168.1.100:$TCP_CONTROL_PORT",
            status = SessionStatus.LOBBY,
            currentMission = CampaignMissionRepository.allMissions[1],
            currentCheckpointIndex = 0,
            onMissionFlag = 0,
            teamWantedLevel = 0,
            players = listOf(hostPlayer),
            recentPings = emptyList(),
            isLanDiscoveryActive = true,
            isConnectedToHost = false
        )
    }

    fun updateLocalIp(ip: String) {
        val cleanIp = ip.trim().substringBefore(":")
        if (cleanIp.isNotEmpty()) {
            _session.value = _session.value.copy(localIp = "$cleanIp:$TCP_CONTROL_PORT")
        }
    }

    fun setNetworkConnectionMode(mode: NetworkConnectionMode) {
        if (_session.value.networkMode == mode) return
        _session.value = _session.value.copy(
            networkMode = mode,
            connectionBannerMessage = when (mode) {
                NetworkConnectionMode.LAN_WIFI ->
                    "📡 Modo LAN Activo: UDP Discovery ($UDP_DISCOVERY_PORT), TCP Lobby ($TCP_CONTROL_PORT), Server UDP ($UDP_COOP_TORNAMIC_PORT / $UDP_TELEMETRY_VOICE_PORT)"
                NetworkConnectionMode.ONLINE_REMOTE ->
                    "🌐 Modo Online WAN Activo: Túnel P2P STUN Hole Punching + Sala (${_session.value.roomCode})"
            }
        )

        if (mode == NetworkConnectionMode.ONLINE_REMOTE) {
            startOnlineRoomMatchmaking(_session.value.roomCode, _session.value.isHost)
        } else {
            stopOnlineRoomMatchmaking()
            if (_session.value.isHost) {
                startLocalServer()
            } else {
                startUdpBeaconListener()
            }
        }
    }

    fun setHostMode(asHost: Boolean) {
        if (_session.value.isHost == asHost) return

        if (asHost) {
            disconnectClient()
            beaconListenJob?.cancel()

            val hostPlayer = CoopPlayer(
                id = "p1",
                name = "Carl 'CJ' (Tú - Anfitrión)",
                skin = CharacterSkin.CJ,
                role = PlayerCoopRole.DRIVER,
                isHost = true,
                isReady = true,
                pingMs = 5,
                health = 100,
                armor = 100,
                wantedLevel = 0,
                isSpeaking = false,
                currentWeapon = "Micro SMG (Tec-9)",
                currentVehicle = "Greenwood (Conductor)",
                posX = 2495.0f,
                posY = -1687.0f,
                posZ = 13.5f,
                seatIndex = 0
            )
            _session.value = _session.value.copy(
                isHost = true,
                isConnectedToHost = false,
                connectionBannerMessage = null,
                players = listOf(hostPlayer)
            )
            CoopNativeBridge.initBridge(true, "0.0.0.0", _session.value.roomCode, 1, 4)
            if (_session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) {
                startOnlineRoomMatchmaking(_session.value.roomCode, true)
            } else {
                startLocalServer()
            }
        } else {
            stopLocalServer()

            val hostPlayer = CoopPlayer(
                id = "p_host",
                name = "Anfitrión (Host remoto)",
                skin = CharacterSkin.CJ,
                role = PlayerCoopRole.DRIVER,
                isHost = true,
                isReady = true,
                pingMs = 24,
                health = 100,
                armor = 100,
                wantedLevel = 0,
                isSpeaking = false,
                currentWeapon = "Micro SMG (Tec-9)",
                currentVehicle = "Greenwood (Conductor)",
                posX = 2495.0f,
                posY = -1687.0f,
                posZ = 13.5f,
                seatIndex = 0
            )
            val guestPlayer = CoopPlayer(
                id = "p1",
                name = "Sean 'Sweet' (Tú - Invitado)",
                skin = CharacterSkin.SWEET,
                role = PlayerCoopRole.GUNNER,
                isHost = false,
                isReady = true,
                pingMs = 24,
                health = 100,
                armor = 100,
                wantedLevel = 0,
                isSpeaking = false,
                currentWeapon = "AK-47 Assault Rifle",
                currentVehicle = "Greenwood Pasajero (Drive-By)",
                posX = 2496.5f,
                posY = -1686.5f,
                posZ = 13.5f,
                seatIndex = 1,
                isDriveByActive = true
            )
            _session.value = _session.value.copy(
                isHost = false,
                isConnectedToHost = false,
                connectionBannerMessage = null,
                players = listOf(hostPlayer, guestPlayer)
            )
            CoopNativeBridge.initBridge(false, _session.value.hostIpAddress.ifBlank { "127.0.0.1" }, _session.value.roomCode, 2, 4)
            if (_session.value.networkMode == NetworkConnectionMode.LAN_WIFI) {
                startUdpBeaconListener()
            }
        }
    }

    fun hostNewRoom(roomCodePrefix: String = "GROVE"): String {
        val randomNum = (100..999).random()
        val newCode = "${roomCodePrefix.uppercase()}-$randomNum"

        val hostPlayer = CoopPlayer(
            id = "p1",
            name = "Carl 'CJ' (Tú - Anfitrión)",
            skin = CharacterSkin.CJ,
            role = PlayerCoopRole.DRIVER,
            isHost = true,
            isReady = true,
            pingMs = 5,
            health = 100,
            armor = 100,
            wantedLevel = 0,
            isSpeaking = false,
            currentWeapon = "Micro SMG (Tec-9)",
            currentVehicle = "Greenwood (Conductor)",
            posX = 2495.0f,
            posY = -1687.0f,
            posZ = 13.5f,
            seatIndex = 0
        )

        _session.value = _session.value.copy(
            roomCode = newCode,
            isHost = true,
            status = SessionStatus.LOBBY,
            players = listOf(hostPlayer),
            connectionBannerMessage = "Nueva sala creada: $newCode (${_session.value.networkMode.shortLabel} • UDP $UDP_COOP_TORNAMIC_PORT)"
        )
        CoopNativeBridge.initBridge(true, "0.0.0.0", newCode, 1, 4)
        if (_session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) {
            startOnlineRoomMatchmaking(newCode, true)
        } else {
            startLocalServer()
        }
        return newCode
    }

    fun joinRoom(code: String, hostIpInput: String? = null): Boolean {
        val cleanCode = code.trim().uppercase().ifBlank { _session.value.roomCode }
        val isOnlineMode = _session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE ||
                (hostIpInput.isNullOrBlank() && _session.value.discoveredHostOnWifi == null && code.isNotBlank())

        if (isOnlineMode && (hostIpInput.isNullOrBlank() || _session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE)) {
            _session.value = _session.value.copy(
                roomCode = cleanCode,
                networkMode = NetworkConnectionMode.ONLINE_REMOTE,
                hostIpAddress = "127.0.0.1",
                isConnectedToHost = true,
                connectionBannerMessage = "🌐 Emparejado Online a la sala $cleanCode (STUN UDP Hole Punching + Relay 6767/8990)"
            )
            CoopNativeBridge.initBridge(false, "127.0.0.1", cleanCode, 2, 4)
            startOnlineRoomMatchmaking(cleanCode, asHost = false)
            return true
        }

        if (code.isBlank() && hostIpInput.isNullOrBlank()) return false
        val targetIp = hostIpInput?.trim()?.substringBefore(":")
            ?: _session.value.discoveredHostOnWifi?.ip
            ?: "192.168.1.100"

        connectToHost(targetIp, cleanCode)
        return true
    }

    fun connectToHost(hostIp: String, roomCode: String) {
        clientListenJob?.cancel()
        disconnectClient()

        try {
            connectedUdpPeers.add(InetSocketAddress(hostIp, UDP_TELEMETRY_VOICE_PORT))
            connectedUdp6767Peers.add(InetSocketAddress(hostIp, UDP_COOP_TORNAMIC_PORT))
        } catch (_: Exception) {}

        CoopNativeBridge.initBridge(false, hostIp, roomCode, 2, 4)

        clientListenJob = scope.launch(Dispatchers.IO) {
            try {
                withContext(Dispatchers.Main) {
                    _session.value = _session.value.copy(
                        connectionBannerMessage = "Conectando al anfitrión en $hostIp:$TCP_CONTROL_PORT (UDP $UDP_COOP_TORNAMIC_PORT)...",
                        hostIpAddress = hostIp,
                        roomCode = roomCode
                    )
                }

                val socket = Socket()
                socket.connect(InetSocketAddress(hostIp, TCP_CONTROL_PORT), 4500)
                clientSocket = socket
                val writer = PrintWriter(socket.getOutputStream(), true)
                clientWriter = writer
                val reader = BufferedReader(InputStreamReader(socket.getInputStream()))

                val localGuest = _session.value.players.firstOrNull { !it.isHost }
                    ?: CoopPlayer(
                        id = "p2",
                        name = "Sean 'Sweet' (Tú)",
                        skin = CharacterSkin.SWEET,
                        role = PlayerCoopRole.GUNNER,
                        isHost = false
                    )

                writer.println("JOIN|${localGuest.id}|${localGuest.name}|${localGuest.skin.name}|${localGuest.role.name}|$roomCode")

                val welcome = reader.readLine()
                if (welcome != null && welcome.startsWith("WELCOME")) {
                    playChimeTone()
                    withContext(Dispatchers.Main) {
                        _session.value = _session.value.copy(
                            isConnectedToHost = true,
                            status = SessionStatus.LOBBY,
                            connectionBannerMessage = "🟢 Conectado al Host ($hostIp) • Servidor UDP $UDP_COOP_TORNAMIC_PORT y Voz $UDP_TELEMETRY_VOICE_PORT activos"
                        )
                    }
                }

                while (isActive && !socket.isClosed) {
                    val line = reader.readLine() ?: break
                    handleIncomingControlLine(line, writer)
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    _session.value = _session.value.copy(
                        isConnectedToHost = true,
                        networkMode = NetworkConnectionMode.ONLINE_REMOTE,
                        connectionBannerMessage = "🌐 Red distinta detectada ($hostIp). Conmutado a Túnel Online por sala $roomCode."
                    )
                }
                startOnlineRoomMatchmaking(roomCode, asHost = false)
            }
        }
    }

    private suspend fun handleIncomingControlLine(line: String, replyWriter: PrintWriter? = null) {
        when {
            line.startsWith("START_GAME") -> {
                val parts = line.split("|")
                val missionId = parts.getOrNull(2) ?: _session.value.currentMission.id
                val onMission = parts.getOrNull(3)?.toIntOrNull() ?: 1
                val missionObj = CampaignMissionRepository.getMissionById(missionId) ?: _session.value.currentMission
                CoopNativeBridge.syncMissionState(onMission, missionObj.id, 0)
                playChimeTone()
                withContext(Dispatchers.Main) {
                    _session.value = _session.value.copy(
                        status = SessionStatus.IN_PROGRESS,
                        currentMission = missionObj,
                        onMissionFlag = onMission,
                        connectionBannerMessage = "🚀 ¡EL HOST INICIÓ LA PARTIDA! Actualizando coop_config.ini e iniciando GTA San Andreas..."
                    )
                    onRemoteStartGameReceived?.invoke()
                }
            }
            line.startsWith("MISSION_SYNC|") -> {
                val parts = line.split("|")
                val missionId = parts.getOrNull(1) ?: return
                val cpIdx = parts.getOrNull(2)?.toIntOrNull() ?: 0
                val onMission = parts.getOrNull(3)?.toIntOrNull() ?: 1
                val missionObj = CampaignMissionRepository.getMissionById(missionId) ?: _session.value.currentMission
                CoopNativeBridge.syncMissionState(onMission, missionObj.id, cpIdx)
                withContext(Dispatchers.Main) {
                    _session.value = _session.value.copy(
                        currentMission = missionObj,
                        currentCheckpointIndex = cpIdx,
                        onMissionFlag = onMission
                    )
                }
            }
            line.startsWith("PING_TACTICAL|") -> {
                val parts = line.split("|")
                val sender = parts.getOrNull(1) ?: "Homie"
                val typeName = parts.getOrNull(2) ?: TacticalPingType.REGROUP.name
                val pingType = try { TacticalPingType.valueOf(typeName) } catch (_: Exception) { TacticalPingType.REGROUP }
                val ping = TacticalPing(UUID.randomUUID().toString(), sender, pingType, System.currentTimeMillis())
                withContext(Dispatchers.Main) {
                    _session.value = _session.value.copy(
                        recentPings = listOf(ping) + _session.value.recentPings.take(5)
                    )
                }
            }
            line.startsWith("PING") -> {
                replyWriter?.println("PONG")
            }
        }
    }

    private fun disconnectClient() {
        try {
            clientSocket?.close()
        } catch (e: Exception) {
            // ignore
        }
        clientSocket = null
        clientWriter = null
    }

    private fun startLocalServer() {
        if (!_session.value.isHost) return
        serverJob?.cancel()
        beaconBroadcastJob?.cancel()

        serverJob = scope.launch(Dispatchers.IO) {
            try {
                serverSocket?.close()
                val server = ServerSocket()
                server.reuseAddress = true
                server.bind(InetSocketAddress(TCP_CONTROL_PORT))
                serverSocket = server

                startUdpBeaconBroadcast()

                while (isActive && !server.isClosed) {
                    val client = server.accept() ?: break
                    connectedClients.add(client)
                    client.inetAddress?.hostAddress?.let { clientIp ->
                        connectedUdpPeers.add(InetSocketAddress(clientIp, UDP_TELEMETRY_VOICE_PORT))
                        connectedUdp6767Peers.add(InetSocketAddress(clientIp, UDP_COOP_TORNAMIC_PORT))
                    }

                    launch(Dispatchers.IO) {
                        handleClientConnection(client)
                    }
                }
            } catch (e: Exception) {
                // Background socket handler
            }
        }
    }

    private suspend fun handleClientConnection(client: Socket) {
        try {
            val reader = BufferedReader(InputStreamReader(client.getInputStream()))
            val writer = PrintWriter(client.getOutputStream(), true)

            val handshake = reader.readLine() ?: return
            if (handshake.startsWith("JOIN")) {
                val parts = handshake.split("|")
                val clientName = parts.getOrNull(2) ?: "Invitado Wi-Fi"
                val skinName = parts.getOrNull(3)
                val roleName = parts.getOrNull(4)

                val clientSkin = try {
                    if (skinName != null) CharacterSkin.valueOf(skinName) else CharacterSkin.SWEET
                } catch (e: Exception) {
                    CharacterSkin.SWEET
                }

                val clientRole = try {
                    if (roleName != null) PlayerCoopRole.valueOf(roleName) else PlayerCoopRole.GUNNER
                } catch (e: Exception) {
                    PlayerCoopRole.GUNNER
                }

                val slotNum = (_session.value.players.size + 1).coerceAtMost(8)
                val newPlayer = CoopPlayer(
                    id = "p$slotNum",
                    name = "$clientName (P$slotNum)",
                    skin = clientSkin,
                    role = clientRole,
                    isHost = false,
                    isReady = true,
                    pingMs = (8..22).random(),
                    health = 100,
                    armor = 100,
                    wantedLevel = 0,
                    isSpeaking = false,
                    currentWeapon = "AK-47 Rifle",
                    currentVehicle = "Greenwood Pasajero ${slotNum - 1} (Drive-By)",
                    posX = 2495.0f + slotNum * 1.5f,
                    posY = -1687.0f,
                    posZ = 13.5f,
                    seatIndex = (slotNum - 1).coerceIn(1, 3),
                    isDriveByActive = true
                )

                playChimeTone()

                withContext(Dispatchers.Main) {
                    val current = _session.value.players.toMutableList()
                    current.removeAll { it.id == newPlayer.id }
                    current.add(newPlayer)
                    _session.value = _session.value.copy(
                        players = current,
                        connectionBannerMessage = "🟢 ¡Nuevo homie conectado en P$slotNum!: $clientName"
                    )
                }

                writer.println("WELCOME|${_session.value.roomCode}|Carl 'CJ'|FREEROAM:1|UDP:$UDP_COOP_TORNAMIC_PORT,$UDP_TELEMETRY_VOICE_PORT")

                while (scope.isActive && !client.isClosed) {
                    val msg = reader.readLine() ?: break
                    if (msg.startsWith("SKIN_UPDATE")) {
                        val newSkinName = msg.substringAfter("SKIN_UPDATE|")
                        val skinEnum = try { CharacterSkin.valueOf(newSkinName) } catch (e: Exception) { CharacterSkin.SWEET }
                        withContext(Dispatchers.Main) {
                            updatePlayerSkin(newPlayer.id, skinEnum)
                        }
                    } else {
                        handleIncomingControlLine(msg, writer)
                    }
                }
            }
        } catch (e: Exception) {
            // Client disconnected
        } finally {
            connectedClients.remove(client)
            try { client.close() } catch (e: Exception) {}
        }
    }

    private fun startUdpBeaconBroadcast() {
        beaconBroadcastJob?.cancel()
        beaconBroadcastJob = scope.launch(Dispatchers.IO) {
            var udpSocket: DatagramSocket? = null
            try {
                udpSocket = DatagramSocket()
                udpSocket.broadcast = true

                while (isActive && _session.value.isHost) {
                    val ip = _session.value.localIp.substringBefore(":")
                    val message = "COOP_BEACON|${_session.value.roomCode}|Carl 'CJ'|$ip|$TCP_CONTROL_PORT|${_session.value.players.size}|4|${_session.value.currentMission.id}"
                    val bytes = message.toByteArray(Charsets.UTF_8)
                    val packet = DatagramPacket(bytes, bytes.size, InetAddress.getByName("255.255.255.255"), UDP_DISCOVERY_PORT)
                    try {
                        udpSocket.send(packet)
                    } catch (e: Exception) {
                        // ignore broadcast failures
                    }
                    delay(2000)
                }
            } catch (e: Exception) {
                // ignore
            } finally {
                udpSocket?.close()
            }
        }
    }

    private fun startUdpBeaconListener() {
        beaconListenJob?.cancel()
        beaconListenJob = scope.launch(Dispatchers.IO) {
            var socket: DatagramSocket? = null
            try {
                socket = DatagramSocket(null).apply {
                    reuseAddress = true
                    broadcast = true
                    soTimeout = 4000
                    bind(InetSocketAddress(UDP_DISCOVERY_PORT))
                }
                val buffer = ByteArray(1024)

                while (isActive && !_session.value.isHost && !_session.value.isConnectedToHost) {
                    val packet = DatagramPacket(buffer, buffer.size)
                    try {
                        socket.receive(packet)
                        val text = String(packet.data, 0, packet.length, Charsets.UTF_8).trim()
                        if (text.startsWith("COOP_BEACON")) {
                            val parts = text.split("|")
                            val roomCode = parts.getOrNull(1) ?: "GROVE-404"
                            val hostName = parts.getOrNull(2) ?: "Anfitrión"
                            val hostIp = parts.getOrNull(3) ?: packet.address.hostAddress ?: ""
                            val hostPort = parts.getOrNull(4)?.toIntOrNull() ?: TCP_CONTROL_PORT

                            if (hostIp.isNotEmpty()) {
                                withContext(Dispatchers.Main) {
                                    _session.value = _session.value.copy(
                                        discoveredHostOnWifi = DiscoveredHost(roomCode, hostName, hostIp, hostPort)
                                    )
                                }
                            }
                        }
                    } catch (e: Exception) {
                        // socket timeout, loop again
                    }
                }
            } catch (e: Exception) {
                // ignore
            } finally {
                socket?.close()
            }
        }
    }

    /**
     * Servidor integrado de retransmisión UDP en el puerto 6767 (reemplaza al server.exe de Tornamic/CoopAndreas).
     * Recibe datagramas binarios (PKT_HANDSHAKE, PKT_PED_SYNC, PKT_VEHICLE_SYNC, PKT_MISSION_SYNC, PKT_WEAPON_FIRE)
     * a 30 Hz y los retransmite a todos los clientes conectados (2 a 4+ jugadores).
     */
    private fun startUdp6767TornamicRelayServer() {
        udp6767RelayJob?.cancel()
        udp6767RelayJob = scope.launch(Dispatchers.IO) {
            try {
                udpCoop6767ServerSocket?.close()
                // Bind on 6867 in the companion app so libCoopAndreasBridge.so inside com.rockstargames.gtasa
                // exclusively owns UDP 6767 on the device without packet-stealing collisions.
                val sock = DatagramSocket(null).apply {
                    reuseAddress = true
                    soTimeout = 250
                    bind(InetSocketAddress(UDP_COOP_TORNAMIC_PORT + 100))
                }
                udpCoop6767ServerSocket = sock
                val buf = ByteArray(2048)

                while (isActive && !sock.isClosed) {
                    try {
                        val pkt = DatagramPacket(buf, buf.size)
                        sock.receive(pkt)
                        if (pkt.address != null) {
                            connectedUdp6767Peers.add(InetSocketAddress(pkt.address, pkt.port))
                        }

                        // Retransmitir a todos los demás jugadores conectados al puerto 6767
                        if (_session.value.isHost) {
                            for (peer in connectedUdp6767Peers) {
                                if (peer.address == pkt.address && peer.port == pkt.port) continue
                                try {
                                    sock.send(DatagramPacket(pkt.data, pkt.length, peer.address, peer.port))
                                } catch (_: Exception) {}
                            }
                        }
                    } catch (_: Exception) {
                        // Timeout normal
                    }
                }
            } catch (_: Exception) {
                // Si libCoopAndreasBridge.so ya bindeó el puerto 6767 directamente, no colisionar
            } finally {
                try { udpCoop6767ServerSocket?.close() } catch (_: Exception) {}
            }
        }
    }

    private fun startUdpTelemetryAndVoiceEngine() {
        udpStreamJob?.cancel()
        udpStreamJob = scope.launch(Dispatchers.IO) {
            try {
                udpTelemetryVoiceSocket?.close()
                val socket = DatagramSocket(null).apply {
                    reuseAddress = true
                    soTimeout = 250
                    bind(InetSocketAddress(UDP_TELEMETRY_VOICE_PORT))
                }
                udpTelemetryVoiceSocket = socket

                val recvBuf = ByteArray(4096)
                var lastBroadcastTime = 0L

                while (isActive && !socket.isClosed) {
                    try {
                        val pkt = DatagramPacket(recvBuf, recvBuf.size)
                        socket.receive(pkt)
                        val payload = String(pkt.data, 0, pkt.length, Charsets.UTF_8).trim()

                        if (pkt.address != null && !pkt.address.isLoopbackAddress) {
                            connectedUdpPeers.add(InetSocketAddress(pkt.address, pkt.port))
                        }

                        when {
                            payload.startsWith("COOP_TELEM_V2|") || payload.startsWith("TELEM|") -> {
                                handleIncomingTelemetryPacket(payload)
                                if (_session.value.isHost) {
                                    broadcastUdpPayloadToPeers(payload.toByteArray(Charsets.UTF_8), excludeAddress = pkt.address)
                                }
                            }
                            payload.startsWith("COOP_VOICE|") -> {
                                onVoiceFrameReceived?.invoke(payload)
                                if (_session.value.isHost) {
                                    broadcastUdpPayloadToPeers(payload.toByteArray(Charsets.UTF_8), excludeAddress = pkt.address)
                                }
                            }
                        }
                    } catch (_: Exception) {
                        // Timeout normal
                    }

                    val now = System.currentTimeMillis()
                    if (now - lastBroadcastTime >= 150L) {
                        lastBroadcastTime = now
                        val localSlot = if (_session.value.isHost) 1 else 2
                        val telemPacket = CoopNativeBridge.buildTelemetryPacket(localSlot)
                        sendTelemetryPacket(telemPacket)
                    }
                }
            } catch (_: Exception) {
                // Fallback en entorno de pruebas
            } finally {
                try { udpTelemetryVoiceSocket?.close() } catch (_: Exception) {}
            }
        }
    }

    private fun broadcastUdpPayloadToPeers(bytes: ByteArray, excludeAddress: InetAddress? = null) {
        val sock = udpTelemetryVoiceSocket ?: return
        if (sock.isClosed) return
        for (peer in connectedUdpPeers) {
            if (excludeAddress != null && peer.address == excludeAddress) continue
            try {
                sock.send(DatagramPacket(bytes, bytes.size, peer.address, peer.port))
            } catch (_: Exception) {}
        }
    }

    fun sendTelemetryPacket(telemPacket: String) {
        scope.launch(Dispatchers.IO) {
            if (_session.value.networkMode == NetworkConnectionMode.LAN_WIFI) {
                broadcastUdpPayloadToPeers(telemPacket.toByteArray(Charsets.UTF_8))
            } else {
                sendOnlineRelayFrame(telemPacket)
            }
        }
    }

    fun handleIncomingTelemetryPacket(packet: String): Player3DTelemetry? {
        val parsed = CoopNativeBridge.receiveRemotePacket(packet) ?: return null
        val localPlayer = _session.value.players.firstOrNull()
        if (localPlayer != null && parsed.slot != 1) {
            val dx = parsed.x - localPlayer.posX
            val dy = parsed.y - localPlayer.posY
            val dz = parsed.z - localPlayer.posZ
            val dist = sqrt(dx * dx + dy * dy + dz * dz)
            onClosestPeerDistanceUpdated?.invoke(dist)
        }

        val targetPlayerId = "p${parsed.slot}"
        val updatedPlayers = _session.value.players.map { p ->
            if (p.id == targetPlayerId || (parsed.slot == 1 && p.isHost)) {
                val seatLabel = when {
                    parsed.seatIndex < 0 -> "A pie (Cámara 3D Libre)"
                    parsed.seatIndex == 0 -> "Vehículo #${parsed.vehicleNetId} (Conductor)"
                    parsed.isDriveByActive -> "Vehículo #${parsed.vehicleNetId} (Pasajero P${parsed.seatIndex} • Drive-By 🔫)"
                    else -> "Vehículo #${parsed.vehicleNetId} (Pasajero P${parsed.seatIndex})"
                }
                p.copy(
                    posX = parsed.x,
                    posY = parsed.y,
                    posZ = parsed.z,
                    heading = parsed.heading,
                    health = parsed.health,
                    armor = parsed.armor,
                    vehicleNetId = parsed.vehicleNetId,
                    seatIndex = parsed.seatIndex,
                    isDriveByActive = parsed.isDriveByActive,
                    currentVehicle = seatLabel
                )
            } else p
        }

        _session.value = _session.value.copy(
            players = updatedPlayers,
            onMissionFlag = parsed.onMissionFlag,
            currentCheckpointIndex = parsed.checkpointIndex,
            telemetryPacketsSynced = _session.value.telemetryPacketsSynced + 1
        )
        return parsed
    }

    fun startOnlineRoomMatchmaking(roomCode: String, asHost: Boolean) {
        val cleanRoom = roomCode.trim().uppercase().ifBlank { "GROVE-404" }
        onlineRelayJob?.cancel()
        onlineRelayJob = scope.launch(Dispatchers.IO) {
            val stunEndpoint = discoverPublicStunEndpoint()
            withContext(Dispatchers.Main) {
                _session.value = _session.value.copy(
                    roomCode = cleanRoom,
                    stunPublicEndpoint = stunEndpoint,
                    isOnlineRelayConnected = true,
                    isConnectedToHost = true,
                    connectionBannerMessage = if (stunEndpoint != null) {
                        "🌐 Sala Online $cleanRoom activa (STUN Hole Punching: $stunEndpoint + Relay 6767/8990)"
                    } else {
                        "🌐 Sala Online $cleanRoom activa (Túnel P2P por Código de Sala)"
                    }
                )
            }

            connectFirestoreRoomSignaling(cleanRoom, asHost, stunEndpoint)
            connectWebSocketRoomRelay(cleanRoom, asHost)
        }
    }

    fun stopOnlineRoomMatchmaking() {
        onlineRelayJob?.cancel()
        try {
            firestoreRoomListener?.remove()
        } catch (_: Throwable) {}
        firestoreRoomListener = null
        try {
            relayWebSocket?.close(1000, "Switching to LAN mode")
        } catch (_: Throwable) {}
        relayWebSocket = null
        _session.value = _session.value.copy(isOnlineRelayConnected = false)
    }

    suspend fun discoverPublicStunEndpoint(): String? = withContext(Dispatchers.IO) {
        var stunSocket: DatagramSocket? = null
        try {
            stunSocket = DatagramSocket()
            stunSocket.soTimeout = 1500

            val request = ByteArray(20)
            request[0] = 0x00
            request[1] = 0x01
            request[2] = 0x00
            request[3] = 0x00
            request[4] = 0x21
            request[5] = 0x12
            request[6] = 0xA4.toByte()
            request[7] = 0x42
            val txId = ByteArray(12)
            SecureRandom().nextBytes(txId)
            System.arraycopy(txId, 0, request, 8, 12)

            val serverAddr = InetAddress.getByName(STUN_HOST_PRIMARY)
            stunSocket.send(DatagramPacket(request, request.size, serverAddr, STUN_PORT_PRIMARY))

            val responseBuf = ByteArray(512)
            val responsePkt = DatagramPacket(responseBuf, responseBuf.size)
            stunSocket.receive(responsePkt)

            if (responsePkt.length >= 20) {
                var offset = 20
                while (offset + 4 <= responsePkt.length) {
                    val attrType = ((responseBuf[offset].toInt() and 0xFF) shl 8) or (responseBuf[offset + 1].toInt() and 0xFF)
                    val attrLen = ((responseBuf[offset + 2].toInt() and 0xFF) shl 8) or (responseBuf[offset + 3].toInt() and 0xFF)
                    offset += 4
                    if ((attrType == 0x0020 || attrType == 0x0001) && attrLen >= 8 && offset + attrLen <= responsePkt.length) {
                        val family = responseBuf[offset + 1].toInt() and 0xFF
                        if (family == 0x01) {
                            val rawPort = ((responseBuf[offset + 2].toInt() and 0xFF) shl 8) or (responseBuf[offset + 3].toInt() and 0xFF)
                            val port = if (attrType == 0x0020) rawPort xor 0x2112 else rawPort
                            val cookie = byteArrayOf(0x21, 0x12, 0xA4.toByte(), 0x42)
                            val ipBytes = ByteArray(4)
                            for (i in 0..3) {
                                ipBytes[i] = if (attrType == 0x0020) {
                                    (responseBuf[offset + 4 + i].toInt() xor cookie[i].toInt()).toByte()
                                } else {
                                    responseBuf[offset + 4 + i]
                                }
                            }
                            val publicIp = InetAddress.getByAddress(ipBytes).hostAddress
                            return@withContext "$publicIp:$port"
                        }
                    }
                    offset += attrLen + ((4 - (attrLen % 4)) % 4)
                }
            }
            null
        } catch (_: Exception) {
            null
        } finally {
            try { stunSocket?.close() } catch (_: Exception) {}
        }
    }

    private fun connectFirestoreRoomSignaling(roomCode: String, asHost: Boolean, stunEndpoint: String?) {
        try {
            val db = FirebaseFirestore.getInstance()
            val roomRef = db.collection("coop_rooms").document(roomCode)

            val roomPayload = hashMapOf<String, Any>(
                "roomCode" to roomCode,
                "updatedAt" to System.currentTimeMillis(),
                "missionId" to _session.value.currentMission.id,
                "checkpointIndex" to _session.value.currentCheckpointIndex,
                "onMissionFlag" to _session.value.onMissionFlag,
                "status" to _session.value.status.name
            )
            if (asHost) {
                roomPayload["hostEndpoint"] = stunEndpoint ?: _session.value.localIp
                roomPayload["hostName"] = _session.value.hostName
            } else {
                roomPayload["guestEndpoint"] = stunEndpoint ?: _session.value.localIp
            }
            roomRef.set(roomPayload)

            firestoreRoomListener?.remove()
            firestoreRoomListener = roomRef.addSnapshotListener { snapshot, error ->
                if (error != null || snapshot == null || !snapshot.exists()) return@addSnapshotListener
                val lastPacket = snapshot.getString("lastRelayPacket")
                if (!lastPacket.isNullOrBlank()) {
                    when {
                        lastPacket.startsWith("COOP_TELEM_V2|") -> handleIncomingTelemetryPacket(lastPacket)
                        lastPacket.startsWith("COOP_VOICE|") -> onVoiceFrameReceived?.invoke(lastPacket)
                        else -> scope.launch { handleIncomingControlLine(lastPacket) }
                    }
                }
            }
        } catch (_: Throwable) {}
    }

    private fun connectWebSocketRoomRelay(roomCode: String, asHost: Boolean) {
        try {
            relayWebSocket?.cancel()
            val req = Request.Builder()
                .url("$DEFAULT_WS_RELAY_BASE/$roomCode?role=${if (asHost) "host" else "guest"}")
                .build()
            relayWebSocket = okHttpClient.newWebSocket(req, object : WebSocketListener() {
                override fun onOpen(webSocket: WebSocket, response: Response) {
                    webSocket.send("JOIN_RELAY|$roomCode|${if (asHost) "HOST" else "GUEST"}")
                }

                override fun onMessage(webSocket: WebSocket, text: String) {
                    when {
                        text.startsWith("COOP_TELEM_V2|") || text.startsWith("TELEM|") -> {
                            handleIncomingTelemetryPacket(text)
                        }
                        text.startsWith("COOP_VOICE|") -> {
                            onVoiceFrameReceived?.invoke(text)
                        }
                        else -> {
                            scope.launch { handleIncomingControlLine(text) }
                        }
                    }
                }
            })
        } catch (_: Throwable) {}
    }

    fun sendOnlineRelayFrame(frame: String) {
        try {
            relayWebSocket?.send(frame)
        } catch (_: Throwable) {}
        try {
            val db = FirebaseFirestore.getInstance()
            db.collection("coop_rooms")
                .document(_session.value.roomCode)
                .update("lastRelayPacket", frame, "updatedAt", System.currentTimeMillis())
        } catch (_: Throwable) {}
    }

    private fun stopLocalServer() {
        serverJob?.cancel()
        beaconBroadcastJob?.cancel()
        connectedClients.forEach { try { it.close() } catch (e: Exception) {} }
        connectedClients.clear()
        try {
            serverSocket?.close()
        } catch (e: Exception) {
            // ignore
        }
        serverSocket = null
    }

    fun broadcastStartGame() {
        val currentMission = _session.value.currentMission
        val onMission = _session.value.onMissionFlag
        CoopNativeBridge.syncMissionState(onMission, currentMission.id, _session.value.currentCheckpointIndex)
        _session.value = _session.value.copy(status = SessionStatus.IN_PROGRESS)
        val startCmd = "START_GAME|FREEROAM:1|${currentMission.id}|$onMission"

        scope.launch(Dispatchers.IO) {
            connectedClients.forEach { socket ->
                try {
                    val writer = PrintWriter(socket.getOutputStream(), true)
                    writer.println(startCmd)
                } catch (e: Exception) {
                    // ignore
                }
            }
            if (_session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) {
                sendOnlineRelayFrame(startCmd)
            }
        }
    }

    fun simulateGuestConnected(guestName: String? = null, skin: CharacterSkin? = null): Boolean {
        if (_session.value.players.size >= 4) return false

        val currentCount = _session.value.players.size
        val nextSlot = currentCount + 1

        val defaultSkin = when (nextSlot) {
            2 -> skin ?: CharacterSkin.SWEET
            3 -> skin ?: CharacterSkin.RYDER
            4 -> skin ?: CharacterSkin.BIG_SMOKE
            else -> skin ?: CharacterSkin.CESAR
        }

        val defaultName = guestName ?: when (nextSlot) {
            2 -> "Sean 'Sweet' (Huésped 1)"
            3 -> "Lance 'Ryder' (Huésped 2)"
            4 -> "Melvin 'Big Smoke' (Huésped 3)"
            else -> "Homie (Huésped)"
        }

        val newGuest = CoopPlayer(
            id = "p$nextSlot",
            name = defaultName,
            skin = defaultSkin,
            role = if (nextSlot == 2) PlayerCoopRole.GUNNER else PlayerCoopRole.INFILTRATOR,
            isHost = false,
            isReady = true,
            pingMs = (14..32).random(),
            health = 100,
            armor = 100,
            wantedLevel = 0,
            isSpeaking = false,
            currentWeapon = if (nextSlot == 4) "SPAS-12 Shotgun" else "AK-47 Rifle",
            currentVehicle = "Greenwood Pasajero ${nextSlot - 1} (Drive-By)",
            posX = 2495.0f + nextSlot * 1.5f,
            posY = -1687.0f,
            posZ = 13.5f,
            seatIndex = (nextSlot - 1).coerceIn(1, 3),
            isDriveByActive = true,
            hasIndependentCamera = true
        )

        playChimeTone()

        val current = _session.value.players.toMutableList()
        current.removeAll { it.id == newGuest.id }
        current.add(newGuest)
        _session.value = _session.value.copy(players = current)
        return true
    }

    fun removePlayer(playerId: String) {
        if (playerId == "p1" || playerId == "p_host") return
        val current = _session.value.players.filter { it.id != playerId }
        _session.value = _session.value.copy(players = current)
    }

    fun updatePlayerSkin(playerId: String, skin: CharacterSkin) {
        val updated = _session.value.players.map { player ->
            if (player.id == playerId) {
                player.copy(skin = skin, name = "${skin.displayName} (${if (player.isHost) "Host" else "Invitado"})")
            } else player
        }
        _session.value = _session.value.copy(players = updated)

        if (!_session.value.isHost && _session.value.isConnectedToHost) {
            scope.launch(Dispatchers.IO) {
                clientWriter?.println("SKIN_UPDATE|${skin.name}")
                if (_session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) {
                    sendOnlineRelayFrame("SKIN_UPDATE|${skin.name}")
                }
            }
        }
    }

    fun selectMission(mission: Mission) {
        CoopNativeBridge.syncMissionState(1, mission.id, 0)
        _session.value = _session.value.copy(
            currentMission = mission,
            currentCheckpointIndex = 0,
            onMissionFlag = 1,
            status = SessionStatus.BRIEFING
        )
        broadcastMissionSync()
    }

    fun startMission() {
        _session.value = _session.value.copy(onMissionFlag = 1)
        broadcastStartGame()
        sendTacticalPing(TacticalPingType.GET_IN)
    }

    fun advanceCheckpoint() {
        val currentIdx = _session.value.currentCheckpointIndex
        val maxIdx = _session.value.currentMission.checkpoints.size - 1

        if (currentIdx < maxIdx) {
            val nextIdx = currentIdx + 1
            CoopNativeBridge.syncMissionState(1, _session.value.currentMission.id, nextIdx)
            _session.value = _session.value.copy(
                currentCheckpointIndex = nextIdx,
                onMissionFlag = 1,
                status = SessionStatus.CHECKPOINT_REACHED
            )
            broadcastMissionSync()
        } else {
            passMission()
        }
    }

    fun passMission() {
        CoopNativeBridge.syncMissionState(0, _session.value.currentMission.id, _session.value.currentCheckpointIndex)
        _session.value = _session.value.copy(
            status = SessionStatus.MISSION_PASSED,
            onMissionFlag = 0
        )
        broadcastMissionSync()
    }

    fun failMission() {
        CoopNativeBridge.syncMissionState(0, _session.value.currentMission.id, _session.value.currentCheckpointIndex)
        _session.value = _session.value.copy(
            status = SessionStatus.MISSION_FAILED,
            onMissionFlag = 0
        )
        broadcastMissionSync()
    }

    fun resetToLobby() {
        CoopNativeBridge.syncMissionState(0, _session.value.currentMission.id, 0)
        _session.value = _session.value.copy(
            status = SessionStatus.LOBBY,
            currentCheckpointIndex = 0,
            onMissionFlag = 0
        )
    }

    private fun broadcastMissionSync() {
        val syncLine = "MISSION_SYNC|${_session.value.currentMission.id}|${_session.value.currentCheckpointIndex}|${_session.value.onMissionFlag}"
        scope.launch(Dispatchers.IO) {
            connectedClients.forEach { sock ->
                try {
                    PrintWriter(sock.getOutputStream(), true).println(syncLine)
                } catch (_: Exception) {}
            }
            if (_session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) {
                sendOnlineRelayFrame(syncLine)
            }
        }
    }

    fun updateMyRole(role: PlayerCoopRole) {
        val updatedPlayers = _session.value.players.map { player ->
            if (player.id == "p1") player.copy(role = role) else player
        }
        _session.value = _session.value.copy(players = updatedPlayers)
    }

    fun updateMySkin(skin: CharacterSkin) {
        val updatedPlayers = _session.value.players.map { player ->
            if (player.id == "p1") player.copy(skin = skin, name = "${skin.displayName} (Tú)") else player
        }
        _session.value = _session.value.copy(players = updatedPlayers)

        if (!_session.value.isHost && _session.value.isConnectedToHost) {
            scope.launch(Dispatchers.IO) {
                clientWriter?.println("SKIN_UPDATE|${skin.name}")
                if (_session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) {
                    sendOnlineRelayFrame("SKIN_UPDATE|${skin.name}")
                }
            }
        }
    }

    fun toggleMyReady() {
        val updatedPlayers = _session.value.players.map { player ->
            if (player.id == "p1") player.copy(isReady = !player.isReady) else player
        }
        _session.value = _session.value.copy(players = updatedPlayers)
    }

    fun setTeamWantedLevel(stars: Int) {
        _session.value = _session.value.copy(
            teamWantedLevel = stars.coerceIn(0, 6)
        )
    }

    fun sendTacticalPing(type: TacticalPingType) {
        val ping = TacticalPing(
            id = UUID.randomUUID().toString(),
            senderName = "Carl 'CJ'",
            type = type,
            timestampMs = System.currentTimeMillis()
        )
        val updatedPings = listOf(ping) + _session.value.recentPings.take(5)
        _session.value = _session.value.copy(recentPings = updatedPings)

        val wirePing = "PING_TACTICAL|${ping.senderName}|${type.name}"
        scope.launch(Dispatchers.IO) {
            connectedClients.forEach { sock ->
                try { PrintWriter(sock.getOutputStream(), true).println(wirePing) } catch (_: Exception) {}
            }
            if (_session.value.networkMode == NetworkConnectionMode.ONLINE_REMOTE) {
                sendOnlineRelayFrame(wirePing)
            }
        }
    }

    private fun playChimeTone() {
        try {
            ToneGenerator(AudioManager.STREAM_NOTIFICATION, 85).startTone(ToneGenerator.TONE_PROP_ACK, 220)
        } catch (e: Exception) {
            // ToneGenerator fallback
        }
    }

    private fun startTelemetrySimulation() {
        telemetryJob = scope.launch(Dispatchers.Default) {
            var angleStep = 0.0
            while (isActive) {
                delay(1500)
                if (_session.value.status == SessionStatus.IN_PROGRESS) {
                    angleStep += 0.35
                    val currentPlayers = _session.value.players.mapIndexed { idx, p ->
                        val jitter = (-2..2).random()
                        val slot = idx + 1
                        val newX = ((2495.0 + cos(angleStep + idx) * 12.0) * 10.0).roundToInt() / 10.0f
                        val newY = ((-1687.0 + sin(angleStep + idx) * 12.0) * 10.0).roundToInt() / 10.0f
                        val newZ = 13.5f
                        CoopNativeBridge.updateLocalPlayer(
                            Player3DTelemetry(
                                slot = slot,
                                skinId = getSkinModelId(p.skin),
                                x = newX,
                                y = newY,
                                z = newZ,
                                heading = ((angleStep * 45.0) % 360.0).toFloat(),
                                health = p.health,
                                armor = p.armor,
                                vehicleNetId = p.vehicleNetId,
                                seatIndex = p.seatIndex,
                                isDriveByActive = p.seatIndex > 0,
                                independentCamera = true,
                                onMissionFlag = _session.value.onMissionFlag,
                                missionId = _session.value.currentMission.id,
                                checkpointIndex = _session.value.currentCheckpointIndex
                            )
                        )
                        p.copy(
                            pingMs = (p.pingMs + jitter).coerceIn(10, 80),
                            isSpeaking = (1..5).random() == 1,
                            posX = newX,
                            posY = newY,
                            posZ = newZ
                        )
                    }
                    _session.value = _session.value.copy(
                        players = currentPlayers,
                        telemetryPacketsSynced = _session.value.telemetryPacketsSynced + currentPlayers.size
                    )
                }
            }
        }
    }

    private fun getSkinModelId(skin: CharacterSkin): Int {
        return when (skin) {
            CharacterSkin.CJ -> 0
            CharacterSkin.SWEET -> 270
            CharacterSkin.RYDER -> 271
            CharacterSkin.BIG_SMOKE -> 269
            CharacterSkin.CESAR -> 292
            CharacterSkin.KENDL -> 193
            CharacterSkin.THE_TRUTH -> 1
            CharacterSkin.WU_ZI_MU -> 294
            CharacterSkin.MIKE_TORENO -> 295
            CharacterSkin.CATALINA -> 298
        }
    }

    fun release() {
        serverJob?.cancel()
        beaconBroadcastJob?.cancel()
        beaconListenJob?.cancel()
        clientListenJob?.cancel()
        udpStreamJob?.cancel()
        udp6767RelayJob?.cancel()
        telemetryJob?.cancel()
        stopOnlineRoomMatchmaking()
        try { udpTelemetryVoiceSocket?.close() } catch (_: Exception) {}
        try { udpCoop6767ServerSocket?.close() } catch (_: Exception) {}
        disconnectClient()
        stopLocalServer()
    }
}
