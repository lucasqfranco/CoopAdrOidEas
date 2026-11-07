package com.example.ui.screens

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
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
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.model.CharacterSkin
import com.example.model.NetworkConnectionMode
import com.example.model.PlayerMarkerColor
import com.example.model.SkinCategory
import com.example.ui.theme.ArmorBlue
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkSurfaceElevated
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.viewmodel.CoopMainViewModel

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LobbyScreen(
    viewModel: CoopMainViewModel,
    modifier: Modifier = Modifier
) {
    val session by viewModel.sessionState.collectAsState()
    val modStatus by viewModel.modStatus.collectAsState()

    val isHost = session.isHost
    val isOnlineMode = session.networkMode == NetworkConnectionMode.ONLINE_REMOTE

    var roomOrIpInput by remember(session.roomCode, modStatus.targetHostIp) {
        mutableStateOf(
            if (isOnlineMode) session.roomCode
            else modStatus.targetHostIp.ifBlank { session.discoveredHostOnWifi?.ip ?: "" }
        )
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 12.dp)
            .testTag("lobby_screen"),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // =====================================================================
        // PASO 1: ROL (HOST O INVITADO) Y TIPO DE CONEXIÓN (WIFI O ONLINE)
        // =====================================================================
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
            shape = RoundedCornerShape(16.dp),
            border = BorderStroke(1.5.dp, if (isHost) SanAndreasGold else ArmorBlue)
        ) {
            Column(modifier = Modifier.padding(14.dp)) {
                Text(
                    text = "1. ELEGÍ TU ROL EN LA PARTIDA",
                    fontFamily = BebasNeueFontFamily,
                    fontSize = 15.sp,
                    color = Color.LightGray,
                    letterSpacing = 0.8.sp
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
                            .padding(vertical = 12.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "👑 CREAR SALA (HOST)",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 16.sp,
                            color = if (isHost) Color.Black else Color.Gray
                        )
                    }

                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .clip(RoundedCornerShape(10.dp))
                            .background(if (!isHost) ArmorBlue else Color.Transparent)
                            .clickable { viewModel.setLobbyRole(false) }
                            .padding(vertical = 12.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "🎮 UNIRME (INVITADO)",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 16.sp,
                            color = if (!isHost) Color.Black else Color.Gray
                        )
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

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
                            .padding(vertical = 8.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "📡 MISMO WI-FI (LAN)",
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
                            .padding(vertical = 8.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "🌐 A DISTANCIA (ONLINE)",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 14.sp,
                            color = if (isOnlineMode) Color.Black else Color.Gray
                        )
                    }
                }
            }
        }

        // =====================================================================
        // PASO 2: CONEXIÓN DE SALA (SEGÚN HOST O INVITADO)
        // =====================================================================
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
            shape = RoundedCornerShape(16.dp),
            border = BorderStroke(1.dp, Color(0xFF28382C))
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                if (isHost) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = "2. DATOS DE TU SALA PARA EL INVITADO",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 15.sp,
                                color = SanAndreasGold
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                text = if (isOnlineMode) "Código: ${session.roomCode}" else "IP Wi-Fi: ${modStatus.localIpAddress}  |  Sala: ${session.roomCode}",
                                style = MaterialTheme.typography.titleMedium,
                                color = Color.White
                            )
                        }
                        IconButton(onClick = { viewModel.createRoom() }) {
                            Icon(Icons.Default.Refresh, contentDescription = "Cambiar código", tint = SanAndreasGold)
                        }
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    val guestCount = (session.players.size - 1).coerceAtLeast(0)
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(8.dp))
                            .background(if (guestCount > 0) Color(0xFF102816) else Color(0xFF1B221D))
                            .padding(10.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Box(
                            modifier = Modifier
                                .size(10.dp)
                                .clip(CircleShape)
                                .background(if (guestCount > 0) GroveGreen else SanAndreasGold)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = if (guestCount > 0)
                                "Compañero conectado ($guestCount en sala). Ya podés iniciar la partida."
                            else
                                "Esperando a que tu compañero se conecte (o iniciá ya y se suma en vivo).",
                            style = MaterialTheme.typography.bodySmall,
                            color = Color.LightGray
                        )
                    }
                } else {
                    Text(
                        text = "2. CONECTATE A LA SALA DEL HOST",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 15.sp,
                        color = ArmorBlue
                    )
                    Spacer(modifier = Modifier.height(8.dp))

                    val discovered = session.discoveredHostOnWifi
                    if (!isOnlineMode && discovered != null) {
                        Button(
                            onClick = { viewModel.connectToDiscoveredHost() },
                            colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black),
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(10.dp)
                        ) {
                            Text(
                                text = "⚡ CONECTAR AL HOST DETECTADO (${discovered.ip} • ${discovered.roomCode})",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 15.sp
                            )
                        }
                        Spacer(modifier = Modifier.height(8.dp))
                    }

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        OutlinedTextField(
                            value = roomOrIpInput,
                            onValueChange = { roomOrIpInput = it },
                            label = {
                                Text(if (isOnlineMode) "Código de Sala (ej. GROVE-101)" else "IP del Host (ej. 192.168.1.45)")
                            },
                            modifier = Modifier.weight(1f),
                            singleLine = true,
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = ArmorBlue,
                                focusedTextColor = Color.White,
                                unfocusedTextColor = Color.White
                            )
                        )

                        Button(
                            onClick = {
                                if (isOnlineMode) {
                                    viewModel.joinRoom(roomOrIpInput.trim(), null)
                                } else {
                                    viewModel.joinRoom(session.roomCode, roomOrIpInput.trim())
                                }
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = ArmorBlue, contentColor = Color.Black),
                            shape = RoundedCornerShape(10.dp),
                            modifier = Modifier.height(54.dp)
                        ) {
                            Text("VINCULAR", fontFamily = BebasNeueFontFamily, fontSize = 15.sp)
                        }
                    }

                    if (session.isConnectedToHost) {
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "✅ Vinculado a la sala ${session.roomCode}",
                            color = GroveGreen,
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))
                var showQrPanel by remember { mutableStateOf(false) }
                val clipboardManager = LocalClipboardManager.current
                val qrPayload = "COOP|${modStatus.localIpAddress}|51515|${session.roomCode}|${session.networkMode.name}"

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = { showQrPanel = !showQrPanel },
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(10.dp),
                        border = BorderStroke(1.dp, SanAndreasGold)
                    ) {
                        Text(
                            text = if (showQrPanel) "📱 Ocultar QR de Sala" else "📱 QR de Sala",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 14.sp,
                            color = SanAndreasGold
                        )
                    }
                    OutlinedButton(
                        onClick = {
                            if (isHost) {
                                clipboardManager.setText(AnnotatedString(qrPayload))
                                viewModel.showNotification("📋 Código QR copiado: $qrPayload")
                            } else {
                                val clipText = clipboardManager.getText()?.text?.trim().orEmpty()
                                val payloadToUse = if (clipText.startsWith("COOP|", ignoreCase = true)) clipText else roomOrIpInput.trim()
                                if (payloadToUse.isNotEmpty()) {
                                    roomOrIpInput = payloadToUse
                                    viewModel.parseAndJoinQrPayload(payloadToUse)
                                }
                            }
                        },
                        modifier = Modifier.weight(1f),
                        shape = RoundedCornerShape(10.dp),
                        border = BorderStroke(1.dp, GroveGreen)
                    ) {
                        Text(
                            text = if (isHost) "📋 Copiar QR 1-Toque" else "📋 Pegar QR 1-Toque",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 14.sp,
                            color = GroveGreen
                        )
                    }
                }

                if (showQrPanel) {
                    Spacer(modifier = Modifier.height(10.dp))
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(12.dp))
                            .background(Color(0xFF121A14))
                            .padding(12.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Canvas(
                            modifier = Modifier
                                .size(154.dp)
                                .clip(RoundedCornerShape(8.dp))
                                .background(Color.White)
                                .padding(8.dp)
                        ) {
                            val gridSize = 21
                            val cellW = size.width / gridSize
                            val cellH = size.height / gridSize
                            fun isFinder(r: Int, c: Int): Boolean {
                                val inTL = r in 0..6 && c in 0..6
                                val inTR = r in 0..6 && c in (gridSize - 7) until gridSize
                                val inBL = r in (gridSize - 7) until gridSize && c in 0..6
                                return inTL || inTR || inBL
                            }
                            fun finderDark(r: Int, c: Int): Boolean {
                                val lr = if (r >= gridSize - 7) r - (gridSize - 7) else r
                                val lc = if (c >= gridSize - 7) c - (gridSize - 7) else c
                                return lr == 0 || lr == 6 || lc == 0 || lc == 6 || (lr in 2..4 && lc in 2..4)
                            }
                            val bytes = qrPayload.encodeToByteArray()
                            for (r in 0 until gridSize) {
                                for (c in 0 until gridSize) {
                                    val dark = if (isFinder(r, c)) {
                                        finderDark(r, c)
                                    } else if (r == 6 || c == 6) {
                                        (r + c) % 2 == 0
                                    } else {
                                        val idx = (r * gridSize + c)
                                        val b = bytes[idx % bytes.size].toInt() and 0xFF
                                        ((b shr (idx % 8)) xor (r * 3 + c * 7)) and 1 == 1
                                    }
                                    if (dark) {
                                        drawRect(
                                            color = Color.Black,
                                            topLeft = Offset(c * cellW, r * cellH),
                                            size = Size(cellW * 0.96f, cellH * 0.96f)
                                        )
                                    }
                                }
                            }
                        }
                        Spacer(modifier = Modifier.height(6.dp))
                        Text(
                            text = qrPayload,
                            style = MaterialTheme.typography.labelSmall,
                            color = SanAndreasGold
                        )
                    }
                }
            }
        }

        // =====================================================================
        // PASO 3: ELEGÍ TU PERSONAJE
        // =====================================================================
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
            shape = RoundedCornerShape(16.dp),
            border = BorderStroke(1.dp, Color(0xFF28382C))
        ) {
            var selectedCategory by remember { mutableStateOf(modStatus.mySelectedSkin.category) }
            var customSkinInput by remember(modStatus.customSkinId, modStatus.mySelectedSkin) {
                mutableStateOf((modStatus.customSkinId ?: modStatus.mySelectedSkin.modelId).toString())
            }
            val activeSkinId = modStatus.customSkinId ?: modStatus.mySelectedSkin.modelId

            Column(modifier = Modifier.padding(14.dp)) {
                Text(
                    text = "3. ELEGÍ TU PERSONAJE: ${modStatus.mySelectedSkin.displayName.uppercase()} (ID $activeSkinId)",
                    fontFamily = BebasNeueFontFamily,
                    fontSize = 15.sp,
                    color = Color.LightGray
                )
                Spacer(modifier = Modifier.height(6.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    SkinCategory.entries.forEach { cat ->
                        val catSelected = selectedCategory == cat
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (catSelected) SanAndreasGold else Color(0xFF141E17))
                                .clickable { selectedCategory = cat }
                                .padding(vertical = 6.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = cat.title,
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 11.sp,
                                color = if (catSelected) Color.Black else Color.LightGray
                            )
                        }
                    }
                }
                Spacer(modifier = Modifier.height(8.dp))
                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    CharacterSkin.entries.filter { it.category == selectedCategory }.forEach { skin ->
                        val selected = modStatus.customSkinId == null && modStatus.mySelectedSkin == skin
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (selected) GroveGreen else Color(0xFF141E17))
                                .clickable { viewModel.changeMySkin(skin) }
                                .padding(horizontal = 10.dp, vertical = 6.dp)
                        ) {
                            Text(
                                text = "${skin.displayName} (${skin.modelId})",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = if (selected) Color.Black else Color.LightGray
                            )
                        }
                    }
                }
                Spacer(modifier = Modifier.height(10.dp))
                OutlinedTextField(
                    value = customSkinInput,
                    onValueChange = { raw ->
                        val digits = raw.filter { it.isDigit() }.take(3)
                        customSkinInput = digits
                        digits.toIntOrNull()?.let { id ->
                            viewModel.changeCustomSkinId(id.coerceIn(0, 299))
                        }
                    },
                    label = { Text("ID de Skin (0-299) • Cualquiera de las 300 skins de GTA SA") },
                    singleLine = true,
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = GroveGreen,
                        unfocusedBorderColor = Color(0xFF28382C),
                        focusedLabelColor = GroveGreen,
                        unfocusedLabelColor = Color.Gray,
                        focusedTextColor = Color.White,
                        unfocusedTextColor = Color.White
                    ),
                    modifier = Modifier
                        .fillMaxWidth()
                        .testTag("custom_skin_id_input")
                )
                Spacer(modifier = Modifier.height(12.dp))
                Text(
                    text = "COLOR DE TU FLECHA / MARCADOR: ${modStatus.myMarkerColor.displayName.uppercase()}",
                    fontFamily = BebasNeueFontFamily,
                    fontSize = 14.sp,
                    color = Color.LightGray
                )
                Spacer(modifier = Modifier.height(6.dp))
                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    PlayerMarkerColor.entries.forEach { markerColor ->
                        val selected = modStatus.myMarkerColor == markerColor
                        val chipColor = Color(markerColor.colorHex)
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (selected) chipColor.copy(alpha = 0.28f) else Color(0xFF141E17))
                                .clickable { viewModel.changeMyMarkerColor(markerColor) }
                                .padding(horizontal = 9.dp, vertical = 5.dp)
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(10.dp)
                                    .clip(CircleShape)
                                    .background(chipColor)
                            )
                            Spacer(modifier = Modifier.width(5.dp))
                            Text(
                                text = markerColor.displayName,
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = if (selected) chipColor else Color.LightGray
                            )
                        }
                    }
                }
            }
        }

        // =====================================================================
        // UN ÚNICO BOTÓN DE INICIO DIRECTO
        // =====================================================================
        Button(
            onClick = { viewModel.oneClickInstallAndLaunchGame(launchAfterInstall = true) },
            colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black),
            modifier = Modifier
                .fillMaxWidth()
                .height(60.dp)
                .testTag("start_coop_single_button"),
            shape = RoundedCornerShape(14.dp)
        ) {
            Icon(
                imageVector = Icons.Default.PlayArrow,
                contentDescription = null,
                modifier = Modifier.size(26.dp)
            )
            Spacer(modifier = Modifier.width(8.dp))
            Text(
                text = "JUGAR GTA: SAN ANDREAS CO-OP",
                fontFamily = BebasNeueFontFamily,
                fontSize = 20.sp,
                letterSpacing = 1.sp
            )
        }

        // =====================================================================
        // PANEL DE TRUCOS RÁPIDOS DIRECTO EN EL LOBBY (SIN TECLADO FÍSICO)
        // =====================================================================
        var showLobbyCheats by remember { mutableStateOf(false) }
        var lobbyCheatInput by remember { mutableStateOf("") }

        OutlinedButton(
            onClick = { showLobbyCheats = !showLobbyCheats },
            modifier = Modifier
                .fillMaxWidth()
                .height(44.dp),
            shape = RoundedCornerShape(12.dp),
            border = BorderStroke(1.dp, SanAndreasGold)
        ) {
            Text(
                text = if (showLobbyCheats) "🎮 OCULTAR TRUCOS RÁPIDOS (SIN TECLADO)" else "🎮 TRUCOS RÁPIDOS DE GTA SA (SIN TECLADO)",
                fontFamily = BebasNeueFontFamily,
                fontSize = 15.sp,
                color = SanAndreasGold
            )
        }

        if (showLobbyCheats) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
                shape = RoundedCornerShape(14.dp),
                border = BorderStroke(1.dp, Color(0xFF28382C))
            ) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    val cheatGroups = listOf(
                        "Locales (Salud, Armas, Jetpack, Estrellas)" to listOf(
                            "HESOYAM" to "Vida/Chaleco + \$250k",
                            "LXGIWYL" to "Armas 1",
                            "KJKSZPJ" to "Armas 2",
                            "UZUMYMW" to "Armas 3",
                            "ROCKETMAN" to "Jetpack",
                            "AEZAKMI" to "0 Estrellas",
                            "OSRBLHH" to "+2 Estrellas"
                        ),
                        "Vehículos (Aparecen frente a CJ)" to listOf(
                            "AIWPRTON" to "Rhino (432)",
                            "JUMPJET" to "Hydra (520)",
                            "OHDUDE" to "Hunter (425)",
                            "PDNEJOH" to "Hotring (494)",
                            "FOURWHEELFUN" to "Quad (471)"
                        ),
                        "Mundo Cooperativo (Clima y Caos)" to listOf(
                            "AFZLLQLL" to "Soleado",
                            "AUIFRVQS" to "Tormenta",
                            "IOJUFZN" to "Modo Caos"
                        )
                    )
                    cheatGroups.forEach { (title, list) ->
                        Text(title, style = MaterialTheme.typography.labelSmall, color = SanAndreasGold)
                        FlowRow(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            verticalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            list.forEach { (code, desc) ->
                                Box(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(8.dp))
                                        .background(Color(0xFF1A281E))
                                        .clickable { viewModel.triggerCheatFromApp(code) }
                                        .padding(horizontal = 9.dp, vertical = 6.dp)
                                ) {
                                    Text("$code • $desc", style = MaterialTheme.typography.labelSmall, color = Color.White)
                                }
                            }
                        }
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        OutlinedTextField(
                            value = lobbyCheatInput,
                            onValueChange = { lobbyCheatInput = it },
                            label = { Text("Código manual (ej. HESOYAM)") },
                            singleLine = true,
                            modifier = Modifier.weight(1f),
                            colors = OutlinedTextFieldDefaults.colors(
                                focusedBorderColor = GroveGreen,
                                focusedTextColor = Color.White,
                                unfocusedTextColor = Color.White
                            )
                        )
                        Button(
                            onClick = {
                                viewModel.triggerCheatFromApp(lobbyCheatInput)
                                lobbyCheatInput = ""
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black),
                            shape = RoundedCornerShape(8.dp)
                        ) {
                            Text("APLICAR", fontFamily = BebasNeueFontFamily, fontSize = 14.sp)
                        }
                    }
                }
            }
        }

        Text(
            text = "Nota: Dentro de GTA SA podés tocar 'Reanudar' (Resume) o 'Iniciar Partida -> Nueva Partida'; el modo cooperativo sincroniza a ambos jugadores automáticamente apenas aparecen en el mapa.",
            style = MaterialTheme.typography.bodySmall,
            color = Color.Gray,
            modifier = Modifier.padding(horizontal = 4.dp)
        )
    }
}
