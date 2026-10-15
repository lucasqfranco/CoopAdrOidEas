package com.example.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.example.R

val BebasNeueFontFamily = FontFamily(
    Font(R.font.bebas_neue, FontWeight.Normal)
)

val OutfitFontFamily = FontFamily(
    Font(R.font.outfit, FontWeight.Normal)
)

val AppTypography = Typography(
    displayLarge = TextStyle(
        fontFamily = BebasNeueFontFamily,
        fontWeight = FontWeight.Normal,
        fontSize = 44.sp,
        lineHeight = 48.sp,
        letterSpacing = 1.sp
    ),
    displayMedium = TextStyle(
        fontFamily = BebasNeueFontFamily,
        fontWeight = FontWeight.Normal,
        fontSize = 36.sp,
        lineHeight = 40.sp,
        letterSpacing = 0.5.sp
    ),
    headlineLarge = TextStyle(
        fontFamily = BebasNeueFontFamily,
        fontWeight = FontWeight.Normal,
        fontSize = 28.sp,
        lineHeight = 32.sp,
        letterSpacing = 0.5.sp
    ),
    headlineMedium = TextStyle(
        fontFamily = BebasNeueFontFamily,
        fontWeight = FontWeight.Normal,
        fontSize = 22.sp,
        lineHeight = 26.sp,
        letterSpacing = 0.5.sp
    ),
    titleLarge = TextStyle(
        fontFamily = BebasNeueFontFamily,
        fontWeight = FontWeight.Normal,
        fontSize = 20.sp,
        lineHeight = 24.sp,
        letterSpacing = 0.5.sp
    ),
    titleMedium = TextStyle(
        fontFamily = OutfitFontFamily,
        fontWeight = FontWeight.Bold,
        fontSize = 16.sp,
        lineHeight = 22.sp,
        letterSpacing = 0.15.sp
    ),
    bodyLarge = TextStyle(
        fontFamily = OutfitFontFamily,
        fontWeight = FontWeight.Normal,
        fontSize = 15.sp,
        lineHeight = 22.sp,
        letterSpacing = 0.25.sp
    ),
    bodyMedium = TextStyle(
        fontFamily = OutfitFontFamily,
        fontWeight = FontWeight.Normal,
        fontSize = 13.sp,
        lineHeight = 18.sp,
        letterSpacing = 0.25.sp
    ),
    labelLarge = TextStyle(
        fontFamily = BebasNeueFontFamily,
        fontWeight = FontWeight.Normal,
        fontSize = 15.sp,
        lineHeight = 18.sp,
        letterSpacing = 0.5.sp
    ),
    labelMedium = TextStyle(
        fontFamily = OutfitFontFamily,
        fontWeight = FontWeight.Medium,
        fontSize = 11.sp,
        lineHeight = 14.sp,
        letterSpacing = 0.5.sp
    )
)
