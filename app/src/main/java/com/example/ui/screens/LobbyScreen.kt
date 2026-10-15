package com.example.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Login
import androidx.compose.material.icons.filled.PersonAdd
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.model.CharacterSkin
import com.example.model.NetworkConnectionMode
import com.example.model.PlayerCoopRole
import com.example.model.SessionStatus
import com.example.model.TacticalPingType
import com.example.ui.components.PlayerCard
import com.example.ui.theme.ArmorBlue
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkBorder
import com.example.ui.theme.DarkSurfaceCard
import com.example.ui.theme.DarkSurfaceElevated
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.ui.theme.WantedRed
import com.example.viewmodel.AppNavTab
import com.example.viewmodel.CoopMainViewModel

@Composable
fun LobbyScreen(
    viewModel: CoopMainViewModel,
    modifier: Modifier = Modifier
) {
    val session by viewModel.sessionState.collectAsState()
    val modStatus by viewModel.modStatus.collectAsState()

    val isHost = session.isHost
    val isOnlineMode = session.networkMode == NetworkConnectionMode.ONLINE_REMOTE

    LazyColumn(
        modifier = modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp)
            .testTag("lobby_screen"),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            Spacer(modifier = Modifier.height(8.dp))

            // ==============================================================
            // SELECTOR DE MODO DE RED: LAN WI-FI VS ONLINE A DISTANCIA (Objetivo 2)
            // ==============================================================
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag("network_mode_selector_card"),
                colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
                shape = RoundedCornerShape(16.dp),
                border = androidx.compose.foundation.BorderStroke(1.5.dp, if (isOnlineMode) ArmorBlue else GroveGreen)
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Text(
                        text = "MODO DE CONEXIÓN DE RED (LAN LOCAL VS ONLINE A DISTANCIA)",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 13.sp,
                        color = Color.LightGray,
                        letterSpacing = 0.8.sp
                    )
                    Spacer(modifier = Modifier.height(6.dp))
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(10.dp))
                            .background(Color(0xFF0F1811))
                            .padding(4.dp),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (!isOnlineMode) GroveGreen else Color.Transparent)
                                .clickable { viewModel.setNetworkConnectionMode(NetworkConnectionMode.LAN_WIFI) }
                                .padding(vertical = 8.dp)
                                .testTag("mode_lan_button"),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = "📡 MODO LAN (WI-FI)",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 14.sp,
                                color = if (!isOnlineMode) Color.Black else Color.Gray
                            )
                        }
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (isOnlineMode) ArmorBlue else Color.Transparent)
                                .clickable { viewModel.setNetworkConnectionMode(NetworkConnectionMode.ONLINE_REMOTE) }
                                .padding(vertical = 8.dp)
                                .testTag("mode_online_button"),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = "🌐 ONLINE (CÓDIGO DE SALA)",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 14.sp,
                                color = if (isOnlineMode) Color.Black else Color.Gray
                            )
                        }
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    Text(
                        text = if (isOnlineMode)
                            "🌐 Modo Online Activo: Conéctate desde distintas redes Wi-Fi o datos móviles 4G/5G usando solo el código de sala (${session.roomCode}) sin abrir puertos en el router."
                        else
                            "📡 Modo LAN Activo: Auto-descubrimiento UDP (8988), Control TCP (8989) y Streaming de Telemetría 3D y Voz por UDP (8990).",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color(0xFFD1D5DB)
                    )
                }
            }
        }

        item {
            // ==============================================================
            // SELECTOR PRINCIPAL: ROL DE ESTE DISPOSITIVO (HOST VS INVITADO)
            // ==============================================================
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag("role_switcher_card"),
                colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
                shape = RoundedCornerShape(16.dp),
                border = androidx.compose.foundation.BorderStroke(1.5.dp, if (isHost) SanAndreasGold else ArmorBlue)
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Text(
                        text = "¿QUÉ ROL TIENE ESTE TELÉFONO EN LA PARTIDA?",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 13.sp,
                        color = Color.LightGray,
                        letterSpacing = 1.sp
                    )

                    Spacer(modifier = Modifier.height(8.dp))

                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(12.dp))
                            .background(Color(0xFF0F1811))
                            .padding(4.dp),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(10.dp))
                                .background(if (isHost) SanAndreasGold else Color.Transparent)
                                .clickable { viewModel.setLobbyRole(true) }
                                .padding(vertical = 10.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(text = "👑", fontSize = 16.sp)
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(
                                    text = "SOY ANFITRIÓN (HOST)",
                                    fontFamily = BebasNeueFontFamily,
                                    fontSize = 15.sp,
                                    color = if (isHost) Color.Black else Color.Gray
                                )
                            }
                        }

                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(10.dp))
                                .background(if (!isHost) ArmorBlue else Color.Transparent)
                                .clickable { viewModel.setLobbyRole(false) }
                                .padding(vertical = 10.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(text = "🎮", fontSize = 16.sp)
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(
                                    text = "SOY INVITADO (HUÉSPED)",
                                    fontFamily = BebasNeueFontFamily,
                                    fontSize = 15.sp,
                                    color = if (!isHost) Color.Black else Color.Gray
                                )
                            }
                        }
                    }
                }
            }
        }

        // ACCESO RÁPIDO 1-CLIC DE INYECCIÓN Y LANZAMIENTO
        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = when {
                        modStatus.isVerifiedInGta -> Color(0xFF102616)
                        modStatus.isModInstalled -> Color(0xFF261D12)
                        else -> Color(0xFF241414)
                    }
                ),
                shape = RoundedCornerShape(12.dp),
                border = androidx.compose.foundation.BorderStroke(
                    1.dp,
                    when {
                        modStatus.isVerifiedInGta -> GroveGreen.copy(alpha = 0.6f)
                        modStatus.isModInstalled -> SanAndreasGold.copy(alpha = 0.6f)
                        else -> WantedRed.copy(alpha = 0.6f)
                    }
                )
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(12.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(modifier = Modifier.weight(1f), verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = if (modStatus.isVerifiedInGta) Icons.Default.CheckCircle else Icons.Default.Warning,
                            contentDescription = null,
                            tint = when {
                                modStatus.isVerifiedInGta -> GroveGreen
                                modStatus.isModInstalled -> SanAndreasGold
                                else -> WantedRed
                            },
                            modifier = Modifier.size(20.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Column {
                            Text(
                                text = when {
                                    modStatus.isVerifiedInGta -> "PUENTE C++ Y MOD VERIFICADOS EN GTA SA ✓"
                                    else -> "AUTO-INSTALADOR 1-CLIC DISPONIBLE"
                                },
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 13.sp,
                                color = when {
                                    modStatus.isVerifiedInGta -> GroveGreen
                                    else -> SanAndreasGold
                                }
                            )
                            Text(
                                text = "Inyecta libCoopAndreasBridge.so, .csa y reescribe IP/Sala en 1 toque.",
                                style = MaterialTheme.typography.bodySmall,
                                color = Color.LightGray,
                                fontSize = 11.sp
                            )
                        }
                    }

                    Button(
                        onClick = { viewModel.oneClickInstallAndLaunchGame(launchAfterInstall = false) },
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier.height(34.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black)
                    ) {
                        Text("⚡ 1-CLIC", fontFamily = BebasNeueFontFamily, fontSize = 12.sp)
                    }
                }
            }
        }

        if (isHost) {
            item {
                HostRoomHeaderCard(
                    session = session,
                    localIp = modStatus.localIpAddress,
                    onRefreshRoom = { viewModel.createRoom() },
                    onShareCode = {
                        viewModel.showNotification("Código copiado: ${session.roomCode} (IP: ${modStatus.localIpAddress})")
                    },
                    onAddTestGuest = {
                        viewModel.addGuestPlayer()
                    },
                    onSetCustomIp = { newIp ->
                        viewModel.setCustomHostIp(newIp)
                    }
                )
            }

            item {
                HostScreenAndDeviceCard(
                    singleDeviceMode = modStatus.singleDeviceMode,
                    screenSplitType = modStatus.screenSplitType,
                    allowSeparateVehicles = modStatus.allowSeparateVehicles,
                    maxPlayers = modStatus.maxPlayers,
                    onToggleSingleDevice = { viewModel.setSingleDeviceMode(it) },
                    onSelectScreenSplit = { viewModel.setScreenSplitType(it) },
                    onToggleSeparateVehicles = { viewModel.toggleModSeparateVehicles(it) },
                    onSelectMaxPlayers = { viewModel.setMaxPlayers(it) }
                )
            }

            item {
                HostMissionAndGameSetupCard(
                    session = session,
                    freeRoamMode = modStatus.freeRoamAnySave,
                    onToggleFreeRoam = { viewModel.setFreeRoamAnySave(it) },
                    onChangeMission = { viewModel.setNavTab(AppNavTab.CAMPAIGN) },
                    onStartCoop = {
                        viewModel.oneClickInstallAndLaunchGame(launchAfterInstall = true)
                    }
                )
            }

            item {
                HostCharacterCard(
                    currentSkin = modStatus.mySelectedSkin,
                    currentRole = session.players.firstOrNull { it.isHost }?.role ?: PlayerCoopRole.DRIVER,
                    onSkinChange = { viewModel.changeMySkin(it) },
                    onRoleChange = { viewModel.changeMyRole(it) }
                )
            }

            item {
                HostConnectedPlayersHeader(
                    playerCount = session.players.size,
                    maxPlayers = modStatus.maxPlayers,
                    onAddPlayerSlot = { viewModel.addGuestPlayer() }
                )
            }

            items(session.players) { player ->
                PlayerCard(
                    player = player,
                    isLocalPlayer = player.isHost
                )
            }
        } else {
            item {
                GuestConnectionCard(
                    session = session,
                    currentHostIp = modStatus.localIpAddress,
                    onJoinRoom = { code, ip ->
                        viewModel.joinRoom(code, ip)
                    },
                    onDisconnect = {
                        viewModel.setLobbyRole(false)
                        viewModel.showNotification("Desconectado de la sala")
                    },
                    onLaunchGame = { viewModel.launchGame() },
                    onConnectDiscovered = { viewModel.connectToDiscoveredHost() }
                )
            }

            item {
                GuestCharacterCard(
                    currentSkin = modStatus.mySelectedSkin,
                    currentRole = session.players.firstOrNull { !it.isHost }?.role ?: PlayerCoopRole.GUNNER,
                    onSkinChange = { viewModel.changeMySkin(it) },
                    onRoleChange = { viewModel.changeMyRole(it) },
                    onReadyClick = {
                        viewModel.toggleReady()
                        viewModel.showNotification("¡Listo para jugar! Tu skin ha sido guardado.")
                    }
                )
            }

            item {
                GuestLobbyStatusCard(session = session)
            }

            item {
                TacticalPingsCard(
                    session = session,
                    onSendPing = { viewModel.sendPing(it) }
                )
            }

            items(session.players) { player ->
                PlayerCard(
                    player = player,
                    isLocalPlayer = !player.isHost
                )
            }
        }

        item {
            Button(
                onClick = { viewModel.setNavTab(AppNavTab.INSTALLATION) },
                colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black),
                modifier = Modifier
                    .fillMaxWidth()
                    .height(48.dp)
                    .testTag("go_to_installer_button"),
                shape = RoundedCornerShape(12.dp)
            ) {
                Icon(
                    imageVector = Icons.Default.Download,
                    contentDescription = null,
                    modifier = Modifier.size(20.dp)
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = "ABRIR AUTO-INSTALADOR DE 1 CLIC →",
                    fontFamily = BebasNeueFontFamily,
                    fontSize = 16.sp
                )
            }
            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

@Composable
private fun HostRoomHeaderCard(
    session: com.example.network.CoopSessionInfo,
    localIp: String,
    onRefreshRoom: () -> Unit,
    onShareCode: () -> Unit,
    onAddTestGuest: () -> Unit,
    onSetCustomIp: (String) -> Unit
) {
    var editIpOpen by remember { mutableStateOf(false) }
    var tempIpInput by remember { mutableStateOf(localIp) }
    val connectedGuests = session.players.filter { !it.isHost }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("host_room_card"),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
        shape = RoundedCornerShape(18.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, SanAndreasGold.copy(alpha = 0.5f))
    ) {
        Column(modifier = Modifier.padding(18.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(
                        text = "SALA ANFITRIÓN (${session.networkMode.shortLabel})",
                        style = MaterialTheme.typography.labelLarge,
                        color = SanAndreasGold
                    )
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(top = 2.dp)
                    ) {
                        Text(
                            text = session.roomCode,
                            style = MaterialTheme.typography.headlineLarge,
                            color = Color.White
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(6.dp))
                                .background(SanAndreasGold.copy(alpha = 0.2f))
                                .padding(horizontal = 8.dp, vertical = 2.dp)
                        ) {
                            Text(
                                text = if (session.networkMode == NetworkConnectionMode.ONLINE_REMOTE) "RELAY ONLINE ACTIVO" else "SERVIDOR LAN ACTIVO",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = SanAndreasGold
                            )
                        }
                    }
                }

                Row {
                    IconButton(onClick = onRefreshRoom) {
                        Icon(Icons.Default.Refresh, contentDescription = "Nueva sala", tint = Color.LightGray)
                    }
                    IconButton(onClick = onShareCode) {
                        Icon(Icons.Default.Share, contentDescription = "Compartir", tint = SanAndreasGold)
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp))
                    .background(Color(0xFF131E15))
                    .padding(12.dp)
            ) {
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = if (session.networkMode == NetworkConnectionMode.ONLINE_REMOTE)
                                    "CÓDIGO DE SALA ONLINE (SIN ABRIR PUERTOS):"
                                else
                                    "TU IP WI-FI (UDP 8988 / TCP 8989 / UDP 8990):",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = GroveGreen
                            )
                            Text(
                                text = if (session.networkMode == NetworkConnectionMode.ONLINE_REMOTE)
                                    "${session.roomCode} (${session.stunPublicEndpoint ?: "Relay P2P"})"
                                else
                                    localIp,
                                style = MaterialTheme.typography.titleMedium,
                                color = Color.White
                            )
                        }

                        Text(
                            text = if (editIpOpen) "CANCELAR" else "EDITAR IP",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 13.sp,
                            color = SanAndreasGold,
                            modifier = Modifier.clickable { editIpOpen = !editIpOpen }
                        )
                    }

                    if (editIpOpen) {
                        Spacer(modifier = Modifier.height(8.dp))
                        OutlinedTextField(
                            value = tempIpInput,
                            onValueChange = { tempIpInput = it },
                            label = { Text("IP de tu teléfono o Zona Wi-Fi") },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = SanAndreasGold,
                                focusedTextColor = Color.White,
                                unfocusedTextColor = Color.White
                            )
                        )
                        Spacer(modifier = Modifier.height(6.dp))
                        Button(
                            onClick = {
                                onSetCustomIp(tempIpInput)
                                editIpOpen = false
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = SanAndreasGold),
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(8.dp)
                        ) {
                            Text("GUARDAR ESTA IP", fontFamily = BebasNeueFontFamily, color = Color.Black)
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp))
                    .background(if (connectedGuests.isNotEmpty()) Color(0xFF102816) else Color(0xFF261D10))
                    .padding(12.dp)
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(10.dp)
                            .clip(CircleShape)
                            .background(if (connectedGuests.isNotEmpty()) GroveGreen else SanAndreasGold)
                    )
                    Spacer(modifier = Modifier.width(10.dp))
                    Column {
                        Text(
                            text = if (connectedGuests.isNotEmpty())
                                "🟢 ¡${connectedGuests.size} COMPAÑERO(S) CONECTADO(S) EN LA SALA!"
                            else
                                "🟡 ESPERANDO QUE TU COMPAÑERO SE CONECTE...",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 15.sp,
                            color = if (connectedGuests.isNotEmpty()) GroveGreen else SanAndreasGold
                        )
                        Text(
                            text = if (session.networkMode == NetworkConnectionMode.ONLINE_REMOTE)
                                "Pásale el código ${session.roomCode} a tus amigos para que se unan desde cualquier red Wi-Fi o datos móviles."
                            else
                                "En la misma red Wi-Fi se detectará automáticamente por UDP 8988 o ingresando tu IP ($localIp).",
                            style = MaterialTheme.typography.bodySmall,
                            color = Color(0xFFE5E7EB)
                        )
                    }
                }
            }

            if (session.players.size < 4) {
                Spacer(modifier = Modifier.height(10.dp))
                OutlinedButton(
                    onClick = onAddTestGuest,
                    modifier = Modifier.fillMaxWidth().height(38.dp),
                    shape = RoundedCornerShape(8.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, GroveGreen)
                ) {
                    Icon(Icons.Default.PersonAdd, contentDescription = null, tint = GroveGreen, modifier = Modifier.size(16.dp))
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = "+ AÑADIR JUGADOR A LA SALA (PROBAR HASTA 4 JUGADORES)",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 12.sp,
                        color = GroveGreen
                    )
                }
            }
        }
    }
}

@Composable
private fun HostScreenAndDeviceCard(
    singleDeviceMode: Boolean,
    screenSplitType: String,
    allowSeparateVehicles: Boolean,
    maxPlayers: Int,
    onToggleSingleDevice: (Boolean) -> Unit,
    onSelectScreenSplit: (String) -> Unit,
    onToggleSeparateVehicles: (Boolean) -> Unit,
    onSelectMaxPlayers: (Int) -> Unit
) {
    var isExpanded by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("host_device_settings_card"),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
        shape = RoundedCornerShape(18.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { isExpanded = !isExpanded },
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "CÁMARA 3D INDEPENDIENTE, VEHÍCULOS Y JUGADORES",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 16.sp,
                        color = SanAndreasGold
                    )
                    Text(
                        text = if (!singleDeviceMode) "Cámara 3D Independiente • Hasta $maxPlayers jugadores" else "Modo 1 Teléfono • 2 Mandos",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.LightGray
                    )
                }
                Text(
                    text = if (isExpanded) "▲ OCULTAR" else "▼ DESPLEGAR",
                    fontFamily = BebasNeueFontFamily,
                    fontSize = 12.sp,
                    color = SanAndreasGold
                )
            }

            AnimatedVisibility(visible = isExpanded) {
                Column(modifier = Modifier.padding(top = 12.dp)) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(10.dp))
                            .background(Color(0xFF101913))
                            .padding(4.dp),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (!singleDeviceMode) GroveGreen else Color.Transparent)
                                .clickable { onToggleSingleDevice(false) }
                                .padding(vertical = 8.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = "📱📱 2-4+ TELÉFONOS (CÁM 3D LIBRE)",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 13.sp,
                                color = if (!singleDeviceMode) Color.Black else Color.Gray
                            )
                        }

                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (singleDeviceMode) SanAndreasGold else Color.Transparent)
                                .clickable { onToggleSingleDevice(true) }
                                .padding(vertical = 8.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = "🎮🎮 1 DISPOSITIVO (2 MANDOS)",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 13.sp,
                                color = if (singleDeviceMode) Color.Black else Color.Gray
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(12.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = "LÍMITE DE JUGADORES EN CO-OP:",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 13.sp,
                                color = Color.LightGray
                            )
                            Text(
                                text = "Sincronización nativa de 2 a 4+ jugadores",
                                fontSize = 11.sp,
                                color = Color.Gray
                            )
                        }

                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf(2, 3, 4).forEach { count ->
                                val isSelected = maxPlayers == count
                                Box(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(6.dp))
                                        .background(if (isSelected) GroveGreen else Color(0xFF1E281F))
                                        .clickable { onSelectMaxPlayers(count) }
                                        .padding(horizontal = 10.dp, vertical = 4.dp)
                                ) {
                                    Text(
                                        text = "$count",
                                        fontFamily = BebasNeueFontFamily,
                                        fontSize = 14.sp,
                                        color = if (isSelected) Color.Black else Color.White
                                    )
                                }
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = "VEHÍCULOS COMPARTIDOS (DRIVE-BY) O SEPARADOS:",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 13.sp,
                                color = Color.LightGray
                            )
                            Text(
                                text = if (allowSeparateVehicles) "Conductor + pasajeros con drive-by o vehículos distintos" else "Solo vehículo compartido",
                                fontSize = 11.sp,
                                color = Color.Gray
                            )
                        }

                        Switch(
                            checked = allowSeparateVehicles,
                            onCheckedChange = onToggleSeparateVehicles,
                            colors = SwitchDefaults.colors(checkedThumbColor = GroveGreen, checkedTrackColor = GroveGreen.copy(alpha = 0.4f))
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun HostMissionAndGameSetupCard(
    session: com.example.network.CoopSessionInfo,
    freeRoamMode: Boolean,
    onToggleFreeRoam: (Boolean) -> Unit,
    onChangeMission: () -> Unit,
    onStartCoop: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("host_mission_setup_card"),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
        shape = RoundedCornerShape(18.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, SanAndreasGold)
    ) {
        Column(modifier = Modifier.padding(18.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "SINCRONIZACIÓN DE CAMPAÑA (\$ONMISSION) Y MODO LIBRE",
                    fontFamily = BebasNeueFontFamily,
                    fontSize = 17.sp,
                    color = SanAndreasGold
                )
                Text(
                    text = "Control Host",
                    fontSize = 11.sp,
                    color = GroveGreen
                )
            }

            Spacer(modifier = Modifier.height(10.dp))

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color(0xFF101C13))
                    .padding(4.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (!freeRoamMode) SanAndreasGold else Color.Transparent)
                        .clickable { onToggleFreeRoam(false) }
                        .padding(vertical = 6.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = "🎯 MISIÓN DE CAMPAÑA",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 13.sp,
                        color = if (!freeRoamMode) Color.Black else Color.Gray
                    )
                }

                Box(
                    modifier = Modifier
                        .weight(1f)
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (freeRoamMode) GroveGreen else Color.Transparent)
                        .clickable { onToggleFreeRoam(true) }
                        .padding(vertical = 6.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = "🗺️ MODO LIBRE + MISIONES",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 13.sp,
                        color = if (freeRoamMode) Color.Black else Color.Gray
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            if (!freeRoamMode) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(Color(0xFF101912))
                        .padding(12.dp)
                ) {
                    Column {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = session.currentMission.title,
                                style = MaterialTheme.typography.titleLarge,
                                color = Color.White
                            )
                            Text(text = session.currentMission.act.icon, fontSize = 20.sp)
                        }

                        Text(
                            text = "Jefe: ${session.currentMission.bossOrGiver}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = GroveGreen
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = session.currentMission.synopsis,
                            style = MaterialTheme.typography.bodySmall,
                            color = Color.LightGray
                        )
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

                OutlinedButton(
                    onClick = onChangeMission,
                    modifier = Modifier.fillMaxWidth().height(42.dp),
                    shape = RoundedCornerShape(10.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, SanAndreasGold)
                ) {
                    Text("CAMBIAR MISIÓN DE CAMPAÑA", fontFamily = BebasNeueFontFamily, fontSize = 15.sp, color = SanAndreasGold)
                }
            } else {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(Color(0xFF112316))
                        .border(1.dp, GroveGreen.copy(alpha = 0.5f), RoundedCornerShape(10.dp))
                        .padding(12.dp)
                ) {
                    Text(
                        text = "🗺️ MODO LIBRE + CAMPAÑA SINCRONIZADA:\nEl puente nativo sincroniza coordenadas (X, Y, Z), cámara 3D independiente, vehículos compartidos con drive-by y el flag \$ONMISSION en cualquier partida guardada o nueva.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color(0xFFD1FAE5)
                    )
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            Button(
                onClick = onStartCoop,
                colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black),
                modifier = Modifier
                    .fillMaxWidth()
                    .height(50.dp)
                    .testTag("host_start_coop_button"),
                shape = RoundedCornerShape(12.dp)
            ) {
                Icon(Icons.Default.PlayArrow, contentDescription = null, modifier = Modifier.size(24.dp))
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = "⚡ INYECTAR CONFIGURACIÓN Y EMPEZAR CO-OP",
                    fontFamily = BebasNeueFontFamily,
                    fontSize = 17.sp
                )
            }
        }
    }
}

@Composable
private fun HostCharacterCard(
    currentSkin: CharacterSkin,
    currentRole: PlayerCoopRole,
    onSkinChange: (CharacterSkin) -> Unit,
    onRoleChange: (PlayerCoopRole) -> Unit
) {
    var skinMenuExpanded by remember { mutableStateOf(false) }
    var roleMenuExpanded by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "TU PERSONAJE & ROL (ANFITRIÓN)",
                fontFamily = BebasNeueFontFamily,
                fontSize = 15.sp,
                color = SanAndreasGold
            )

            Spacer(modifier = Modifier.height(10.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Box(modifier = Modifier.weight(1f)) {
                    OutlinedButton(
                        onClick = { skinMenuExpanded = true },
                        modifier = Modifier.fillMaxWidth().height(42.dp),
                        shape = RoundedCornerShape(8.dp),
                        border = androidx.compose.foundation.BorderStroke(1.dp, SanAndreasGold)
                    ) {
                        Text(
                            text = "Skin: ${currentSkin.displayName.take(12)}...",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 14.sp,
                            color = SanAndreasGold
                        )
                    }
                    DropdownMenu(
                        expanded = skinMenuExpanded,
                        onDismissRequest = { skinMenuExpanded = false }
                    ) {
                        CharacterSkin.values().forEach { skin ->
                            DropdownMenuItem(
                                text = { Text(skin.displayName) },
                                onClick = {
                                    onSkinChange(skin)
                                    skinMenuExpanded = false
                                }
                            )
                        }
                    }
                }

                Box(modifier = Modifier.weight(1f)) {
                    OutlinedButton(
                        onClick = { roleMenuExpanded = true },
                        modifier = Modifier.fillMaxWidth().height(42.dp),
                        shape = RoundedCornerShape(8.dp),
                        border = androidx.compose.foundation.BorderStroke(1.dp, GroveGreen)
                    ) {
                        Text(
                            text = "Rol: ${currentRole.title.take(14)}...",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 14.sp,
                            color = GroveGreen
                        )
                    }
                    DropdownMenu(
                        expanded = roleMenuExpanded,
                        onDismissRequest = { roleMenuExpanded = false }
                    ) {
                        PlayerCoopRole.values().forEach { role ->
                            DropdownMenuItem(
                                text = { Text("${role.icon} ${role.title}") },
                                onClick = {
                                    onRoleChange(role)
                                    roleMenuExpanded = false
                                }
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun HostConnectedPlayersHeader(
    playerCount: Int,
    maxPlayers: Int,
    onAddPlayerSlot: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column {
            Text(
                text = "JUGADORES EN LA SALA ($playerCount/$maxPlayers)",
                fontFamily = BebasNeueFontFamily,
                fontSize = 18.sp,
                color = Color.White
            )
            Text(
                text = if (playerCount > 1) "Sincronizados en vivo (UDP 8990 / Relay)" else "Esperando compañeros",
                style = MaterialTheme.typography.bodySmall,
                color = if (playerCount > 1) GroveGreen else SanAndreasGold
            )
        }

        if (playerCount < maxPlayers) {
            Button(
                onClick = onAddPlayerSlot,
                colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black),
                shape = RoundedCornerShape(8.dp)
            ) {
                Text("+ Añadir Homie", fontFamily = BebasNeueFontFamily, fontSize = 13.sp)
            }
        }
    }
}

@Composable
private fun GuestConnectionCard(
    session: com.example.network.CoopSessionInfo,
    currentHostIp: String,
    onJoinRoom: (String, String) -> Unit,
    onDisconnect: () -> Unit,
    onLaunchGame: () -> Unit,
    onConnectDiscovered: () -> Unit
) {
    val isConnected = session.isConnectedToHost
    val isOnlineMode = session.networkMode == NetworkConnectionMode.ONLINE_REMOTE
    val discovered = session.discoveredHostOnWifi
    var joinCodeInput by remember { mutableStateOf(discovered?.roomCode ?: session.roomCode) }
    var hostIpInput by remember { mutableStateOf(discovered?.ip ?: currentHostIp) }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("guest_connection_card"),
        colors = CardDefaults.cardColors(containerColor = Color(0xFF101C2B)),
        shape = RoundedCornerShape(18.dp),
        border = androidx.compose.foundation.BorderStroke(1.5.dp, ArmorBlue)
    ) {
        Column(modifier = Modifier.padding(18.dp)) {
            Text(
                text = if (isOnlineMode) "EMPAREJAMIENTO ONLINE POR CÓDIGO DE SALA" else "CONEXIÓN LAN CON EL ANFITRIÓN (HOST)",
                fontFamily = BebasNeueFontFamily,
                fontSize = 17.sp,
                color = ArmorBlue
            )

            Spacer(modifier = Modifier.height(10.dp))

            session.connectionBannerMessage?.let { banner ->
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(Color(0xFF162B3D))
                        .padding(10.dp)
                ) {
                    Text(
                        text = banner,
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.White
                    )
                }
                Spacer(modifier = Modifier.height(10.dp))
            }

            if (isConnected) {
                val targetIp = session.hostIpAddress.ifBlank { currentHostIp }
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color(0xFF0C2417))
                        .padding(14.dp)
                ) {
                    Column {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Box(
                                modifier = Modifier
                                    .size(10.dp)
                                    .clip(CircleShape)
                                    .background(GroveGreen)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = "CONECTADO A LA SALA: ${session.roomCode}",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 16.sp,
                                color = GroveGreen
                            )
                        }

                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = if (isOnlineMode) "Canal Online Relay + STUN Activo (Sin abrir puertos)" else "Servidor Host IP: $targetIp (TCP 8989 / UDP 8990)",
                            style = MaterialTheme.typography.bodyMedium,
                            color = Color.LightGray
                        )
                        Spacer(modifier = Modifier.height(8.dp))

                        Button(
                            onClick = onLaunchGame,
                            colors = ButtonDefaults.buttonColors(containerColor = SanAndreasGold, contentColor = Color.Black),
                            modifier = Modifier.fillMaxWidth().height(46.dp),
                            shape = RoundedCornerShape(10.dp)
                        ) {
                            Icon(Icons.Default.PlayArrow, contentDescription = null, modifier = Modifier.size(20.dp))
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("⚡ INYECTAR IP/SALA Y ABRIR GTA SAN ANDREAS", fontFamily = BebasNeueFontFamily, fontSize = 15.sp)
                        }
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

                OutlinedButton(
                    onClick = onDisconnect,
                    modifier = Modifier.fillMaxWidth().height(38.dp),
                    shape = RoundedCornerShape(8.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, WantedRed)
                ) {
                    Text("DESCONECTARSE / CAMBIAR DE SALA", fontFamily = BebasNeueFontFamily, color = WantedRed, fontSize = 13.sp)
                }
            } else {
                if (!isOnlineMode && discovered != null) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(12.dp))
                            .background(Color(0xFF0F2B1B))
                            .border(1.5.dp, GroveGreen, RoundedCornerShape(12.dp))
                            .padding(12.dp)
                    ) {
                        Column {
                            Text(
                                text = "📡 ¡ANFITRIÓN DETECTADO EN TU RED WI-FI (UDP 8988)!",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 15.sp,
                                color = GroveGreen
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                text = "Host: ${discovered.hostName} | IP: ${discovered.ip} (Sala: ${discovered.roomCode})",
                                style = MaterialTheme.typography.bodySmall,
                                color = Color.LightGray
                            )
                            Spacer(modifier = Modifier.height(8.dp))
                            Button(
                                onClick = onConnectDiscovered,
                                colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black),
                                modifier = Modifier.fillMaxWidth().height(42.dp),
                                shape = RoundedCornerShape(8.dp)
                            ) {
                                Text("⚡ CONECTARME CON 1 TOQUE", fontFamily = BebasNeueFontFamily, fontSize = 15.sp)
                            }
                        }
                    }
                    Spacer(modifier = Modifier.height(12.dp))
                }

                OutlinedTextField(
                    value = joinCodeInput,
                    onValueChange = { joinCodeInput = it },
                    label = { Text("Código de sala (ej: GROVE-404)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth().testTag("guest_room_code_input"),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = ArmorBlue,
                        focusedTextColor = Color.White,
                        unfocusedTextColor = Color.White
                    )
                )

                if (!isOnlineMode) {
                    Spacer(modifier = Modifier.height(8.dp))
                    OutlinedTextField(
                        value = hostIpInput,
                        onValueChange = { hostIpInput = it },
                        label = { Text("IP del Host en LAN (ej: 192.168.1.105)") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth().testTag("guest_host_ip_input"),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedBorderColor = ArmorBlue,
                            focusedTextColor = Color.White,
                            unfocusedTextColor = Color.White
                        )
                    )
                }

                Spacer(modifier = Modifier.height(12.dp))

                Button(
                    onClick = { onJoinRoom(joinCodeInput, if (isOnlineMode) "" else hostIpInput) },
                    colors = ButtonDefaults.buttonColors(containerColor = ArmorBlue),
                    modifier = Modifier.fillMaxWidth().height(48.dp).testTag("guest_connect_button"),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Icon(Icons.Default.Login, contentDescription = null, tint = Color.Black)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = if (isOnlineMode) "UNIRME POR CÓDIGO DE SALA ONLINE" else "CONECTARME A LA SALA DEL HOST",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 16.sp,
                        color = Color.Black
                    )
                }
            }
        }
    }
}

@Composable
private fun GuestCharacterCard(
    currentSkin: CharacterSkin,
    currentRole: PlayerCoopRole,
    onSkinChange: (CharacterSkin) -> Unit,
    onRoleChange: (PlayerCoopRole) -> Unit,
    onReadyClick: () -> Unit
) {
    var skinMenuExpanded by remember { mutableStateOf(false) }
    var roleMenuExpanded by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("guest_character_card"),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
        shape = RoundedCornerShape(18.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, ArmorBlue)
    ) {
        Column(modifier = Modifier.padding(18.dp)) {
            Text(
                text = "ELIGE TU PERSONAJE & ROL (HUÉSPED)",
                fontFamily = BebasNeueFontFamily,
                fontSize = 17.sp,
                color = ArmorBlue
            )

            Spacer(modifier = Modifier.height(10.dp))

            Box(modifier = Modifier.fillMaxWidth()) {
                Button(
                    onClick = { skinMenuExpanded = true },
                    modifier = Modifier.fillMaxWidth().height(46.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF142436)),
                    shape = RoundedCornerShape(10.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, ArmorBlue)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "🎭 PERSONAJE: ${currentSkin.displayName}",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 16.sp,
                            color = ArmorBlue
                        )
                        Text(text = "CAMBIAR ▼", fontFamily = BebasNeueFontFamily, fontSize = 13.sp, color = Color.LightGray)
                    }
                }

                DropdownMenu(
                    expanded = skinMenuExpanded,
                    onDismissRequest = { skinMenuExpanded = false }
                ) {
                    CharacterSkin.values().forEach { skin ->
                        DropdownMenuItem(
                            text = { Text("${skin.displayName} (${skin.tag})") },
                            onClick = {
                                onSkinChange(skin)
                                skinMenuExpanded = false
                            }
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            Box(modifier = Modifier.fillMaxWidth()) {
                Button(
                    onClick = { roleMenuExpanded = true },
                    modifier = Modifier.fillMaxWidth().height(46.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF142436)),
                    shape = RoundedCornerShape(10.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, GroveGreen)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "${currentRole.icon} ROL: ${currentRole.title}",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 16.sp,
                            color = GroveGreen
                        )
                        Text(text = "CAMBIAR ▼", fontFamily = BebasNeueFontFamily, fontSize = 13.sp, color = Color.LightGray)
                    }
                }

                DropdownMenu(
                    expanded = roleMenuExpanded,
                    onDismissRequest = { roleMenuExpanded = false }
                ) {
                    PlayerCoopRole.values().forEach { role ->
                        DropdownMenuItem(
                            text = { Text("${role.icon} ${role.title} - ${role.description}") },
                            onClick = {
                                onRoleChange(role)
                                roleMenuExpanded = false
                            }
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(14.dp))

            Button(
                onClick = onReadyClick,
                colors = ButtonDefaults.buttonColors(containerColor = ArmorBlue, contentColor = Color.Black),
                modifier = Modifier.fillMaxWidth().height(46.dp),
                shape = RoundedCornerShape(10.dp)
            ) {
                Icon(Icons.Default.Check, contentDescription = null)
                Spacer(modifier = Modifier.width(6.dp))
                Text("CONFIRMAR MI PERSONAJE Y MARCAR LISTO", fontFamily = BebasNeueFontFamily, fontSize = 15.sp)
            }
        }
    }
}

@Composable
private fun GuestLobbyStatusCard(
    session: com.example.network.CoopSessionInfo
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "ESTADO DE LA SALA & MISIÓN ENTRANTE",
                fontFamily = BebasNeueFontFamily,
                fontSize = 15.sp,
                color = SanAndreasGold
            )
            Spacer(modifier = Modifier.height(6.dp))

            Text(
                text = "Misión fijada por el Host: ${session.currentMission.title}",
                style = MaterialTheme.typography.titleMedium,
                color = Color.White
            )
            Text(
                text = "Tu rol en esta misión: ${session.currentMission.coOpRole2}",
                style = MaterialTheme.typography.bodySmall,
                color = GroveGreen
            )
        }
    }
}

@Composable
private fun TacticalPingsCard(
    session: com.example.network.CoopSessionInfo,
    onSendPing: (TacticalPingType) -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "PINGS TÁCTICOS RÁPIDOS",
                fontFamily = BebasNeueFontFamily,
                fontSize = 15.sp,
                color = SanAndreasGold
            )

            Spacer(modifier = Modifier.height(10.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                PingChip(TacticalPingType.GET_IN) { onSendPing(it) }
                PingChip(TacticalPingType.REGROUP) { onSendPing(it) }
                PingChip(TacticalPingType.FOLLOW_TRAIN) { onSendPing(it) }
            }

            Spacer(modifier = Modifier.height(6.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                PingChip(TacticalPingType.NEED_BACKUP) { onSendPing(it) }
                PingChip(TacticalPingType.SNIPER_UP) { onSendPing(it) }
                PingChip(TacticalPingType.RESPECT_BOOST) { onSendPing(it) }
            }

            if (session.recentPings.isNotEmpty()) {
                Spacer(modifier = Modifier.height(8.dp))
                val latest = session.recentPings.first()
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF0F1811))
                        .padding(horizontal = 10.dp, vertical = 6.dp)
                ) {
                    Text(
                        text = "📢 ${latest.senderName}: \"${latest.type.label}\"",
                        style = MaterialTheme.typography.bodyMedium,
                        color = SanAndreasGold
                    )
                }
            }
        }
    }
}

@Composable
private fun PingChip(
    type: TacticalPingType,
    onSend: (TacticalPingType) -> Unit
) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF132317))
            .border(1.dp, GroveGreen.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
            .clickable { onSend(type) }
            .padding(horizontal = 8.dp, vertical = 6.dp)
    ) {
        Text(
            text = "${type.icon} ${type.name.replace('_', ' ').take(10)}",
            fontFamily = BebasNeueFontFamily,
            fontSize = 12.sp,
            color = Color.White
        )
    }
}
