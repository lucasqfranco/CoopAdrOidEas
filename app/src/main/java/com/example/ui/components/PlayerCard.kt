package com.example.ui.components

import androidx.compose.animation.animateColorAsState
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MicOff
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.VpnKey
import androidx.compose.material3.Badge
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.model.CoopPlayer
import com.example.ui.theme.ArmorBlue
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkBorder
import com.example.ui.theme.DarkSurfaceCard
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.ui.theme.WantedRed

@Composable
fun PlayerCard(
    player: CoopPlayer,
    isLocalPlayer: Boolean = false,
    modifier: Modifier = Modifier
) {
    val micBorderColor by animateColorAsState(
        targetValue = if (player.isSpeaking) GroveGreen else Color.Transparent,
        label = "micBorder"
    )

    Card(
        modifier = modifier
            .fillMaxWidth()
            .testTag("player_card_${player.id}"),
        colors = CardDefaults.cardColors(containerColor = DarkSurfaceCard),
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            if (player.isSpeaking) GroveGreen else DarkBorder
        )
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp)
        ) {
            // Header Row: Avatar, Name, Role, Host/Ready Badge
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Avatar circle with skin color and speaking glow
                Box(
                    modifier = Modifier
                        .size(46.dp)
                        .clip(CircleShape)
                        .background(Color(player.skin.avatarColorHex))
                        .border(2.dp, micBorderColor, CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = player.skin.name.take(2),
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 18.sp,
                        color = Color.White
                    )
                }

                Spacer(modifier = Modifier.width(12.dp))

                Column(modifier = Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = player.name,
                            style = MaterialTheme.typography.titleMedium,
                            color = Color.White
                        )
                        if (player.isHost) {
                            Spacer(modifier = Modifier.width(6.dp))
                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(4.dp))
                                    .background(SanAndreasGold.copy(alpha = 0.2f))
                                    .padding(horizontal = 6.dp, vertical = 2.dp)
                            ) {
                                Text(
                                    text = "HOST",
                                    fontFamily = BebasNeueFontFamily,
                                    fontSize = 11.sp,
                                    color = SanAndreasGold
                                )
                            }
                        }
                    }

                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.padding(top = 2.dp)
                    ) {
                        Text(
                            text = "${player.role.icon} ${player.role.title}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = GroveGreen
                        )
                    }
                }

                // Voice status icon
                Box(
                    modifier = Modifier
                        .size(34.dp)
                        .clip(CircleShape)
                        .background(
                            if (player.isSpeaking) GroveGreen.copy(alpha = 0.25f)
                            else if (player.isMuted) WantedRed.copy(alpha = 0.15f)
                            else Color(0xFF142017)
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = if (player.isMuted) Icons.Default.MicOff else Icons.Default.Mic,
                        contentDescription = "Voice State",
                        tint = if (player.isSpeaking) GroveGreen
                        else if (player.isMuted) WantedRed
                        else Color.Gray,
                        modifier = Modifier.size(18.dp)
                    )
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // Bars: Health & Armor
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                // Health bar
                Column(modifier = Modifier.weight(1f)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "HEALTH",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 12.sp,
                            color = WantedRed
                        )
                        Text(
                            text = "${player.health}%",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 12.sp,
                            color = WantedRed
                        )
                    }
                    Spacer(modifier = Modifier.height(3.dp))
                    LinearProgressIndicator(
                        progress = { player.health / 100f },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(6.dp)
                            .clip(RoundedCornerShape(3.dp)),
                        color = WantedRed,
                        trackColor = Color(0xFF2C1515)
                    )
                }

                // Armor bar
                Column(modifier = Modifier.weight(1f)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = "ARMOR",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 12.sp,
                            color = ArmorBlue
                        )
                        Text(
                            text = "${player.armor}%",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 12.sp,
                            color = ArmorBlue
                        )
                    }
                    Spacer(modifier = Modifier.height(3.dp))
                    LinearProgressIndicator(
                        progress = { player.armor / 100f },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(6.dp)
                            .clip(RoundedCornerShape(3.dp)),
                        color = ArmorBlue,
                        trackColor = Color(0xFF14232B)
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Footer row: Current weapon, vehicle, and ping
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color(0xFF132017))
                    .padding(horizontal = 10.dp, vertical = 6.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "🔫 ${player.currentWeapon}",
                    style = MaterialTheme.typography.bodyMedium,
                    color = Color(0xFFD1D5DB)
                )

                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(7.dp)
                            .clip(CircleShape)
                            .background(if (player.pingMs < 50) GroveGreen else SanAndreasGold)
                    )
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(
                        text = "${player.pingMs}ms",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 13.sp,
                        color = if (player.pingMs < 50) GroveGreen else SanAndreasGold
                    )
                }
            }
        }
    }
}
