package com.example.ui.screens

import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Archive
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.HelpOutline
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.model.CharacterSkin
import com.example.ui.theme.ArmorBlue
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkBorder
import com.example.ui.theme.DarkSurfaceCard
import com.example.ui.theme.DarkSurfaceElevated
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.ui.theme.WantedRed
import com.example.viewmodel.CoopMainViewModel

@Composable
fun InstallationScreen(
    viewModel: CoopMainViewModel,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val modStatus by viewModel.modStatus.collectAsState()
    val session by viewModel.sessionState.collectAsState()
    val isCheckingDiagnostics by viewModel.isCheckingDiagnostics.collectAsState()

    val safFolderLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        if (uri != null) {
            try {
                context.contentResolver.takePersistableUriPermission(
                    uri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                )
            } catch (e: Exception) {
                // ignore
            }
            viewModel.oneClickInstallAndLaunchGame(launchAfterInstall = false, customSafUri = uri)
        }
    }

    var mySkinMenuOpen by remember { mutableStateOf(false) }
    val partnerPlayer = session.players.getOrNull(1)
    var isAdvancedExpanded by remember { mutableStateOf(false) }
    var isSecondaryOptionsExpanded by remember { mutableStateOf(false) }

    LazyColumn(
        modifier = modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp)
            .testTag("installation_screen"),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            Spacer(modifier = Modifier.height(4.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(
                        text = "AUTO-INSTALADOR DE 1 CLIC REAL",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 22.sp,
                        color = Color.White
                    )
                    Text(
                        text = "Inyección directa de libCoopAndreasBridge.so, scripts .csa y SAMP/settings.ini",
                        style = MaterialTheme.typography.bodySmall,
                        color = GroveGreen
                    )
                }

                val statusText = when {
                    modStatus.isVerifiedInGta -> "INYECTADO EN GTA SA ✓"
                    modStatus.isModInstalled -> "LISTO PARA INYECTAR"
                    else -> "PENDIENTE"
                }
                val statusColor = when {
                    modStatus.isVerifiedInGta -> GroveGreen
                    modStatus.isModInstalled -> SanAndreasGold
                    else -> WantedRed
                }

                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(statusColor.copy(alpha = 0.2f))
                        .padding(horizontal = 8.dp, vertical = 3.dp)
                ) {
                    Text(
                        text = statusText,
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 11.sp,
                        color = statusColor
                    )
                }
            }
        }

        // TARJETA PRINCIPAL: AUTO-INSTALADOR DE 1 CLIC REAL + LANZAR JUEGO (Objetivo 4)
        item {
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag("one_click_auto_installer_card"),
                colors = CardDefaults.cardColors(containerColor = Color(0xFF102416)),
                shape = RoundedCornerShape(18.dp),
                border = androidx.compose.foundation.BorderStroke(2.dp, GroveGreen)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(32.dp)
                                .clip(CircleShape)
                                .background(GroveGreen),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(Icons.Default.Download, contentDescription = null, tint = Color.Black, modifier = Modifier.size(18.dp))
                        }
                        Spacer(modifier = Modifier.width(10.dp))
                        Column {
                            Text(
                                text = "⚡ INYECCIÓN AUTOMÁTICA EN 1 CLIC (SIN PASOS MANUALES)",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 17.sp,
                                color = SanAndreasGold
                            )
                            Text(
                                text = "Sala: ${session.roomCode} • Modo: ${session.networkMode.shortLabel} • Rol: ${if (session.isHost) "HOST" else "INVITADO"}",
                                style = MaterialTheme.typography.bodySmall,
                                color = Color(0xFFD1FAE5)
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(10.dp))

                    Text(
                        text = "Con un único toque inyecta automáticamente libCoopAndreasBridge.so, libCLEOMod.so, scripts .csa compilados, coop_config.ini y SAMP/settings.ini en /storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/ o mediante SAF persistente en Android/data/com.rockstargames.gtasa/files/, reescribiendo la IP y Sala antes de lanzar GTA SA.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color(0xFFE5E7EB),
                        lineHeight = 17.sp
                    )

                    if (modStatus.isCleoInstalling) {
                        Spacer(modifier = Modifier.height(10.dp))
                        LinearProgressIndicator(
                            progress = { modStatus.cleoInstallProgress },
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(6.dp)
                                .clip(RoundedCornerShape(3.dp)),
                            color = SanAndreasGold,
                            trackColor = Color(0xFF1F3826)
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = modStatus.cleoInstallStatusMessage ?: "Inyectando binarios y scripts...",
                            style = MaterialTheme.typography.bodySmall,
                            color = SanAndreasGold
                        )
                    }

                    Spacer(modifier = Modifier.height(14.dp))

                    // BOTÓN ÚNICO 1-CLIC: INYECTAR TODO Y LANZAR GTA SA
                    Button(
                        onClick = { viewModel.oneClickInstallAndLaunchGame(launchAfterInstall = true) },
                        enabled = !modStatus.isCleoInstalling,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = SanAndreasGold,
                            contentColor = Color.Black
                        ),
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(54.dp)
                            .testTag("one_click_cleo_install_button"),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        if (modStatus.isCleoInstalling) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(20.dp),
                                color = Color.Black,
                                strokeWidth = 2.dp
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = "INYECTANDO BINARIOS Y SCRIPTS...",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 16.sp
                            )
                        } else {
                            Icon(Icons.Default.PlayArrow, contentDescription = null, modifier = Modifier.size(24.dp))
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = "⚡ INSTALAR EN 1 CLIC, REESCRIBIR IP/SALA Y JUGAR GTA SA",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 16.sp
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedButton(
                            onClick = { viewModel.oneClickInstallAndLaunchGame(launchAfterInstall = false) },
                            enabled = !modStatus.isCleoInstalling,
                            modifier = Modifier
                                .weight(1f)
                                .height(42.dp)
                                .testTag("one_click_inject_only_button"),
                            shape = RoundedCornerShape(10.dp),
                            border = androidx.compose.foundation.BorderStroke(1.dp, GroveGreen)
                        ) {
                            Icon(Icons.Default.Check, contentDescription = null, tint = GroveGreen, modifier = Modifier.size(16.dp))
                            Spacer(modifier = Modifier.width(4.dp))
                            Text(
                                text = "SOLO INYECTAR EN 1 CLIC",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 13.sp,
                                color = GroveGreen
                            )
                        }

                        OutlinedButton(
                            onClick = { safFolderLauncher.launch(null) },
                            modifier = Modifier
                                .weight(1f)
                                .height(42.dp)
                                .testTag("bind_saf_folder_button"),
                            shape = RoundedCornerShape(10.dp),
                            border = androidx.compose.foundation.BorderStroke(1.dp, ArmorBlue)
                        ) {
                            Icon(Icons.Default.Folder, contentDescription = null, tint = ArmorBlue, modifier = Modifier.size(16.dp))
                            Spacer(modifier = Modifier.width(4.dp))
                            Text(
                                text = if (modStatus.persistedSafUriString != null) "SAF VINCULADO ✓" else "VINCULAR SAF ANDROID/DATA",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = ArmorBlue
                            )
                        }
                    }
                }
            }
        }

        // HERRAMIENTA DE DIAGNÓSTICO AUTOMÁTICO EN 1 CLIC
        item {
            ModDiagnosticCard(
                report = modStatus.diagnosticReport,
                isChecking = isCheckingDiagnostics,
                onRunDiagnostics = { viewModel.runAutoDiagnostics() },
                onPickSafFolder = { safFolderLauncher.launch(null) },
                onOpenZArchiver = { viewModel.openInZArchiver() },
                onLaunchGame = { viewModel.launchGame() },
                onInstallCleo = { viewModel.oneClickInstallAndLaunchGame(launchAfterInstall = false) }
            )
        }

        // CONFIGURACIÓN DE PERSONAJES, CÁMARA 3D INDEPENDIENTE Y VEHÍCULOS
        item {
            StepCard(
                stepNumber = "1",
                stepTitle = "PERSONAJES, CÁMARA 3D INDEPENDIENTE Y VEHÍCULOS",
                stepStatus = "ACTIVO"
            ) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text = "Tu Personaje 3D (Sincronizado en tiempo real):",
                        fontSize = 12.sp,
                        color = Color.LightGray
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    OutlinedButton(
                        onClick = { mySkinMenuOpen = true },
                        modifier = Modifier.fillMaxWidth().height(44.dp),
                        shape = RoundedCornerShape(8.dp),
                        border = androidx.compose.foundation.BorderStroke(1.dp, GroveGreen)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "👤 ${modStatus.mySelectedSkin.displayName}",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 15.sp,
                                color = GroveGreen
                            )
                            Text(
                                text = "CAMBIAR ▾",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = Color.Gray
                            )
                        }
                    }

                    DropdownMenu(expanded = mySkinMenuOpen, onDismissRequest = { mySkinMenuOpen = false }) {
                        CharacterSkin.values().forEach { skin ->
                            DropdownMenuItem(
                                text = { Text(skin.displayName) },
                                onClick = {
                                    viewModel.setMyModSkin(skin)
                                    mySkinMenuOpen = false
                                }
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF111E14))
                        .padding(10.dp)
                ) {
                    Column {
                        Text(
                            text = "PUENTE NATIVO C++ (libCoopAndreasBridge.so):",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 12.sp,
                            color = SanAndreasGold
                        )
                        Spacer(modifier = Modifier.height(2.dp))
                        Text(
                            text = if (partnerPlayer != null)
                                "🟢 Sincronizando a ${partnerPlayer.name} (${partnerPlayer.skin.displayName}) + hasta 4 jugadores con cámara 3D independiente y drive-by."
                            else
                                "🟢 Soporta 2 a 4+ jugadores con cámara 3D independiente en cada dispositivo, vehículos compartidos (conductor + pasajeros con drive-by) o separados, y \$ONMISSION.",
                            style = MaterialTheme.typography.bodySmall,
                            color = Color.White
                        )
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF101914))
                        .clickable { isAdvancedExpanded = !isAdvancedExpanded }
                        .padding(10.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(Icons.Default.Settings, contentDescription = null, tint = ArmorBlue, modifier = Modifier.size(16.dp))
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "AJUSTES DE CÁMARA, IP MANUAL Y VEHÍCULOS",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = ArmorBlue
                            )
                        }
                        Text(
                            text = if (isAdvancedExpanded) "▲ OCULTAR" else "▼ DESPLEGAR",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 11.sp,
                            color = ArmorBlue
                        )
                    }
                }

                AnimatedVisibility(visible = isAdvancedExpanded) {
                    Column(
                        modifier = Modifier.padding(top = 8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        val isIndepCam = modStatus.cameraMode.contains("Independiente")
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(8.dp))
                                .background(Color(0xFF132017))
                                .clickable {
                                    viewModel.setModCameraMode(
                                        if (isIndepCam) "Cámara Compartida Dinámica" else "Cámara Normal Independiente (Cada uno en su pantalla)"
                                    )
                                }
                                .padding(horizontal = 10.dp, vertical = 8.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = if (isIndepCam) "🎥 Cámara 3D Independiente (Recomendado)" else "👥 Cámara Compartida Dinámica",
                                    fontFamily = BebasNeueFontFamily,
                                    fontSize = 14.sp,
                                    color = Color.White
                                )
                                Text(
                                    text = if (isIndepCam) "Cada jugador controla su propia cámara 3D en su dispositivo" else "Ambos jugadores en un mismo encuadre",
                                    fontSize = 11.sp,
                                    color = Color.Gray
                                )
                            }

                            Text(
                                text = "CAMBIAR",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = GroveGreen
                            )
                        }

                        var editIpOpen by remember { mutableStateOf(false) }
                        var tempIpInput by remember { mutableStateOf(modStatus.localIpAddress) }

                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(8.dp))
                                .background(Color(0xFF101B24))
                                .padding(10.dp)
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column {
                                    Text(
                                        text = "IP HOST EN COOP_CONFIG.INI & SAMP/SETTINGS.INI:",
                                        fontFamily = BebasNeueFontFamily,
                                        fontSize = 12.sp,
                                        color = ArmorBlue
                                    )
                                    Text(
                                        text = modStatus.localIpAddress,
                                        style = MaterialTheme.typography.titleMedium,
                                        color = Color.White
                                    )
                                }

                                Text(
                                    text = if (editIpOpen) "LISTO" else "EDITAR IP",
                                    fontFamily = BebasNeueFontFamily,
                                    fontSize = 12.sp,
                                    color = ArmorBlue,
                                    modifier = Modifier.clickable {
                                        if (editIpOpen) {
                                            viewModel.setCustomHostIp(tempIpInput)
                                        }
                                        editIpOpen = !editIpOpen
                                    }
                                )
                            }

                            if (editIpOpen) {
                                Spacer(modifier = Modifier.height(6.dp))
                                OutlinedTextField(
                                    value = tempIpInput,
                                    onValueChange = { tempIpInput = it },
                                    label = { Text("IP del Host en LAN") },
                                    singleLine = true,
                                    modifier = Modifier.fillMaxWidth(),
                                    colors = OutlinedTextFieldDefaults.colors(
                                        focusedBorderColor = ArmorBlue,
                                        unfocusedBorderColor = DarkBorder,
                                        focusedTextColor = Color.White,
                                        unfocusedTextColor = Color.White
                                    )
                                )
                            }
                        }

                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(8.dp))
                                .background(Color(0xFF132017))
                                .padding(horizontal = 10.dp, vertical = 6.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = "🚗 Vehículos Compartidos (Drive-By) o Separados",
                                    fontFamily = BebasNeueFontFamily,
                                    fontSize = 14.sp,
                                    color = Color.White
                                )
                                Text(
                                    text = if (modStatus.allowSeparateVehicles) "Pueden compartir vehículo con drive-by o conducir autos distintos" else "Vehículo compartido obligatorio",
                                    fontSize = 11.sp,
                                    color = Color.Gray
                                )
                            }

                            Switch(
                                checked = modStatus.allowSeparateVehicles,
                                onCheckedChange = { viewModel.toggleModSeparateVehicles(it) },
                                colors = SwitchDefaults.colors(
                                    checkedThumbColor = GroveGreen,
                                    checkedTrackColor = Color(0xFF14301B)
                                )
                            )
                        }
                    }
                }
            }
        }

        // OPCIONES SECUNDARIAS (COMPARTIR ZIP / GUARDAR EN DESCARGAS / DESINSTALAR)
        item {
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(12.dp))
                    .clickable { isSecondaryOptionsExpanded = !isSecondaryOptionsExpanded },
                colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
                shape = RoundedCornerShape(12.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "HERRAMIENTAS SECUNDARIAS (EXPORTAR ZIP / DESINSTALAR)",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 12.sp,
                            color = Color.Gray
                        )
                        Text(
                            text = if (isSecondaryOptionsExpanded) "▲ OCULTAR" else "▼ DESPLEGAR",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 11.sp,
                            color = Color.Gray
                        )
                    }

                    AnimatedVisibility(visible = isSecondaryOptionsExpanded) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(top = 10.dp),
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            OutlinedButton(
                                onClick = {
                                    val intent = viewModel.getShareModZipIntent()
                                    if (intent != null) {
                                        context.startActivity(intent)
                                    } else {
                                        viewModel.installModToGame()
                                        viewModel.showNotification("ZIP generado. Pulsa de nuevo para compartir.")
                                    }
                                },
                                modifier = Modifier.weight(1f).height(40.dp),
                                shape = RoundedCornerShape(8.dp),
                                border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
                            ) {
                                Icon(Icons.Default.Archive, contentDescription = null, tint = Color.LightGray, modifier = Modifier.size(16.dp))
                                Spacer(modifier = Modifier.width(4.dp))
                                Text("COMPARTIR ZIP", fontFamily = BebasNeueFontFamily, fontSize = 12.sp, color = Color.White)
                            }

                            if (modStatus.isModInstalled) {
                                OutlinedButton(
                                    onClick = { viewModel.uninstallModFromGame() },
                                    modifier = Modifier.weight(1f).height(40.dp),
                                    shape = RoundedCornerShape(8.dp),
                                    border = androidx.compose.foundation.BorderStroke(1.dp, WantedRed.copy(alpha = 0.5f))
                                ) {
                                    Icon(Icons.Default.Delete, contentDescription = null, tint = WantedRed, modifier = Modifier.size(16.dp))
                                    Spacer(modifier = Modifier.width(4.dp))
                                    Text("DESINSTALAR MOD", fontFamily = BebasNeueFontFamily, fontSize = 12.sp, color = WantedRed)
                                }
                            }
                        }
                    }
                }
            }
            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

@Composable
private fun StepCard(
    stepNumber: String,
    stepTitle: String,
    stepStatus: String,
    content: @Composable () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(26.dp)
                            .clip(CircleShape)
                            .background(GroveGreen),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = stepNumber,
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 16.sp,
                            color = Color.Black
                        )
                    }
                    Spacer(modifier = Modifier.width(10.dp))
                    Text(
                        text = stepTitle,
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 15.sp,
                        color = Color.White
                    )
                }

                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(4.dp))
                        .background(Color(0xFF142419))
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text(
                        text = stepStatus,
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 11.sp,
                        color = GroveGreen
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            content()
        }
    }
}
