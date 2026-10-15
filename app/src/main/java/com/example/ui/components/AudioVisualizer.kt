package com.example.ui.components

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.example.ui.theme.GroveGreen
import com.example.ui.theme.SanAndreasGold
import com.example.ui.theme.WantedRed
import kotlin.random.Random

@Composable
fun AudioVisualizer(
    levelDb: Int,
    isTransmitting: Boolean,
    modifier: Modifier = Modifier,
    barCount: Int = 24
) {
    val animatedFactor = remember { Animatable(0f) }

    LaunchedEffect(isTransmitting) {
        if (isTransmitting) {
            animatedFactor.animateTo(
                targetValue = 1f,
                animationSpec = infiniteRepeatable(
                    animation = tween(220, easing = LinearEasing),
                    repeatMode = RepeatMode.Reverse
                )
            )
        } else {
            animatedFactor.snapTo(0f)
        }
    }

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(Color(0xFF0D1610))
            .padding(horizontal = 12.dp, vertical = 8.dp)
            .testTag("audio_visualizer")
    ) {
        Canvas(modifier = Modifier.fillMaxWidth().height(40.dp)) {
            val canvasWidth = size.width
            val canvasHeight = size.height
            val barSpacing = 4.dp.toPx()
            val totalSpacing = barSpacing * (barCount - 1)
            val barWidth = (canvasWidth - totalSpacing) / barCount

            val baseNormalized = (levelDb / 100f).coerceIn(0.05f, 1f)

            for (i in 0 until barCount) {
                // Symmetrical wave pattern
                val distFromCenter = kotlin.math.abs(i - barCount / 2f) / (barCount / 2f)
                val waveHeightRatio = (1f - distFromCenter * 0.6f)

                val randomJitter = if (isTransmitting) {
                    ((i * 17) % 7) * 0.05f * animatedFactor.value
                } else 0f

                val calculatedHeight = if (isTransmitting) {
                    (canvasHeight * baseNormalized * waveHeightRatio * (0.6f + randomJitter))
                        .coerceIn(4.dp.toPx(), canvasHeight)
                } else {
                    4.dp.toPx()
                }

                val x = i * (barWidth + barSpacing)
                val y = (canvasHeight - calculatedHeight) / 2f

                val barColor = when {
                    !isTransmitting -> Color(0xFF223627)
                    levelDb > 75 -> WantedRed
                    levelDb > 40 -> SanAndreasGold
                    else -> GroveGreen
                }

                drawRoundRect(
                    color = barColor,
                    topLeft = Offset(x, y),
                    size = Size(barWidth, calculatedHeight),
                    cornerRadius = CornerRadius(barWidth / 2f, barWidth / 2f)
                )
            }
        }
    }
}
