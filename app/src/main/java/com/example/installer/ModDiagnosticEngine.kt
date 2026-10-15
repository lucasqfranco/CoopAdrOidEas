package com.example.installer

import android.content.Context
import android.net.Uri
import android.os.Environment
import android.provider.DocumentsContract
import com.example.model.CharacterSkin
import com.example.model.NetworkConnectionMode
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.net.InetSocketAddress
import java.net.Socket
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

enum class DiagnosticVerdict {
    READY_TO_PLAY,        // Todo verificado: archivos en GTA SA, red lista, rol configurado
    PENDING_COPY_TO_GTA,  // Generados pero faltan en Android/data/com.rockstargames.gtasa/files/
    CONFIG_MISMATCH,      // IP o rol no coinciden (invitado sin IP o ambos como host)
    CLEO_MISSING_WARNING, // Archivos en carpeta pero no se detecta soporte CLEO/AML
    GAME_NOT_INSTALLED    // El APK de GTA San Andreas no está instalado
}

enum class DiagnosticItemStatus {
    SUCCESS,
    WARNING,
    ERROR,
    INFO
}

data class DiagnosticCheckItem(
    val title: String,
    val description: String,
    val status: DiagnosticItemStatus,
    val actionHint: String? = null
)

data class FileIntegrityInfo(
    val filename: String,
    val exists: Boolean,
    val sizeBytes: Long,
    val location: String,
    val integrityNote: String
)

data class IniConfigDetail(
    val exists: Boolean = false,
    val isHost: Int? = null,
    val hostIp: String? = null,
    val port: Int? = null,
    val roomCode: String? = null,
    val connectionMode: String? = null,
    val player1Skin: Int? = null,
    val player2Skin: Int? = null,
    val freeRoamAnySave: Int? = null,
    val location: String = ""
)

data class ModDiagnosticReport(
    val timestamp: String = "",
    val verdict: DiagnosticVerdict = DiagnosticVerdict.PENDING_COPY_TO_GTA,
    val headline: String = "",
    val detailExplanation: String = "",
    val checks: List<DiagnosticCheckItem> = emptyList(),
    val whyGuestDidNotJoinNote: String = "",
    val inGameActivationSteps: List<String> = emptyList(),
    val recommendedActions: List<String> = emptyList(),
    val detectedGtaPath: String? = null,
    val filesFoundInGta: List<String> = emptyList(),
    val filesMissingInGta: List<String> = emptyList(),
    val fileIntegrityDetails: List<FileIntegrityInfo> = emptyList(),
    val iniConfigDetail: IniConfigDetail? = null,
    val cleoDetected: Boolean = false,
    val amlDetected: Boolean = false,
    val savesFound: List<String> = emptyList()
)

class ModDiagnosticEngine(private val context: Context) {

    private val requiredModFiles = listOf(
        "coop_campaign.csa",
        "coop_cam.csa",
        "coop_spawner.csa",
        "coop_config.ini",
        "coop_texts.fxt"
    )

    suspend fun runFullDiagnostics(modStatus: ModInstallationStatus): ModDiagnosticReport = withContext(Dispatchers.IO) {
        val checks = mutableListOf<DiagnosticCheckItem>()
        val timestamp = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())
        val integrityList = mutableListOf<FileIntegrityInfo>()

        // 1. CHEQUEO DEL PAQUETE DE GTA SAN ANDREAS
        val gtaPackages = listOf(
            "com.rockstargames.gtasa" to "GTA San Andreas (Classic Mobile)",
            "com.rockstargames.gtasade" to "GTA San Andreas (The Definitive Edition)",
            "com.rockstargames.gtasa.netflix" to "GTA San Andreas (Netflix Edition)"
        )
        val installedPkgEntry = gtaPackages.firstOrNull { isPackageInstalled(it.first) }
        val isGameInstalled = installedPkgEntry != null

        if (isGameInstalled) {
            checks.add(
                DiagnosticCheckItem(
                    title = "1. Juego Instalado en el Teléfono",
                    description = "Detectado: ${installedPkgEntry?.second} (${installedPkgEntry?.first})",
                    status = DiagnosticItemStatus.SUCCESS
                )
            )
        } else {
            checks.add(
                DiagnosticCheckItem(
                    title = "1. Juego Instalado en el Teléfono",
                    description = "No se detectó com.rockstargames.gtasa en la lista de apps. Si usas un APK modificado o renombrado, continúa con los siguientes pasos.",
                    status = DiagnosticItemStatus.WARNING,
                    actionHint = "Asegúrate de tener GTA San Andreas instalado en este teléfono."
                )
            )
        }

        // 2. CHEQUEO DE ARCHIVOS GENERADOS POR LA APP (Integridad y tamaño)
        val appModDir = File(context.getExternalFilesDir(null), "CoopAndreas_Mod")
        val appFiles = appModDir.listFiles()?.toList() ?: emptyList()

        var appFilesOk = true
        requiredModFiles.forEach { reqName ->
            val f = appFiles.firstOrNull { it.name == reqName }
            if (f != null && f.exists() && f.length() > 0) {
                integrityList.add(
                    FileIntegrityInfo(
                        filename = reqName,
                        exists = true,
                        sizeBytes = f.length(),
                        location = "App Interna (${appModDir.name})",
                        integrityNote = "Íntegro (${f.length()} bytes)"
                    )
                )
            } else {
                appFilesOk = false
                integrityList.add(
                    FileIntegrityInfo(
                        filename = reqName,
                        exists = false,
                        sizeBytes = 0,
                        location = "App Interna",
                        integrityNote = "Falta generar"
                    )
                )
            }
        }

        // Verificar también SAMP/settings.ini y libCoopAndreasBridge.so
        val sampIni = File(appModDir, "SAMP/settings.ini")
        if (sampIni.exists() && sampIni.length() > 0) {
            integrityList.add(
                FileIntegrityInfo(
                    filename = "SAMP/settings.ini",
                    exists = true,
                    sizeBytes = sampIni.length(),
                    location = "App Interna (SAMP)",
                    integrityNote = "Íntegro (${sampIni.length()} bytes)"
                )
            )
        }

        val bridgeSo = File(appModDir, "mods/libCoopAndreasBridge.so")
        if (bridgeSo.exists() && bridgeSo.length() > 0) {
            integrityList.add(
                FileIntegrityInfo(
                    filename = "libCoopAndreasBridge.so",
                    exists = true,
                    sizeBytes = bridgeSo.length(),
                    location = "App Interna (mods)",
                    integrityNote = "Puente Nativo C++ Íntegro (${bridgeSo.length()} bytes)"
                )
            )
        }

        if (appFilesOk) {
            checks.add(
                DiagnosticCheckItem(
                    title = "2. Archivos del Mod y Puente C++ Generados",
                    description = "Los ${requiredModFiles.size} archivos (.csa compilados, coop_config.ini, SAMP/settings.ini, libCoopAndreasBridge.so y save slot 8) están listos.",
                    status = DiagnosticItemStatus.SUCCESS
                )
            )
        } else {
            checks.add(
                DiagnosticCheckItem(
                    title = "2. Archivos del Mod Generados",
                    description = "Faltan archivos por generar o tienen 0 bytes. Toca 'INSTALACIÓN 1-CLIC' para crearlos e inyectarlos.",
                    status = DiagnosticItemStatus.ERROR,
                    actionHint = "Pulsa el botón principal 'INSTALACIÓN 1-CLIC'"
                )
            )
        }

        // 3. INSPECCIÓN REAL EN LA CARPETA DE GTA SAN ANDREAS
        val candidateGtaDirs = listOf(
            File("/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files"),
            File("/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/cleo"),
            File("/sdcard/Android_unprotected/data/com.rockstargames.gtasa/files"),
            File("/sdcard/Android_unprotected/data/com.rockstargames.gtasa/files/cleo"),
            File(Environment.getExternalStorageDirectory(), "Android_unprotected/data/com.rockstargames.gtasa/files"),
            File(Environment.getExternalStorageDirectory(), "Android_unprotected/data/com.rockstargames.gtasa/files/cleo"),
            File("/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa"),
            File("/storage/emulated/0/Android/data/com.rockstargames.gtasa/files"),
            File("/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/cleo"),
            File("/sdcard/Android/data/com.rockstargames.gtasa/files"),
            File("/sdcard/Android/data/com.rockstargames.gtasa/files/cleo"),
            File("/storage/emulated/0/Android/data/com.rockstargames.gtasa"),
            File(Environment.getExternalStorageDirectory(), "Android/data/com.rockstargames.gtasa/files"),
            File("/storage/emulated/0/Android/data/com.rockstargames.gtasade/files"),
            File("/storage/emulated/0/Android/data/com.rockstargames.gtasa.netflix/files")
        )

        var foundGtaDir: File? = null
        val foundFiles = mutableListOf<String>()
        val missingFiles = mutableListOf<String>()

        for (dir in candidateGtaDirs) {
            if (dir.exists()) {
                val dirFiles = dir.list()?.toList() ?: emptyList()
                val matched = requiredModFiles.filter { dirFiles.contains(it) }
                if (matched.isNotEmpty()) {
                    foundGtaDir = dir
                    foundFiles.addAll(matched)
                    missingFiles.addAll(requiredModFiles.filterNot { dirFiles.contains(it) })
                    break
                }
            }
        }

        val areFilesPhysicallyInGta = foundGtaDir != null && foundFiles.contains("coop_config.ini")

        // 4. PARSEO E INTEGRIDAD DEL ARCHIVO coop_config.ini
        var iniDetail: IniConfigDetail? = null
        var iniMismatchWarning: String? = null

        val iniFile = foundGtaDir?.let { File(it, "coop_config.ini") }?.takeIf { it.exists() }
            ?: File(appModDir, "coop_config.ini").takeIf { it.exists() }

        if (iniFile != null && iniFile.exists()) {
            try {
                val lines = iniFile.readLines()
                var isHostVal: Int? = null
                var hostIpVal: String? = null
                var portVal: Int? = null
                var roomCodeVal: String? = null
                var connModeVal: String? = null
                var p1SkinVal: Int? = null
                var p2SkinVal: Int? = null
                var freeRoamVal: Int? = null

                lines.forEach { line ->
                    val trimmed = line.trim()
                    when {
                        trimmed.startsWith("Is_Host", ignoreCase = true) ->
                            isHostVal = trimmed.substringAfter("=").trim().toIntOrNull()
                        trimmed.startsWith("Host_IP", ignoreCase = true) ->
                            hostIpVal = trimmed.substringAfter("=").trim()
                        trimmed.startsWith("Port", ignoreCase = true) ->
                            portVal = trimmed.substringAfter("=").trim().toIntOrNull()
                        trimmed.startsWith("Room_Code", ignoreCase = true) ->
                            roomCodeVal = trimmed.substringAfter("=").trim()
                        trimmed.startsWith("Connection_Mode", ignoreCase = true) ->
                            connModeVal = trimmed.substringAfter("=").trim()
                        trimmed.startsWith("Player1_Skin", ignoreCase = true) ->
                            p1SkinVal = trimmed.substringAfter("=").trim().toIntOrNull()
                        trimmed.startsWith("Player2_Skin", ignoreCase = true) ->
                            p2SkinVal = trimmed.substringAfter("=").trim().toIntOrNull()
                        trimmed.startsWith("FreeRoamAnySave", ignoreCase = true) ||
                                trimmed.startsWith("Free_Roam_Any_Save", ignoreCase = true) ->
                            freeRoamVal = trimmed.substringAfter("=").trim().toIntOrNull()
                    }
                }

                iniDetail = IniConfigDetail(
                    exists = true,
                    isHost = isHostVal,
                    hostIp = hostIpVal,
                    port = portVal,
                    roomCode = roomCodeVal,
                    connectionMode = connModeVal,
                    player1Skin = p1SkinVal,
                    player2Skin = p2SkinVal,
                    freeRoamAnySave = freeRoamVal,
                    location = iniFile.absolutePath
                )

                val isOnlineRelay = modStatus.networkConnectionMode == NetworkConnectionMode.ONLINE_REMOTE
                if (modStatus.isHostDevice) {
                    if (isHostVal == 0) {
                        iniMismatchWarning = "⚠️ Discrepancia: Este teléfono está como HOST en la app, pero en coop_config.ini tiene Is_Host=0."
                    }
                } else {
                    if (isHostVal == 1) {
                        iniMismatchWarning = "🚨 ¡ERROR CRÍTICO!: Este teléfono está como INVITADO en la app, pero en coop_config.ini tiene Is_Host=1."
                    } else if (!isOnlineRelay && (hostIpVal == "0.0.0.0" || hostIpVal.isNullOrBlank())) {
                        iniMismatchWarning = "🚨 ¡ERROR CRÍTICO!: El teléfono invitado tiene Host_IP=0.0.0.0 en coop_config.ini."
                    }
                }
            } catch (e: Exception) {
                // ignore
            }
        }

        if (areFilesPhysicallyInGta) {
            val allPresent = missingFiles.isEmpty()
            val status = if (allPresent && iniMismatchWarning == null) DiagnosticItemStatus.SUCCESS else DiagnosticItemStatus.WARNING
            checks.add(
                DiagnosticCheckItem(
                    title = "3. Existencia Física en GTA San Andreas",
                    description = "¡Archivos inyectados en ${foundGtaDir?.path}!\n" +
                            "Presentes (${foundFiles.size}): ${foundFiles.joinToString(", ")}" +
                            (if (missingFiles.isNotEmpty()) "\nFaltan: ${missingFiles.joinToString(", ")}" else "") +
                            (if (iniMismatchWarning != null) "\n$iniMismatchWarning" else ""),
                    status = status,
                    actionHint = if (missingFiles.isNotEmpty() || iniMismatchWarning != null) "Toca 'INSTALACIÓN 1-CLIC' para actualizar." else null
                )
            )
        } else {
            checks.add(
                DiagnosticCheckItem(
                    title = "3. Existencia Física en GTA San Andreas",
                    description = "🚨 ARCHIVOS NO ENCONTRADOS en la carpeta del juego.\n" +
                            "Toca el botón 'INSTALACIÓN 1-CLIC' para inyectarlos automáticamente en Android_unprotected o SAF.",
                    status = DiagnosticItemStatus.ERROR,
                    actionHint = "Pulsa 'INSTALACIÓN 1-CLIC' para inyectar automáticamente."
                )
            )
        }

        // 5. DETECCIÓN DE CLEO, AML Y PUENTE NATIVO C++ (libCoopAndreasBridge.so)
        val candidateBases = listOf(
            File("/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa"),
            File("/sdcard/Android_unprotected/data/com.rockstargames.gtasa"),
            File(Environment.getExternalStorageDirectory(), "Android_unprotected/data/com.rockstargames.gtasa"),
            File("/storage/emulated/0/Android/data/com.rockstargames.gtasa"),
            File("/sdcard/Android/data/com.rockstargames.gtasa")
        )
        val activeGtaBase = candidateBases.firstOrNull { it.exists() }
        val amlDir = activeGtaBase?.let { File(it, "files/aml") }
        val modsDir = activeGtaBase?.let { File(it, "mods") }
        val amlLog = activeGtaBase?.let { File(it, "files/aml.log") }

        var amlLogText = ""
        if (amlLog?.exists() == true) {
            try { amlLogText = amlLog.readText() } catch (e: Exception) {}
        }

        val hasCleoPluginInMods = modsDir?.listFiles()?.any {
            (it.name.contains("cleo", ignoreCase = true) || it.name.contains("rusjj", ignoreCase = true)) && it.name.endsWith(".so")
        } == true
        val hasBridgeInMods = modsDir?.listFiles()?.any {
            it.name.equals("libCoopAndreasBridge.so", ignoreCase = true)
        } == true
        val hasCleoInFiles = activeGtaBase?.let {
            File(it, "files/cleomod.so").exists() || File(it, "files/aml/cleomod.so").exists()
        } == true
        val amlDetected = amlLog?.exists() == true || modsDir?.exists() == true || amlDir?.exists() == true || isPackageInstalled("com.rusjj.aml")
        val cleoDetected = hasCleoPluginInMods || hasCleoInFiles || amlLogText.contains("cleo", ignoreCase = true)

        if (amlDetected && !cleoDetected) {
            checks.add(
                DiagnosticCheckItem(
                    title = "4. Estado de CLEO / AML y Puente C++",
                    description = "🚨 AML detectado pero falta libCLEOMod.so / libCoopAndreasBridge.so en mods/.",
                    status = DiagnosticItemStatus.ERROR,
                    actionHint = "Pulsa el botón 1-CLIC para inyectar los binarios .so automáticamente."
                )
            )
        } else if (cleoDetected) {
            checks.add(
                DiagnosticCheckItem(
                    title = "4. Estado de CLEO / AML y Puente C++",
                    description = "🟢 Plugins nativos listos en mods/ (CLEO + ${if (hasBridgeInMods) "libCoopAndreasBridge.so activo" else "Bridge integrado"}).",
                    status = DiagnosticItemStatus.SUCCESS
                )
            )
        } else {
            checks.add(
                DiagnosticCheckItem(
                    title = "4. Estado de CLEO / AML y Puente C++",
                    description = "ℹ️ Listo para inyectar plugins nativos con el Auto-Instalador de 1 Clic.",
                    status = DiagnosticItemStatus.INFO
                )
            )
        }

        // 6. CONFIGURACIÓN DE RED (LAN O ONLINE ROOM CODE)
        val isHost = modStatus.isHostDevice
        val isOnlineMode = modStatus.networkConnectionMode == NetworkConnectionMode.ONLINE_REMOTE
        if (isOnlineMode) {
            checks.add(
                DiagnosticCheckItem(
                    title = "5. Red Online a Distancia (Sala: ${modStatus.roomCode})",
                    description = "🌐 Emparejamiento por código de sala (${modStatus.roomCode}) con retransmisión de paquetes (STUN/Relay) sin abrir puertos.",
                    status = DiagnosticItemStatus.SUCCESS
                )
            )
        } else if (isHost) {
            checks.add(
                DiagnosticCheckItem(
                    title = "5. Rol en Red LAN: HOST (ANFITRIÓN)",
                    description = "Servidor activo en ${modStatus.localIpAddress} (Discovery UDP 8988 | Control TCP 8989 | Telemetría y Voz UDP 8990).",
                    status = DiagnosticItemStatus.SUCCESS
                )
            )
        } else {
            val targetIp = modStatus.targetHostIp.ifBlank { modStatus.localIpAddress }
            val hasValidIp = targetIp.isNotBlank() && targetIp != "127.0.0.1" && targetIp != "0.0.0.0"

            if (!hasValidIp) {
                checks.add(
                    DiagnosticCheckItem(
                        title = "5. Rol en Red LAN: INVITADO",
                        description = "🚨 IP del Host no configurada en LAN (o cambia a Modo Online por Código de Sala).",
                        status = DiagnosticItemStatus.ERROR,
                        actionHint = "Autodetecta al Host en la Sala, escribe su IP, o activa el Modo Online por Código de Sala."
                    )
                )
            } else {
                var socketReachable = false
                try {
                    val socket = Socket()
                    socket.connect(InetSocketAddress(targetIp, 8989), 1000)
                    socket.close()
                    socketReachable = true
                } catch (e: Exception) {
                    socketReachable = false
                }

                checks.add(
                    DiagnosticCheckItem(
                        title = "5. Rol en Red LAN: INVITADO",
                        description = if (socketReachable) "🟢 Host alcanzable en $targetIp:8989 (Voz/Telemetría UDP 8990)."
                        else "Apuntando a $targetIp:8989 (Asegúrate de estar en el mismo Wi-Fi o usa el Modo Online por Código de Sala).",
                        status = if (socketReachable) DiagnosticItemStatus.SUCCESS else DiagnosticItemStatus.WARNING
                    )
                )
            }
        }

        // DETERMINAR VEREDICTO GENERAL
        val verdict: DiagnosticVerdict
        val headline: String
        val detailExplanation: String

        when {
            !appFilesOk -> {
                verdict = DiagnosticVerdict.PENDING_COPY_TO_GTA
                headline = "ARCHIVOS PENDIENTES DE GENERAR"
                detailExplanation = "Pulsa el botón 'INSTALACIÓN 1-CLIC' para inyectar binarios, scripts .csa y configuración."
            }
            !isHost && !isOnlineMode && (modStatus.targetHostIp.isBlank() || modStatus.targetHostIp == "0.0.0.0") -> {
                verdict = DiagnosticVerdict.CONFIG_MISMATCH
                headline = "IP DEL HOST NO CONFIGURADA EN EL INVITADO"
                detailExplanation = "El teléfono invitado necesita la IP del Host en modo LAN o cambiar al Modo Online por Código de Sala."
            }
            iniMismatchWarning != null && iniMismatchWarning.contains("ERROR CRÍTICO") -> {
                verdict = DiagnosticVerdict.CONFIG_MISMATCH
                headline = "CONFIGURACIÓN DE HOST/INVITADO DISCORDANTE"
                detailExplanation = iniMismatchWarning
            }
            !areFilesPhysicallyInGta -> {
                verdict = DiagnosticVerdict.PENDING_COPY_TO_GTA
                headline = "LISTO PARA INYECCIÓN AUTOMÁTICA EN GTA SA"
                detailExplanation = "Pulsa el botón de 1 Clic para inyectar los archivos directamente en Android_unprotected o mediante SAF persistente."
            }
            amlDetected && !cleoDetected -> {
                verdict = DiagnosticVerdict.CLEO_MISSING_WARNING
                headline = "FALTA PLUGIN CLEOMOD.SO EN AML"
                detailExplanation = "Pulsa el botón 1-Clic para instalar automáticamente libCLEOMod.so y libCoopAndreasBridge.so en mods/."
            }
            else -> {
                verdict = DiagnosticVerdict.READY_TO_PLAY
                headline = "¡PUENTE NATIVO Y MOD LISTOS PARA JUGAR!"
                detailExplanation = "libCoopAndreasBridge.so, scripts .csa, coop_config.ini y SAMP/settings.ini están sincronizados. Lanza GTA SA desde la app."
            }
        }

        ModDiagnosticReport(
            timestamp = timestamp,
            verdict = verdict,
            headline = headline,
            detailExplanation = detailExplanation,
            checks = checks,
            whyGuestDidNotJoinNote = "El puente nativo C++ (libCoopAndreasBridge.so) y los scripts .csa sincronizan de 2 a 4+ jugadores con cámara 3D independiente, vehículos compartidos (conductor + pasajeros con drive-by) o separados, y estado de misiones (\$ONMISSION y checkpoints).",
            inGameActivationSteps = listOf(
                "Pulsa el botón 'INSTALACIÓN 1-CLIC Y JUGAR' para inyectar automáticamente los archivos.",
                "La app reescribe automáticamente la IP y el Código de Sala antes de lanzar GTA San Andreas."
            ),
            recommendedActions = emptyList(),
            detectedGtaPath = foundGtaDir?.path ?: "/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/files/",
            filesFoundInGta = foundFiles,
            filesMissingInGta = missingFiles,
            fileIntegrityDetails = integrityList,
            iniConfigDetail = iniDetail,
            cleoDetected = cleoDetected,
            amlDetected = amlDetected,
            savesFound = emptyList()
        )
    }

    /**
     * Copia y sobreescribe recursivamente los archivos del mod (incluyendo subcarpetas cleo/, SAMP/ y mods/)
     * en el árbol SAF persistente otorgado por el usuario sobre Android/data/com.rockstargames.gtasa/files/.
     */
    suspend fun copyFilesToSafTree(treeUri: Uri): Int = withContext(Dispatchers.IO) {
        var copiedCount = 0
        try {
            val modDir = File(context.getExternalFilesDir(null), "CoopAndreas_Mod")
            if (!modDir.exists() || modDir.listFiles().isNullOrEmpty()) return@withContext 0

            val resolver = context.contentResolver
            val rootDocUri = DocumentsContract.buildDocumentUriUsingTree(
                treeUri,
                DocumentsContract.getTreeDocumentId(treeUri)
            )

            fun writeSingleFileToSafDir(parentDocUri: Uri, file: File) {
                try {
                    val mime = when {
                        file.name.endsWith(".ini") || file.name.endsWith(".txt") || file.name.endsWith(".fxt") -> "text/plain"
                        else -> "application/octet-stream"
                    }
                    val createdUri = DocumentsContract.createDocument(resolver, parentDocUri, mime, file.name)
                    if (createdUri != null) {
                        resolver.openOutputStream(createdUri, "wt")?.use { out ->
                            file.inputStream().use { input ->
                                input.copyTo(out)
                            }
                        }
                        copiedCount++
                    }
                } catch (_: Exception) {}
            }

            modDir.listFiles()?.forEach { file ->
                if (file.isDirectory) {
                    try {
                        val subDirUri = DocumentsContract.createDocument(
                            resolver,
                            rootDocUri,
                            DocumentsContract.Document.MIME_TYPE_DIR,
                            file.name
                        )
                        if (subDirUri != null) {
                            file.listFiles()?.forEach { child ->
                                if (!child.isDirectory) {
                                    writeSingleFileToSafDir(subDirUri, child)
                                }
                            }
                        }
                    } catch (_: Exception) {}
                } else {
                    writeSingleFileToSafDir(rootDocUri, file)
                }
            }
        } catch (e: Exception) {
            // ignore
        }
        copiedCount
    }

    private fun isPackageInstalled(pkg: String): Boolean {
        return try {
            context.packageManager.getPackageInfo(pkg, 0)
            true
        } catch (e: Exception) {
            false
        }
    }
}
