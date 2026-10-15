package com.example.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Security
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.model.InstallationIntegrityReport
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkBorder
import com.example.ui.theme.DarkSurfaceCard
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold

@Composable
fun InstallStatusCard(
    report: InstallationIntegrityReport,
    onLaunchGame: () -> Unit,
    onOpenInstaller: (() -> Unit)? = null,
    modifier: Modifier = Modifier
) {
    Card(
        modifier = modifier
            .fillMaxWidth()
            .testTag("install_status_card"),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, DarkBorder)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(36.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(GroveGreen.copy(alpha = 0.2f)),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Default.Security,
                            contentDescription = "Safe",
                            tint = GroveGreen,
                            modifier = Modifier.size(20.dp)
                        )
                    }
                    Spacer(modifier = Modifier.width(10.dp))
                    Column {
                        Text(
                            text = report.edition.title,
                            style = MaterialTheme.typography.titleMedium,
                            color = Color.White
                        )
                        Text(
                            text = report.detectedPath,
                            style = MaterialTheme.typography.bodyMedium,
                            color = Color.Gray
                        )
                    }
                }

                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(GroveGreen.copy(alpha = 0.2f))
                        .padding(horizontal = 8.dp, vertical = 4.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.Check,
                            contentDescription = "Active",
                            tint = GroveGreen,
                            modifier = Modifier.size(14.dp)
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = "NO INVASIVO",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 12.sp,
                            color = GroveGreen
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Non-invasive bullet points
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp))
                    .background(Color(0xFF111E14))
                    .padding(10.dp),
                horizontalArrangement = Arrangement.SpaceAround
            ) {
                StatusPill("Texturas intactas", true)
                StatusPill("Vehículos mods OK", true)
                StatusPill("CLEO seguro", true)
            }

            Spacer(modifier = Modifier.height(14.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                if (onOpenInstaller != null) {
                    androidx.compose.material3.OutlinedButton(
                        onClick = onOpenInstaller,
                        modifier = Modifier
                            .weight(1f)
                            .height(46.dp),
                        shape = RoundedCornerShape(10.dp),
                        border = androidx.compose.foundation.BorderStroke(1.dp, GroveGreen)
                    ) {
                        Text(
                            text = "INSTALADOR MOD",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 15.sp,
                            color = GroveGreen
                        )
                    }
                }

                Button(
                    onClick = onLaunchGame,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = SanAndreasGold,
                        contentColor = Color.Black
                    ),
                    modifier = Modifier
                        .weight(1.3f)
                        .height(46.dp)
                        .testTag("launch_game_button"),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.PlayArrow,
                        contentDescription = null,
                        modifier = Modifier.size(20.dp)
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = "ABRIR GTA SA",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 16.sp
                    )
                }
            }
        }
    }
}

@Composable
private fun StatusPill(label: String, isOk: Boolean) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .size(6.dp)
                .clip(androidx.compose.foundation.shape.CircleShape)
                .background(if (isOk) GroveGreen else SanAndreasGold)
        )
        Spacer(modifier = Modifier.width(4.dp))
        Text(
            text = label,
            fontSize = 11.sp,
            color = Color(0xFFD1D5DB)
        )
    }
}
