package com.example.audio

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.util.Base64
import com.example.model.NetworkConnectionMode
import com.example.model.RadioFilterEffect
import com.example.model.VoiceChatState
import com.example.model.VoiceTransmissionMode
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CopyOnWriteArraySet
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Motor de voz táctico P2P para CoopAndreas.
 * Soporta de forma transparente:
 *  a) Modo LAN: Streaming de paquetes PCM de baja latencia por UDP en el puerto 8990.
 *  b) Modo Online a Distancia: Empaquetado y retransmisión de tramas de voz asociadas al
 *     código de sala (roomCode) vía UDP Relay / WebSockets / Firestore sin abrir puertos.
 *  Además, calcula atenuación espacial 3D por proximidad según las coordenadas (X, Y, Z) de GTA SA.
 */
class VoiceChatEngine(
    private val context: Context,
    private val scope: CoroutineScope
) {
    companion object {
        const val VOICE_TELEMETRY_UDP_PORT = 8990
        private const val VOICE_PACKET_HEADER = "COOP_VOICE|"
    }

    private val sampleRate = 16000
    private val channelConfig = AudioFormat.CHANNEL_IN_MONO
    private val audioFormat = AudioFormat.ENCODING_PCM_16BIT
    private val bufferSize = max(
        AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat),
        2048
    )

    private val _voiceState = MutableStateFlow(VoiceChatState())
    val voiceState: StateFlow<VoiceChatState> = _voiceState.asStateFlow()

    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private var recordingJob: Job? = null

    private var isRecordingLoopActive = false
    private var isPttHeld = false

    // Configuración de red sincronizada con CoopNetworkManager
    @Volatile
    private var networkMode: NetworkConnectionMode = NetworkConnectionMode.LAN_WIFI
    @Volatile
    private var activeRoomCode: String = "GROVE-404"
    @Volatile
    private var localPlayerId: String = "p1"
    private val lanTargetEndpoints = CopyOnWriteArraySet<String>()
    private var onlineRelayCallback: ((String) -> Unit)? = null
    private var udpSendSocket: DatagramSocket? = null

    // Distancia en metros al compañero más cercano para simulación de proximidad 3D
    @Volatile
    private var closestTeammateDistanceMeters: Float = 8.0f

    init {
        initAudioTrack()
        initUdpVoiceSocket()
    }

    private fun initUdpVoiceSocket() {
        try {
            udpSendSocket = DatagramSocket()
        } catch (e: Exception) {
            // Fallback en entorno de pruebas
        }
    }

    /**
     * Vincula el motor de voz con la topología de red activa (LAN UDP 8990 vs Online Relay por RoomCode).
     */
    fun configureNetworkTransport(
        mode: NetworkConnectionMode,
        roomCode: String,
        playerId: String = "p1",
        lanPeerIps: Collection<String> = emptyList(),
        onSendOnlineRelayFrame: ((String) -> Unit)? = null
    ) {
        networkMode = mode
        activeRoomCode = roomCode.ifBlank { "GROVE-404" }
        localPlayerId = playerId
        lanTargetEndpoints.clear()
        lanPeerIps.filter { it.isNotBlank() && it != "0.0.0.0" }.forEach { ip ->
            lanTargetEndpoints.add(ip.substringBefore(":"))
        }
        if (onSendOnlineRelayFrame != null) {
            onlineRelayCallback = onSendOnlineRelayFrame
        }
    }

    fun addLanPeerEndpoint(ip: String) {
        val cleanIp = ip.trim().substringBefore(":")
        if (cleanIp.isNotEmpty() && cleanIp != "0.0.0.0" && cleanIp != "127.0.0.1") {
            lanTargetEndpoints.add(cleanIp)
        }
    }

    fun updateProximityDistance(distanceMeters: Float) {
        closestTeammateDistanceMeters = distanceMeters.coerceAtLeast(0f)
    }

    private fun initAudioTrack() {
        try {
            val minTrackBufferSize = AudioTrack.getMinBufferSize(
                sampleRate,
                AudioFormat.CHANNEL_OUT_MONO,
                audioFormat
            )
            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(audioFormat)
                        .setSampleRate(sampleRate)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                        .build()
                )
                .setBufferSizeInBytes(max(minTrackBufferSize, 4096))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
            audioTrack?.play()
        } catch (e: Exception) {
            // AudioTrack init fallback
        }
    }

    @SuppressLint("MissingPermission")
    fun startVoiceService(hasAudioPermission: Boolean) {
        if (!hasAudioPermission) return
        stopVoiceService()

        try {
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.VOICE_COMMUNICATION,
                sampleRate,
                channelConfig,
                audioFormat,
                bufferSize
            )

            if (audioRecord?.state == AudioRecord.STATE_INITIALIZED) {
                audioRecord?.startRecording()
                isRecordingLoopActive = true
                startCaptureLoop()
            }
        } catch (e: Exception) {
            // Audio permission or hardware busy fallback
        }
    }

    fun stopVoiceService() {
        isRecordingLoopActive = false
        recordingJob?.cancel()
        recordingJob = null
        try {
            audioRecord?.stop()
            audioRecord?.release()
        } catch (e: Exception) {
            // ignore
        }
        audioRecord = null
    }

    private fun startCaptureLoop() {
        recordingJob = scope.launch(Dispatchers.IO) {
            val buffer = ShortArray(512) // Trama de 32ms a 16kHz para mínima latencia UDP
            var packetCounter = _voiceState.value.totalPacketsTransmitted

            while (isActive && isRecordingLoopActive) {
                val record = audioRecord ?: break
                val readShorts = record.read(buffer, 0, buffer.size)

                if (readShorts > 0) {
                    var sumSquares = 0.0
                    for (i in 0 until readShorts) {
                        val sample = buffer[i].toDouble()
                        sumSquares += sample * sample
                    }
                    val rms = sqrt(sumSquares / readShorts)
                    val db = if (rms > 0) {
                        (20.0 * log10(rms) / 90.0 * 100.0).toInt().coerceIn(0, 100)
                    } else 0

                    val isModePtt = _voiceState.value.transmissionMode == VoiceTransmissionMode.PUSH_TO_TALK
                    val shouldTransmit = if (isModePtt) {
                        isPttHeld && !_voiceState.value.isMicMuted
                    } else {
                        !_voiceState.value.isMicMuted && db >= _voiceState.value.vadThresholdPercent
                    }

                    if (shouldTransmit) {
                        packetCounter++
                        applyRadioFilter(buffer, readShorts, _voiceState.value.radioFilter)
                        transmitVoiceSamples(buffer, readShorts, packetCounter)
                    }

                    val updatedActiveSpeakers = if (shouldTransmit) {
                        _voiceState.value.activeSpeakerNames + "You (Local)"
                    } else {
                        _voiceState.value.activeSpeakerNames - "You (Local)"
                    }

                    _voiceState.value = _voiceState.value.copy(
                        liveMicLevelDb = if (shouldTransmit || !isModePtt) db else 0,
                        totalPacketsTransmitted = packetCounter,
                        activeSpeakerNames = updatedActiveSpeakers
                    )
                }
            }
        }
    }

    /**
     * Transmite una trama PCM de voz ya sea por UDP directo (puerto 8990 en Modo LAN)
     * o mediante el canal de retransmisión por roomCode (Modo Online a Distancia).
     */
    fun transmitVoiceSamples(samples: ShortArray, length: Int, sequenceNumber: Long = 0L) {
        try {
            val byteBuf = ByteBuffer.allocate(length * 2).order(ByteOrder.LITTLE_ENDIAN)
            for (i in 0 until length) {
                byteBuf.putShort(samples[i])
            }
            val pcmBytes = byteBuf.array()
            val encodedB64 = try {
                Base64.encodeToString(pcmBytes, Base64.NO_WRAP)
            } catch (t: Throwable) {
                java.util.Base64.getEncoder().encodeToString(pcmBytes)
            }

            val wireFrame = "$VOICE_PACKET_HEADER$activeRoomCode|$localPlayerId|$sequenceNumber|$encodedB64"

            if (networkMode == NetworkConnectionMode.LAN_WIFI) {
                val datagramBytes = wireFrame.toByteArray(Charsets.UTF_8)
                val sock = udpSendSocket
                if (sock != null && !sock.isClosed) {
                    for (peerIp in lanTargetEndpoints) {
                        try {
                            val packet = DatagramPacket(
                                datagramBytes,
                                datagramBytes.size,
                                InetAddress.getByName(peerIp),
                                VOICE_TELEMETRY_UDP_PORT
                            )
                            sock.send(packet)
                        } catch (_: Exception) {}
                    }
                }
            } else {
                // Modo Online a Distancia: Retransmisión por código de sala
                onlineRelayCallback?.invoke(wireFrame)
            }
        } catch (_: Exception) {}
    }

    /**
     * Recibe y reproduce una trama de voz entrante (desde UDP 8990 LAN o Relay Online),
     * aplicando volumen maestro y atenuación espacial por proximidad 3D en GTA San Andreas.
     */
    fun onIncomingVoicePacket(wireFrame: String) {
        if (_voiceState.value.isDeafened) return
        if (!wireFrame.startsWith(VOICE_PACKET_HEADER)) return

        val parts = wireFrame.split("|")
        if (parts.size < 5) return
        val frameRoom = parts[1]
        val senderId = parts[2]
        if (senderId == localPlayerId) return
        if (frameRoom != activeRoomCode && activeRoomCode.isNotBlank()) return

        val b64Payload = parts[4]
        scope.launch(Dispatchers.Default) {
            try {
                val pcmBytes = try {
                    Base64.decode(b64Payload, Base64.NO_WRAP)
                } catch (t: Throwable) {
                    java.util.Base64.getDecoder().decode(b64Payload)
                }
                val shortCount = pcmBytes.size / 2
                val shortBuf = ByteBuffer.wrap(pcmBytes).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
                val samples = ShortArray(shortCount)
                shortBuf.get(samples)

                // Calcular ganancia según volumen maestro y proximidad espacial 3D en SA
                val masterGain = (_voiceState.value.masterVolumePercent / 100f).coerceIn(0f, 1f)
                val proximityGain = if (_voiceState.value.proximitySimulationEnabled) {
                    // Hasta 25m se escucha al 100%; atenúa gradualmente hasta 300m (con mínimo 25% por radio)
                    when {
                        closestTeammateDistanceMeters <= 25f -> 1.0f
                        closestTeammateDistanceMeters >= 300f -> 0.28f
                        else -> 1.0f - ((closestTeammateDistanceMeters - 25f) / 275f) * 0.72f
                    }
                } else 1.0f

                val totalGain = (masterGain * proximityGain).coerceIn(0f, 1f)
                for (i in 0 until shortCount) {
                    samples[i] = (samples[i] * totalGain).toInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort()
                }

                applyRadioFilter(samples, shortCount, _voiceState.value.radioFilter)
                audioTrack?.write(samples, 0, shortCount, AudioTrack.WRITE_NON_BLOCKING)

                _voiceState.value = _voiceState.value.copy(
                    activeSpeakerNames = _voiceState.value.activeSpeakerNames + senderId
                )
            } catch (_: Exception) {}
        }
    }

    private fun applyRadioFilter(buffer: ShortArray, size: Int, filter: RadioFilterEffect) {
        when (filter) {
            RadioFilterEffect.OFF -> {
                // Raw signal
            }
            RadioFilterEffect.POLICE_SCANNER, RadioFilterEffect.CB_RADIO -> {
                for (i in 0 until size) {
                    var s = buffer[i].toInt()
                    if (s > 18000) s = 18000 + (s - 18000) / 4
                    if (s < -18000) s = -18000 + (s + 18000) / 4
                    buffer[i] = s.toShort()
                }
            }
            RadioFilterEffect.HELMET_COMMS -> {
                for (i in 0 until size) {
                    var s = buffer[i].toInt()
                    if (s > 14000) s = 14000
                    if (s < -14000) s = -14000
                    buffer[i] = s.toShort()
                }
            }
        }
    }

    fun onPttPressed() {
        isPttHeld = true
        _voiceState.value = _voiceState.value.copy(
            isPttPressed = true,
            totalPacketsTransmitted = _voiceState.value.totalPacketsTransmitted + 1
        )
        playRadioChirp(isStart = true)
    }

    fun onPttReleased() {
        isPttHeld = false
        _voiceState.value = _voiceState.value.copy(isPttPressed = false)
        playRadioChirp(isStart = false)
    }

    fun toggleMute() {
        val nextMute = !_voiceState.value.isMicMuted
        _voiceState.value = _voiceState.value.copy(isMicMuted = nextMute)
    }

    fun toggleDeafen() {
        val nextDeafen = !_voiceState.value.isDeafened
        _voiceState.value = _voiceState.value.copy(isDeafened = nextDeafen)
    }

    fun setTransmissionMode(mode: VoiceTransmissionMode) {
        _voiceState.value = _voiceState.value.copy(transmissionMode = mode)
    }

    fun setRadioFilter(filter: RadioFilterEffect) {
        _voiceState.value = _voiceState.value.copy(radioFilter = filter)
    }

    fun setVadThreshold(threshold: Int) {
        _voiceState.value = _voiceState.value.copy(vadThresholdPercent = threshold.coerceIn(5, 95))
    }

    fun setMasterVolume(volPercent: Int) {
        _voiceState.value = _voiceState.value.copy(masterVolumePercent = volPercent.coerceIn(0, 100))
    }

    fun toggleProximity() {
        val next = !_voiceState.value.proximitySimulationEnabled
        _voiceState.value = _voiceState.value.copy(proximitySimulationEnabled = next)
    }

    fun playRadioChirp(isStart: Boolean) {
        scope.launch(Dispatchers.Default) {
            try {
                val track = audioTrack ?: return@launch
                val chirpSamples = if (isStart) 800 else 1200
                val freq = if (isStart) 1600.0 else 900.0
                val chirpBuffer = ShortArray(chirpSamples)

                for (i in 0 until chirpSamples) {
                    val angle = 2.0 * Math.PI * i * freq / sampleRate
                    val envelope = if (isStart) {
                        (chirpSamples - i).toDouble() / chirpSamples
                    } else {
                        min(1.0, i.toDouble() / 200.0) * (chirpSamples - i).toDouble() / chirpSamples
                    }
                    chirpBuffer[i] = (sin(angle) * 8000 * envelope).toInt().toShort()
                }

                track.write(chirpBuffer, 0, chirpSamples, AudioTrack.WRITE_NON_BLOCKING)
            } catch (e: Exception) {
                // non-fatal sound cue
            }
        }
    }

    fun release() {
        stopVoiceService()
        try {
            udpSendSocket?.close()
        } catch (_: Exception) {}
        udpSendSocket = null
        try {
            audioTrack?.stop()
            audioTrack?.release()
        } catch (e: Exception) {
            // ignore
        }
        audioTrack = null
    }
}
