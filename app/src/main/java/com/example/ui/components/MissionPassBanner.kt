package com.example.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.fadeIn
import androidx.compose.animation.scaleIn
import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.ui.theme.BebasNeueFontFamily
import com.example.ui.theme.DarkSurfaceBase
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold

@Composable
fun MissionPassBanner(
    missionTitle: String,
    respectReward: Int,
    cashReward: Int,
    onContinue: () -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.88f))
            .padding(24.dp)
            .testTag("mission_pass_banner"),
        contentAlignment = Alignment.Center
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(20.dp))
                .border(2.dp, SanAndreasGold, RoundedCornerShape(20.dp)),
            color = Color(0xFF131F16),
            tonalElevation = 8.dp
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(28.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center
            ) {
                // Header badge
                Box(
                    modifier = Modifier
                        .size(68.dp)
                        .clip(RoundedCornerShape(34.dp))
                        .background(GroveGreen.copy(alpha = 0.2f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Default.CheckCircle,
                        contentDescription = "Passed",
                        tint = GroveGreen,
                        modifier = Modifier.size(44.dp)
                    )
                }

                Spacer(modifier = Modifier.height(16.dp))

                // Iconic GTA San Andreas text
                Text(
                    text = "MISSION PASSED!",
                    style = MaterialTheme.typography.displayMedium,
                    color = SanAndreasGold,
                    letterSpacing = 2.sp
                )

                Text(
                    text = missionTitle,
                    style = MaterialTheme.typography.titleLarge,
                    color = Color.White,
                    modifier = Modifier.padding(top = 4.dp)
                )

                Spacer(modifier = Modifier.height(24.dp))

                // Respect reward
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color(0xFF1B2E21))
                        .padding(horizontal = 20.dp, vertical = 14.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.Star,
                            contentDescription = "Respect",
                            tint = GroveGreen,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "RESPECT +",
                            fontFamily = BebasNeueFontFamily,
                            fontSize = 22.sp,
                            color = GroveGreen
                        )
                    }
                    Text(
                        text = "+$respectReward%",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 24.sp,
                        color = Color.White
                    )
                }

                Spacer(modifier = Modifier.height(10.dp))

                // Cash reward
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color(0xFF1B2E21))
                        .padding(horizontal = 20.dp, vertical = 14.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "CASH REWARD",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 20.sp,
                        color = SanAndreasGold
                    )
                    Text(
                        text = "$$cashReward",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 26.sp,
                        color = SanAndreasGold
                    )
                }

                Spacer(modifier = Modifier.height(14.dp))

                // Non-invasive save sync confirmation
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(8.dp))
                        .background(Color(0xFF0F1812))
                        .padding(horizontal = 12.dp, vertical = 8.dp)
                ) {
                    Text(
                        text = "🔒 Save slot GTASAsf8.b aligned across all co-op clients without overwriting career slot 1.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color(0xFFA7F3D0)
                    )
                }

                Spacer(modifier = Modifier.height(24.dp))

                Button(
                    onClick = onContinue,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = GroveGreen,
                        contentColor = Color.Black
                    ),
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(52.dp)
                        .testTag("continue_button"),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Text(
                        text = "RETURN TO LOBBY / NEXT MISSION",
                        fontFamily = BebasNeueFontFamily,
                        fontSize = 20.sp
                    )
                }
            }
        }
    }
}
