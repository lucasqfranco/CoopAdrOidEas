package com.example.ui.screens

import android.view.MotionEvent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
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
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material.icons.filled.Radio
import androidx.compose.material.icons.filled.VolumeDown
import androidx.compose.material.icons.filled.VolumeOff
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.model.RadioFilterEffect
import com.example.model.VoiceTransmissionMode
import com.example.ui.components.AudioVisualizer
import com.example.ui.theme.ArmorBlue
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkBorder
import com.example.ui.theme.DarkSurfaceCard
import com.example.ui.theme.DarkSurfaceElevated
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.ui.theme.WantedRed
import com.example.viewmodel.CoopMainViewModel

@OptIn(ExperimentalComposeUiApi::class)
@Composable
fun VoiceChatScreen(
    viewModel: CoopMainViewModel,
    modifier: Modifier = Modifier
) {
    val voiceState by viewModel.voiceState.collectAsState()
    val session by viewModel.sessionState.collectAsState()

    LazyColumn(
        modifier = modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp)
            .testTag("voice_chat_screen"),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            Spacer(modifier = Modifier.height(8.dp))
            // Radio Intercom Station Tuner Card
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag("radio_station_card"),
                colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
                shape = RoundedCornerShape(20.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, GroveGreen.copy(alpha = 0.6f))
            ) {
                Column(
                    modifier = Modifier.padding(18.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = Icons.Default.Radio,
                                contentDescription = null,
                                tint = GroveGreen,
                                modifier = Modifier.size(24.dp)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = "CANAL DE RADIO TÁCTICO",
                                style = MaterialTheme.typography.labelLarge,
                                color = SanAndreasGold
                            )
                        }

                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(6.dp))
                                .background(GroveGreen.copy(alpha = 0.2f))
                                .padding(horizontal = 8.dp, vertical = 2.dp)
                        ) {
                            Text(
                                text = "${voiceState.roundTripLatencyMs}ms LATENCIA",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 12.sp,
                                color = GroveGreen
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    // Frequency readout
                    Text(
                        text = "104.7 MHz • GROVE INTERCOM",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 28.sp,
                        color = Color.White,
                        letterSpacing = 1.sp
                    )

                    Text(
                        text = "Sincronizado durante misiones en curso",
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color.Gray
                    )

                    Spacer(modifier = Modifier.height(16.dp))

                    // Live Audio Visualizer
                    AudioVisualizer(
                        levelDb = voiceState.liveMicLevelDb,
                        isTransmitting = voiceState.isPttPressed ||
                                (voiceState.transmissionMode == VoiceTransmissionMode.VOICE_ACTIVATED && voiceState.liveMicLevelDb > voiceState.vadThresholdPercent)
                    )

                    Spacer(modifier = Modifier.height(8.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "Nivel de entrada: ${voiceState.liveMicLevelDb} dB",
                            style = MaterialTheme.typography.bodyMedium,
                            color = Color.LightGray
                        )
                        Text(
                            text = if (voiceState.isPttPressed) "TRANSMITIENDO EN VIVO" else "EN ESPERA",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 13.sp,
                            color = if (voiceState.isPttPressed) GroveGreen else Color.Gray
                        )
                    }
                }
            }
        }

        // Tactical Big Push-to-Talk (PTT) Button
        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
                shape = RoundedCornerShape(20.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(20.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Text(
                        text = "PULSADOR DE COMUNICACIÓN (PTT)",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 17.sp,
                        color = SanAndreasGold
                    )
                    Text(
                        text = "Mantén presionado para hablar en radio con el equipo",
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color.Gray
                    )

                    Spacer(modifier = Modifier.height(18.dp))

                    // Large Circular PTT Button with Touch Hold Listener
                    Box(
                        modifier = Modifier
                            .size(140.dp)
                            .clip(CircleShape)
                            .background(
                                if (voiceState.isPttPressed) GroveGreen
                                else if (voiceState.isMicMuted) WantedRed.copy(alpha = 0.4f)
                                else Color(0xFF1B3822)
                            )
                            .border(
                                4.dp,
                                if (voiceState.isPttPressed) Color.White else GroveGreen,
                                CircleShape
                            )
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
                            }
                            .testTag("ptt_button"),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(
                                imageVector = if (voiceState.isMicMuted) Icons.Default.MicOff else Icons.Default.Mic,
                                contentDescription = "PTT Trigger",
                                tint = if (voiceState.isPttPressed) Color.Black else Color.White,
                                modifier = Modifier.size(46.dp)
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                text = if (voiceState.isPttPressed) "HABLANDO" else "PULSAR",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 17.sp,
                                color = if (voiceState.isPttPressed) Color.Black else Color.White
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(18.dp))

                    // Quick Mute / Deafen controls
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceEvenly
                    ) {
                        Button(
                            onClick = { viewModel.toggleMute() },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (voiceState.isMicMuted) WantedRed else Color(0xFF182A1D)
                            ),
                            shape = RoundedCornerShape(10.dp)
                        ) {
                            Icon(
                                imageVector = if (voiceState.isMicMuted) Icons.Default.MicOff else Icons.Default.Mic,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = if (voiceState.isMicMuted) "MIC SILENCIADO" else "SILENCIAR MIC",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 14.sp
                            )
                        }

                        Button(
                            onClick = { viewModel.toggleDeafen() },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (voiceState.isDeafened) WantedRed else Color(0xFF182A1D)
                            ),
                            shape = RoundedCornerShape(10.dp)
                        ) {
                            Icon(
                                imageVector = if (voiceState.isDeafened) Icons.Default.VolumeOff else Icons.Default.Headphones,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = if (voiceState.isDeafened) "ENSORDIDO" else "ENSORDESER",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 14.sp
                            )
                        }
                    }
                }
            }
        }

        // Radio Filter Effect Card
        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
                shape = RoundedCornerShape(16.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text(
                        text = "EFECTO DE RADIO & FILTRO DE AUDIO",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 16.sp,
                        color = SanAndreasGold
                    )
                    Text(
                        text = "Simula el audio analógico de walkie-talkie / escáner policial",
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color.Gray
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    RadioFilterEffect.values().forEach { filter ->
                        val isSelected = voiceState.radioFilter == filter
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(8.dp))
                                .background(if (isSelected) GroveGreen.copy(alpha = 0.15f) else Color.Transparent)
                                .border(
                                    1.dp,
                                    if (isSelected) GroveGreen else DarkBorder,
                                    RoundedCornerShape(8.dp)
                                )
                                .padding(10.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = filter.title,
                                    style = MaterialTheme.typography.titleMedium,
                                    color = if (isSelected) GroveGreen else Color.White
                                )
                                Text(
                                    text = filter.description,
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = Color.Gray
                                )
                            }
                            Button(
                                onClick = { viewModel.setRadioFilter(filter) },
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = if (isSelected) GroveGreen else Color(0xFF1B2E21),
                                    contentColor = if (isSelected) Color.Black else Color.White
                                ),
                                shape = RoundedCornerShape(6.dp)
                            ) {
                                Text(
                                    text = if (isSelected) "ACTIVO" else "ACTIVAR",
                                    fontFamily = BebasNeueFontFamily,
                                    fontSize = 13.sp
                                )
                            }
                        }
                        Spacer(modifier = Modifier.height(6.dp))
                    }
                }
            }
        }

        // Voice Transmission Settings & Proximity
        item {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
                shape = RoundedCornerShape(16.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text(
                        text = "MODO DE TRANSMISIÓN & PROXIMIDAD",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 16.sp,
                        color = SanAndreasGold
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = "Activación por Voz (VAD)",
                                style = MaterialTheme.typography.titleMedium,
                                color = Color.White
                            )
                            Text(
                                text = "Transmite automáticamente sin pulsar el botón",
                                style = MaterialTheme.typography.bodyMedium,
                                color = Color.Gray
                            )
                        }
                        Switch(
                            checked = voiceState.transmissionMode == VoiceTransmissionMode.VOICE_ACTIVATED,
                            onCheckedChange = { checked ->
                                viewModel.setTransmissionMode(
                                    if (checked) VoiceTransmissionMode.VOICE_ACTIVATED
                                    else VoiceTransmissionMode.PUSH_TO_TALK
                                )
                            },
                            colors = SwitchDefaults.colors(
                                checkedThumbColor = GroveGreen,
                                checkedTrackColor = Color(0xFF17381E)
                            )
                        )
                    }

                    if (voiceState.transmissionMode == VoiceTransmissionMode.VOICE_ACTIVATED) {
                        Spacer(modifier = Modifier.height(10.dp))
                        Text(
                            text = "Sensibilidad VAD: ${voiceState.vadThresholdPercent}%",
                            style = MaterialTheme.typography.bodyMedium,
                            color = GroveGreen
                        )
                        Slider(
                            value = voiceState.vadThresholdPercent.toFloat(),
                            onValueChange = { viewModel.setVadThreshold(it.toInt()) },
                            valueRange = 10f..80f,
                            colors = SliderDefaults.colors(
                                thumbColor = GroveGreen,
                                activeTrackColor = GroveGreen
                            )
                        )
                    }

                    Spacer(modifier = Modifier.height(14.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = "Audio Espacial por Proximidad",
                                style = MaterialTheme.typography.titleMedium,
                                color = Color.White
                            )
                            Text(
                                text = "Atenúa la voz según distancia en el mapa de SA",
                                style = MaterialTheme.typography.bodyMedium,
                                color = Color.Gray
                            )
                        }
                        Switch(
                            checked = voiceState.proximitySimulationEnabled,
                            onCheckedChange = { viewModel.toggleProximity() },
                            colors = SwitchDefaults.colors(
                                checkedThumbColor = GroveGreen,
                                checkedTrackColor = Color(0xFF17381E)
                            )
                        )
                    }

                    Spacer(modifier = Modifier.height(14.dp))

                    Text(
                        text = "Volumen Maestro del Equipo: ${voiceState.masterVolumePercent}%",
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color.LightGray
                    )
                    Slider(
                        value = voiceState.masterVolumePercent.toFloat(),
                        onValueChange = { viewModel.setMasterVolume(it.toInt()) },
                        valueRange = 0f..100f,
                        colors = SliderDefaults.colors(
                            thumbColor = SanAndreasGold,
                            activeTrackColor = SanAndreasGold
                        )
                    )
                }
            }
            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}
