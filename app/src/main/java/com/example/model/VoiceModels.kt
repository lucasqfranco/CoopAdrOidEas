package com.example.model

enum class VoiceTransmissionMode(val title: String, val description: String) {
    PUSH_TO_TALK("Push-to-Talk (PTT)", "Press and hold button to transmit voice with radio click cues"),
    VOICE_ACTIVATED("Voice Activity (VAD)", "Automatically transmits when microphone detects speech threshold")
}

enum class RadioFilterEffect(val title: String, val description: String) {
    OFF("Studio Crisp", "Direct clean audio signal with minimal processing"),
    POLICE_SCANNER("San Andreas Police Scanner", "Bandpass 300Hz-3.4kHz with radio squelch & dispatcher chirps"),
    CB_RADIO("Grove Street Walkie-Talkie", "Gritty analog CB radio with authentic mic-click chirps"),
    HELMET_COMMS("Tactical SWAT Intercom", "Compressed headset comms for pilot and drive-by operations")
}

data class VoiceChatState(
    val isConnected: Boolean = true,
    val isMicMuted: Boolean = false,
    val isDeafened: Boolean = false,
    val isPttPressed: Boolean = false,
    val transmissionMode: VoiceTransmissionMode = VoiceTransmissionMode.PUSH_TO_TALK,
    val radioFilter: RadioFilterEffect = RadioFilterEffect.CB_RADIO,
    val vadThresholdPercent: Int = 35,
    val liveMicLevelDb: Int = 0, // 0 to 100 dB indicator
    val masterVolumePercent: Int = 85,
    val proximitySimulationEnabled: Boolean = true,
    val activeSpeakerNames: Set<String> = emptySet(),
    val totalPacketsTransmitted: Long = 0L,
    val roundTripLatencyMs: Int = 22
)
