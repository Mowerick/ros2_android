package com.github.mowerick.ros2.android.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// Material 3 tonal palette generated from seed color Teal/Cyan #00BCD4
// Dark theme: primary uses tone 80, container uses tone 30, surface uses neutral tones

private val DarkColorScheme = darkColorScheme(
    primary = Color(0xFF4DD0E1),           // Cyan 300 - main interactive elements
    onPrimary = Color(0xFF00363D),         // Dark teal - text on primary
    primaryContainer = Color(0xFF004F58),  // Dark cyan - filled containers
    onPrimaryContainer = Color(0xFF97F0FF),// Light cyan - text on primary container

    secondary = Color(0xFFB1CBD0),         // Muted teal-grey - secondary elements
    onSecondary = Color(0xFF1C3438),       // Dark - text on secondary
    secondaryContainer = Color(0xFF334B4F),// Muted dark teal - secondary containers
    onSecondaryContainer = Color(0xFFCDE7EC),// Light - text on secondary container

    tertiary = Color(0xFFB5C6E0),         // Blue-grey - tertiary accents
    onTertiary = Color(0xFF1F3044),        // Dark blue - text on tertiary
    tertiaryContainer = Color(0xFF36475B), // Dark blue-grey - tertiary containers
    onTertiaryContainer = Color(0xFFD1E2FC),// Light blue - text on tertiary container

    error = Color(0xFFFFB4AB),
    onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A),
    onErrorContainer = Color(0xFFFFDAD6),

    background = Color(0xFF191C1D),        // Near-black with slight teal tint
    onBackground = Color(0xFFE1E3E3),
    surface = Color(0xFF191C1D),
    onSurface = Color(0xFFE1E3E3),
    surfaceVariant = Color(0xFF3F484A),    // Elevated surfaces
    onSurfaceVariant = Color(0xFFBFC8CA),
    outline = Color(0xFF899294),           // Borders, dividers
    outlineVariant = Color(0xFF3F484A)     // Subtle borders
)

@Composable
fun Ros2AndroidTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColorScheme,
        content = content
    )
}
