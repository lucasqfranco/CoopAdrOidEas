package com.example.ui.screens

import android.view.MotionEvent
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.FastForward
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.ui.components.AudioVisualizer
import com.example.ui.theme.ArmorBlue
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkBorder
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.ui.theme.WantedRed
import com.example.viewmodel.CoopMainViewModel

@OptIn(ExperimentalComposeUiApi::class)
@Composable
fun ActiveMissionOverlay(
    viewModel: CoopMainViewModel,
    modifier: Modifier = Modifier
) {
    val session by viewModel.sessionState.collectAsState()
    val voiceState by viewModel.voiceState.collectAsState()
    val currentMission = session.currentMission
    val checkpoints = currentMission.checkpoints
    val currentCp = checkpoints.getOrNull(session.currentCheckpointIndex) ?: checkpoints.lastOrNull()

    Surface(
        modifier = modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.94f))
            .testTag("active_mission_overlay"),
        color = Color(0xFF0C140E)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(18.dp)
        ) {
            // Header Bar
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(10.dp)
                            .clip(CircleShape)
                            .background(GroveGreen)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "EN VIVO: PUENTE C++ 3D & HUD GTA SAN ANDREAS",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 17.sp,
                        color = GroveGreen
                    )
                }

                IconButton(
                    onClick = { viewModel.toggleOverlayMode() },
                    modifier = Modifier.testTag("close_overlay_button")
                ) {
                    Icon(
                        imageVector = Icons.Default.Close,
                        contentDescription = "Close Overlay",
                        tint = Color.White
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Mission Title & Wanted Stars
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = Color(0xFF132317)),
                shape = RoundedCornerShape(16.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, SanAndreasGold)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = currentMission.title,
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 24.sp,
                            color = Color.White
                        )

                        Row {
                            for (i in 1..6) {
                                Icon(
                                    imageVector = Icons.Default.Star,
                                    contentDescription = "Star $i",
                                    tint = if (i <= session.teamWantedLevel) WantedRed else Color(0xFF263328),
                                    modifier = Modifier
                                        .size(18.dp)
                                        .clickable {
                                            viewModel.coopNetworkManager.setTeamWantedLevel(
                                                if (session.teamWantedLevel == i) i - 1 else i
                                            )
                                        }
                                )
                            }
                        }
                    }

                    Text(
                        text = "Jefe: ${currentMission.bossOrGiver} • Sala ${session.roomCode} (${session.networkMode.shortLabel}) • \$ONMISSION=${session.onMissionFlag}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = GroveGreen
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    val progressRatio = if (checkpoints.isNotEmpty()) {
                        (session.currentCheckpointIndex + 1).toFloat() / checkpoints.size.toFloat()
                    } else 1f

                    LinearProgressIndicator(
                        progress = { progressRatio },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(8.dp)
                            .clip(RoundedCornerShape(4.dp)),
                        color = GroveGreen,
                        trackColor = Color(0xFF1B3522)
                    )

                    Spacer(modifier = Modifier.height(6.dp))

                    Text(
                        text = "Checkpoint ${session.currentCheckpointIndex + 1} de ${checkpoints.size} • Cámara 3D Independiente Activa",
                        style = MaterialTheme.typography.bodyMedium,
                        color = SanAndreasGold
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Telemetría 3D en Tiempo Real (Coordenadas X,Y,Z, Vehículos y Drive-By de 2 a 4+ jugadores)
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag("overlay_telemetry_card"),
                colors = CardDefaults.cardColors(containerColor = Color(0xFF101D14)),
                shape = RoundedCornerShape(14.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, ArmorBlue.copy(alpha = 0.5f))
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "📡 TELEMETRÍA 3D EN VIVO (UDP 8990 / BRIDGE .SO)",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 14.sp,
                            color = ArmorBlue
                        )
                        Text(
                            text = "${session.players.size} Jugadores Sincronizados",
                            style = MaterialTheme.typography.bodySmall,
                            color = GroveGreen
                        )
                    }

                    Spacer(modifier = Modifier.height(6.dp))

                    session.players.forEachIndexed { idx, p ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 3.dp)
                                .clip(RoundedCornerShape(6.dp))
                                .background(Color(0xFF0C150E))
                                .padding(horizontal = 8.dp, vertical = 5.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = "P${idx + 1} ${p.skin.displayName} • ${p.currentVehicle}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = Color.White
                                )
                                Text(
                                    text = "XYZ: (${"%.1f".format(p.posX)}, ${"%.1f".format(p.posY)}, ${"%.1f".format(p.posZ)}) | Cam 3D Libre",
                                    fontSize = 11.sp,
                                    color = Color(0xFFA7F3D0)
                                )
                            }
                            Text(
                                text = "${p.pingMs}ms",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = SanAndreasGold
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Current Checkpoint Task & Co-op Roles (P1 to P4)
            currentCp?.let { cp ->
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF17291B)),
                    shape = RoundedCornerShape(16.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = "OBJETIVO ACTUAL:",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 15.sp,
                            color = SanAndreasGold
                        )
                        Text(
                            text = cp.description,
                            style = MaterialTheme.typography.titleMedium,
                            color = Color.White
                        )

                        Spacer(modifier = Modifier.height(10.dp))

                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(8.dp))
                                .background(Color(0xFF0F1B12))
                                .padding(10.dp)
                        ) {
                            Column {
                                Text(
                                    text = "🚗 ROL P1 (Conductor): ${cp.role1Task}",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = Color(0xFFA7F3D0)
                                )
                                Spacer(modifier = Modifier.height(4.dp))
                                Text(
                                    text = "🎯 ROL P2 (Drive-By): ${cp.role2Task}",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = Color(0xFFFDE68A)
                                )
                                if (session.players.size >= 3) {
                                    Spacer(modifier = Modifier.height(4.dp))
                                    Text(
                                        text = "🛡️ ROL P3 (Apoyo): ${cp.role3Task}",
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = ArmorBlue
                                    )
                                }
                                if (session.players.size >= 4) {
                                    Spacer(modifier = Modifier.height(4.dp))
                                    Text(
                                        text = "💥 ROL P4 (Táctico): ${cp.role4Task}",
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = Color(0xFFE9D5FF)
                                    )
                                }
                            }
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Live Voice Comms Bar inside Overlay
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = Color(0xFF132216)),
                shape = RoundedCornerShape(16.dp)
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box(
                        modifier = Modifier
                            .size(54.dp)
                            .clip(CircleShape)
                            .background(if (voiceState.isPttPressed) GroveGreen else Color(0xFF224229))
                            .border(2.dp, if (voiceState.isPttPressed) Color.White else GroveGreen, CircleShape)
                            .pointerInteropFilter { motionEvent ->
                                when (motionEvent.action) {
                                    MotionEvent.ACTION_DOWN -> {
                                        viewModel.onPttPressed()
                                        true
                                    }
                                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                                        viewModel.onPttReleased()
                                        true
                                    }
                                    else -> false
                                }
                            },
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Default.Mic,
                            contentDescription = "Overlay PTT",
                            tint = if (voiceState.isPttPressed) Color.Black else Color.White,
                            modifier = Modifier.size(24.dp)
                        )
                    }

                    Spacer(modifier = Modifier.width(12.dp))

                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = if (voiceState.isPttPressed) "HABLANDO EN RADIO (UDP 8990 / RELAY)" else "RADIO: PTT LISTO",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 15.sp,
                            color = if (voiceState.isPttPressed) GroveGreen else Color.LightGray
                        )
                        AudioVisualizer(
                            levelDb = voiceState.liveMicLevelDb,
                            isTransmitting = voiceState.isPttPressed,
                            modifier = Modifier.height(28.dp),
                            barCount = 16
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // Mission Action Controls: Advance Checkpoint, Pass, Fail
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                OutlinedButton(
                    onClick = { viewModel.failMission() },
                    modifier = Modifier
                        .weight(1f)
                        .height(48.dp)
                        .testTag("fail_mission_button"),
                    shape = RoundedCornerShape(10.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, WantedRed)
                ) {
                    Text(
                        text = "FALLAR / REPETIR",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 14.sp,
                        color = WantedRed
                    )
                }

                Button(
                    onClick = { viewModel.advanceCheckpoint() },
                    colors = ButtonDefaults.buttonColors(containerColor = SanAndreasGold),
                    modifier = Modifier
                        .weight(1.3f)
                        .height(48.dp)
                        .testTag("advance_checkpoint_button"),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.FastForward,
                        contentDescription = null,
                        tint = Color.Black,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(
                        text = "AVANZAR CHECKPOINT",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 15.sp,
                        color = Color.Black
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            Button(
                onClick = { viewModel.completeMission() },
                colors = ButtonDefaults.buttonColors(containerColor = GroveGreen),
                modifier = Modifier
                    .fillMaxWidth()
                    .height(50.dp)
                    .testTag("pass_mission_button"),
                shape = RoundedCornerShape(12.dp)
            ) {
                Text(
                    text = "🏆 COMPLETAR MISIÓN (PASSED)",
                    fontFamily = BebasNeueFontFamily,
                    fontSize = 18.sp,
                    color = Color.Black
                )
            }
        }
    }
}
