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
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.HelpOutline
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.installer.DiagnosticCheckItem
import com.example.installer.DiagnosticItemStatus
import com.example.installer.DiagnosticVerdict
import com.example.installer.ModDiagnosticReport
import com.example.ui.theme.ArmorBlue
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkBorder
import com.example.ui.theme.DarkSurfaceCard
import com.example.ui.theme.DarkSurfaceElevated
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.ui.theme.WantedRed

@Composable
fun ModDiagnosticCard(
    report: ModDiagnosticReport?,
    isChecking: Boolean,
    onRunDiagnostics: () -> Unit,
    onPickSafFolder: () -> Unit,
    onOpenZArchiver: () -> Unit,
    onLaunchGame: () -> Unit,
    onInstallCleo: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    var isChecklistExpanded by remember { mutableStateOf(false) }
    var isExplanationExpanded by remember { mutableStateOf(false) }

    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(
            1.5.dp,
            when (report?.verdict) {
                DiagnosticVerdict.READY_TO_PLAY -> GroveGreen
                DiagnosticVerdict.PENDING_COPY_TO_GTA -> WantedRed
                DiagnosticVerdict.CONFIG_MISMATCH -> SanAndreasGold
                else -> SanAndreasGold.copy(alpha = 0.6f)
            }
        )
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            // ENCABEZADO CON BOTÓN DE EJECUTAR
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(28.dp)
                            .clip(CircleShape)
                            .background(SanAndreasGold.copy(alpha = 0.2f)),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Default.Info,
                            contentDescription = null,
                            tint = SanAndreasGold,
                            modifier = Modifier.size(18.dp)
                        )
                    }
                    Spacer(modifier = Modifier.width(10.dp))
                    Column {
                        Text(
                            text = "DIAGNÓSTICO AUTOMÁTICO DEL MOD",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 17.sp,
                            color = Color.White
                        )
                        Text(
                            text = if (report != null) "Última comprobación: ${report.timestamp}" else "¿Por qué no se unen? Comprueba en 1 clic",
                            style = MaterialTheme.typography.bodySmall,
                            color = Color.Gray
                        )
                    }
                }

                Button(
                    onClick = onRunDiagnostics,
                    enabled = !isChecking,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (report == null) GroveGreen else SanAndreasGold,
                        contentColor = Color.Black
                    ),
                    shape = RoundedCornerShape(8.dp),
                    modifier = Modifier.height(36.dp)
                ) {
                    if (isChecking) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            color = Color.Black,
                            strokeWidth = 2.dp
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("REVISANDO...", fontFamily = BebasNeueFontFamily, fontSize = 12.sp)
                    } else {
                        Icon(
                            imageVector = Icons.Default.Refresh,
                            contentDescription = null,
                            modifier = Modifier.size(16.dp)
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = if (report == null) "COMPROBAR" else "RE-VERIFICAR",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 13.sp
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // SI AÚN NO SE HA EJECUTADO EL DIAGNÓSTICO
            if (report == null) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(Color(0xFF131D16))
                        .padding(12.dp)
                ) {
                    Column {
                        Text(
                            text = "🔍 ¿Los teléfonos se detectaron en la sala pero no en la partida?",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 14.sp,
                            color = SanAndreasGold
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = "Toca el botón 'COMPROBAR' arriba para revisar automáticamente si los scripts están pegados en la carpeta de GTA San Andreas, si la IP del host está bien configurada y cómo activar la skin dentro del juego.",
                            style = MaterialTheme.typography.bodySmall,
                            color = Color(0xFFD1D5DB)
                        )
                    }
                }
            } else {
                // VEREDICTO GENERAL DESTACADO
                val bannerBg = when (report.verdict) {
                    DiagnosticVerdict.READY_TO_PLAY -> Color(0xFF102616)
                    DiagnosticVerdict.PENDING_COPY_TO_GTA -> Color(0xFF2A1212)
                    DiagnosticVerdict.CONFIG_MISMATCH -> Color(0xFF281C10)
                    else -> Color(0xFF201A10)
                }
                val bannerBorder = when (report.verdict) {
                    DiagnosticVerdict.READY_TO_PLAY -> GroveGreen
                    DiagnosticVerdict.PENDING_COPY_TO_GTA -> WantedRed
                    DiagnosticVerdict.CONFIG_MISMATCH -> SanAndreasGold
                    else -> ArmorBlue
                }

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(bannerBg)
                        .border(1.dp, bannerBorder, RoundedCornerShape(10.dp))
                        .padding(12.dp)
                ) {
                    Column {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = when (report.verdict) {
                                    DiagnosticVerdict.READY_TO_PLAY -> Icons.Default.CheckCircle
                                    DiagnosticVerdict.PENDING_COPY_TO_GTA -> Icons.Default.Warning
                                    DiagnosticVerdict.CONFIG_MISMATCH -> Icons.Default.Warning
                                    else -> Icons.Default.Info
                                },
                                contentDescription = null,
                                tint = bannerBorder,
                                modifier = Modifier.size(20.dp)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = report.headline,
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 16.sp,
                                color = bannerBorder
                            )
                        }
                        Spacer(modifier = Modifier.height(6.dp))
                        Text(
                            text = report.detailExplanation,
                            style = MaterialTheme.typography.bodyMedium,
                            color = Color.White
                        )
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

                // BOTONES DE ACCIÓN RÁPIDA SEGÚN VEREDICTO
                if (report.verdict == DiagnosticVerdict.PENDING_COPY_TO_GTA) {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Button(
                            onClick = onPickSafFolder,
                            colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.Black),
                            modifier = Modifier.fillMaxWidth().height(44.dp),
                            shape = RoundedCornerShape(10.dp)
                        ) {
                            Icon(Icons.Default.Folder, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "📁 COPIAR A GTA SA CON SELECTOR DE CARPETAS",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 14.sp
                            )
                        }

                        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedButton(
                                onClick = onOpenZArchiver,
                                modifier = Modifier.weight(1f).height(40.dp),
                                shape = RoundedCornerShape(8.dp),
                                border = androidx.compose.foundation.BorderStroke(1.dp, SanAndreasGold)
                            ) {
                                Text("📂 ABRIR ZARCHIVER", fontFamily = BebasNeueFontFamily, fontSize = 12.sp, color = SanAndreasGold)
                            }

                            Button(
                                onClick = onLaunchGame,
                                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1E3A24), contentColor = Color.White),
                                modifier = Modifier.weight(1f).height(40.dp),
                                shape = RoundedCornerShape(8.dp)
                            ) {
                                Icon(Icons.Default.PlayArrow, contentDescription = null, modifier = Modifier.size(16.dp))
                                Spacer(modifier = Modifier.width(4.dp))
                                Text("ABRIR GTA SA", fontFamily = BebasNeueFontFamily, fontSize = 12.sp)
                            }
                        }
                    }
                    Spacer(modifier = Modifier.height(10.dp))
                }

                // Si falta CLEO en mods/, ofrecer botón directo de instalación en 1 clic
                val hasCleoError = report.checks.any { it.title.contains("CLEO", ignoreCase = true) && it.status == DiagnosticItemStatus.ERROR }
                if (hasCleoError) {
                    Button(
                        onClick = onInstallCleo,
                        colors = ButtonDefaults.buttonColors(containerColor = GroveGreen, contentColor = Color.White),
                        modifier = Modifier.fillMaxWidth().height(44.dp),
                        shape = RoundedCornerShape(10.dp),
                        border = androidx.compose.foundation.BorderStroke(1.5.dp, SanAndreasGold)
                    ) {
                        Icon(Icons.Default.Download, contentDescription = null, tint = SanAndreasGold, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(
                            text = "⚡ INSTALAR CLEO EN MODS/ (1 CLICK TODO-EN-UNO)",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 13.sp
                        )
                    }
                    Spacer(modifier = Modifier.height(10.dp))
                }

                if (report.verdict == DiagnosticVerdict.READY_TO_PLAY) {
                    Button(
                        onClick = onLaunchGame,
                        colors = ButtonDefaults.buttonColors(containerColor = SanAndreasGold, contentColor = Color.Black),
                        modifier = Modifier.fillMaxWidth().height(46.dp),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Icon(Icons.Default.PlayArrow, contentDescription = null, modifier = Modifier.size(20.dp))
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("▶ ABRIR GTA SAN ANDREAS AHORA", fontFamily = BebasNeueFontFamily, fontSize = 16.sp)
                    }
                    Spacer(modifier = Modifier.height(10.dp))
                }

                // CAJA EXPLICATIVA: ¿POR QUÉ NO APARECIÓ EL INVITADO AL REANUDAR?
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF18151E)),
                    shape = RoundedCornerShape(10.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, Color(0xFF7C3AED).copy(alpha = 0.5f))
                ) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { isExplanationExpanded = !isExplanationExpanded },
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Icon(Icons.Default.HelpOutline, contentDescription = null, tint = Color(0xFFA78BFA), modifier = Modifier.size(18.dp))
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(
                                    text = "¿POR QUÉ NO ABRE EL MENÚ CLEO O NO SE UNEN?",
                                    fontFamily = BebasNeueFontFamily,
                                    fontSize = 14.sp,
                                    color = Color(0xFFA78BFA)
                                )
                            }
                            Text(
                                text = if (isExplanationExpanded) "▲ OCULTAR" else "▼ DESPLEGAR RESPUESTA",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 11.sp,
                                color = Color(0xFFA78BFA)
                            )
                        }

                        AnimatedVisibility(visible = isExplanationExpanded) {
                            Column(modifier = Modifier.padding(top = 8.dp)) {
                                Text(
                                    text = report.whyGuestDidNotJoinNote,
                                    style = MaterialTheme.typography.bodySmall,
                                    color = Color(0xFFE2E8F0),
                                    lineHeight = 18.sp
                                )
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

                // CHECKLIST DE 6 PUNTOS
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = DarkSurfaceElevated),
                    shape = RoundedCornerShape(10.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
                ) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { isChecklistExpanded = !isChecklistExpanded },
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "DETALLE DE VERIFICACIÓN (${report.checks.size} PUNTOS)",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 13.sp,
                                color = Color.LightGray
                            )
                            Text(
                                text = if (isChecklistExpanded) "▲ OCULTAR" else "▼ DESPLEGAR",
                                fontFamily = BebasNeueFontFamily,
                                fontSize = 11.sp,
                                color = GroveGreen
                            )
                        }

                        AnimatedVisibility(visible = isChecklistExpanded) {
                            Column(
                                modifier = Modifier.padding(top = 8.dp),
                                verticalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                report.checks.forEach { checkItem ->
                                    DiagnosticCheckRow(item = checkItem)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun DiagnosticCheckRow(item: DiagnosticCheckItem) {
    val statusColor = when (item.status) {
        DiagnosticItemStatus.SUCCESS -> GroveGreen
        DiagnosticItemStatus.WARNING -> SanAndreasGold
        DiagnosticItemStatus.ERROR -> WantedRed
        DiagnosticItemStatus.INFO -> ArmorBlue
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF0F1512))
            .padding(10.dp),
        verticalAlignment = Alignment.Top
    ) {
        Box(
            modifier = Modifier
                .size(20.dp)
                .clip(CircleShape)
                .background(statusColor.copy(alpha = 0.2f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = when (item.status) {
                    DiagnosticItemStatus.SUCCESS -> Icons.Default.Check
                    DiagnosticItemStatus.WARNING -> Icons.Default.Warning
                    DiagnosticItemStatus.ERROR -> Icons.Default.Close
                    DiagnosticItemStatus.INFO -> Icons.Default.Info
                },
                contentDescription = null,
                tint = statusColor,
                modifier = Modifier.size(13.dp)
            )
        }

        Spacer(modifier = Modifier.width(10.dp))

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = item.title,
                fontFamily = BebasNeueFontFamily,
                fontSize = 13.sp,
                color = Color.White
            )
            Spacer(modifier = Modifier.height(2.dp))
            Text(
                text = item.description,
                style = MaterialTheme.typography.bodySmall,
                color = Color(0xFFCBD5E1),
                fontSize = 11.sp
            )
            if (item.actionHint != null) {
                Spacer(modifier = Modifier.height(3.dp))
                Text(
                    text = "👉 ${item.actionHint}",
                    style = MaterialTheme.typography.bodySmall,
                    color = SanAndreasGold,
                    fontSize = 11.sp
                )
            }
        }
    }
}
