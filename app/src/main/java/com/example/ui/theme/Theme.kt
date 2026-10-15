package com.example.ui.theme

import android.app.Activity
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val CoopAndreasColorScheme = darkColorScheme(
    primary = GroveGreen,
    onPrimary = DarkSurfaceBase,
    primaryContainer = GroveGreenContainer,
    onPrimaryContainer = OnGroveGreenContainer,

    secondary = SanAndreasGold,
    onSecondary = DarkSurfaceBase,
    secondaryContainer = SanAndreasGoldContainer,
    onSecondaryContainer = OnSanAndreasGoldContainer,

    tertiary = ArmorBlue,
    onTertiary = DarkSurfaceBase,

    background = DarkSurfaceBase,
    onBackground = TextPrimary,

    surface = DarkSurfaceElevated,
    onSurface = TextPrimary,
    surfaceVariant = DarkSurfaceCard,
    onSurfaceVariant = TextSecondary,

    outline = DarkBorder,
    error = WantedRed,
    onError = TextPrimary
)

@Composable
fun MyApplicationTheme(
    darkTheme: Boolean = true,
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit
) {
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            window.statusBarColor = DarkSurfaceBase.toArgb()
            window.navigationBarColor = DarkSurfaceBase.toArgb()
            WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = false
            WindowCompat.getInsetsController(window, view).isAppearanceLightNavigationBars = false
        }
    }

    MaterialTheme(
        colorScheme = CoopAndreasColorScheme,
        typography = AppTypography,
        content = content
    )
}
