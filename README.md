# CoopAndreas (CoopAdrOidEas) 🚗💥

[![Build CoopAndreas Debug APK](https://github.com/lucasqfranco/CoopAdrOidEas/actions/workflows/build-apk.yml/badge.svg)](https://github.com/lucasqfranco/CoopAdrOidEas/actions/workflows/build-apk.yml)
[![Status: Experimental / Pre-Alpha](https://img.shields.io/badge/Status-Experimental%20%2F%20Pre--Alpha-yellow.svg)](#-estado-del-proyecto-experimental--pre-alfa-wip)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform: Android](https://img.shields.io/badge/Platform-Android%207.0%2B%20(ARM64%20%2F%20ARMv7)-green.svg)](https://developer.android.com)
[![Target: GTA San Andreas Mobile](https://img.shields.io/badge/GTA%20SA-v2.00%20%2F%20v2.10-orange.svg)](https://rockstargames.com)

**Mod cooperativo multijugador nativo y aplicación companion para Grand Theft Auto: San Andreas en Android.**

CoopAdrOidEas permite revivir la experiencia de GTA San Andreas en modo multijugador cooperativo en dispositivos móviles: jugá misiones de historia de la campaña o recorré el estado en modo libre con amigos a través de **Wi-Fi LAN** o conexión **Online**, con **cámaras 3D independientes** (sin las limitaciones de pantalla dividida ni restricciones de proximidad de PS2).

---

> ⚠️ **ESTADO DEL PROYECTO: EXPERIMENTAL / PRE-ALFA (WORK IN PROGRESS)**
> 
> **Este proyecto es un desarrollo en curso, altamente experimental y concebido como una prueba de concepto técnica (PoC).**  
> **NO es una versión final, completa ni estable, ni se encuentra cerca de serlo todavía.**
> 
> - **Fase de desarrollo**: Pre-Alfa temprana / Prototipo en evolución constante (*WIP*).
> - **Inestabilidad esperada**: Es común y esperado experimentar desincronizaciones de red (*desyncs*), glitches visuales en animaciones o vehículos, congelamientos de cinemáticas o cierres inesperados (*crashes*) en ciertas misiones y configuraciones de hardware.
> - **Propósito open-source**: Se comparte abiertamente con la comunidad con el fin de **investigación, colaboración técnica y experimentación comunitaria**. Buscamos que otros desarrolladores, modders y entusiastas de la ingeniería inversa puedan auditar el código, probar la telemetría y sumar mejoras.
> - **Expectativas para usuarios**: Si buscás una experiencia de juego comercial pulida de inicio a fin sin errores, el proyecto aún no está en ese punto. Tu paciencia, pruebas y reportes en [Issues](https://github.com/lucasqfranco/CoopAdrOidEas/issues) son lo que ayuda a hacerlo avanzar.

---

## 🌟 Características Principales (Features)

- 🔄 **Sincronización de Jugadores en Tiempo Real**: Telemetría fluida a 30 Hz que replica posición ($X, Y, Z$), ángulo de rotación (*heading*), velocidades vectoriales ($V_x, V_y, V_z$), salud, blindaje, armas equipadas y disparos.
- 🎥 **Cámaras 3D 100% Independientes**: Adiós a la pantalla dividida forzada o a estar atados al mismo plano. Cada jugador cuenta con su propia cámara libre en tercera persona y autonomía total de movimiento a lo largo y ancho del mapa.
- 📜 **Compatibilidad con Misiones de Campaña y Modo Libre**: Soporte para jugar misiones originales de la historia con sincronización de estado `$ONMISSION` y *checkpoints*, o recorrer San Andreas en *Freeroam* sobre cualquier partida guardada.
- 🚗 **Sincronización Vehicular y Drive-By**: Entrada y salida de vehículos sincronizada, visualización de pasajeros y mecánicas cooperativas de disparo desde el asiento de acompañante (*Drive-By*).
- 📻 **Chat de Voz Walkie-Talkie Integrado**: Motor de audio UDP de bajísima latencia (Push-To-Talk) integrado directamente en la app companion con visualizador de audio en tiempo real.
- 🎭 **Selector de Skins Interactivo**: Interfaz visual de lobby para seleccionar personajes clásicos (Carl 'CJ' Johnson, Sweet, Ryder, Big Smoke, miembros de Grove Street y modelos personalizados).
- 🌐 **Soporte LAN y Online**: Autodescubrimiento sin configuración en redes locales Wi-Fi (UDP broadcast) y conexión por Código de Sala (*Room Code*) o IP directa para jugar mediante VPN (ZeroTier, Tailscale) o reenvío de puertos.
- 🛡️ **Motor de Diagnóstico e Instalador CLEO Automático**: Comprobador integrado de integridad que verifica rutas de juego (`com.rockstargames.gtasa`), bibliotecas nativas, permisos de almacenamiento y despliega los scripts `.csa` con un solo toque.

---

## ⚠️ Limitaciones Conocidas y Estado Actual (Known Limitations)

Como proyecto experimental en fase Pre-Alfa, existen limitaciones técnicas conocidas en las que se está trabajando activamente:

1. **Cinemáticas y Misiones con Guiones Complejos**:
   - Misiones que toman el control forzado de la cámara (*cutscenes* dentro del motor) pueden causar desincronización temporal entre el Host y el Guest.
   - Las misiones que emplean minijuegos específicos (misiones de baile, suspensión hidráulica de lowriders o misiones de vehículos a radiocontrol RC) requieren opcodes adicionales aún no finalizados.
2. **Tolerancia a Latencia en Conexiones Online**:
   - En redes locales Wi-Fi (LAN) la telemetría es altamente fluida. En partidas Online mediante internet público o VPN, fluctuaciones de ping o pérdida de paquetes UDP pueden generar *rubberbanding* o desfasajes de posición.
3. **Variabilidad de Dispositivos y Versiones de GTA**:
   - El mod está optimizado primariamente para GTA San Andreas v2.00 / v2.10. Versiones modificadas de terceros, APKs no estándar o incompatibilidades con GPUs específicas (Mali vs Adreno) pueden provocar cierres repentinos al inyectar las librerías CLEO.
4. **Guardado de Progreso**:
   - Se recomienda utilizar partidas guardadas de respaldo, ya que la ejecución de scripts cooperativos experimentales podría corromper el slot de guardado en caso de un crash durante una misión.

---

## 🎖️ Créditos y Agradecimientos Obligatorios (Acknowledgements)

Este proyecto rinde tributo y basa su lógica fundamental en el trabajo pionero de **[Tornamic/CoopAndreas](https://github.com/Tornamic/CoopAndreas)** para PC.

> ### Reconocimiento a Tornamic / CoopAndreas (PC)
> El mod cooperativo original desarrollado por **[Tornamic](https://github.com/Tornamic)** demostró por primera vez en la historia del modding de GTA San Andreas que era posible jugar la campaña completa en cooperativo con sincronización de scripts y cámaras independientes.
> 
> **CoopAdrOidEas** traslada esa misma visión pionera al ecosistema nativo de Android:
> - Adapta la arquitectura de red y la sincronización de scripts de Tornamic a dispositivos móviles ARM/ARM64.
> - Implementa los opcodes personalizados (`0D00`-`0D04`) y la estructura de telemetría binaria a través de una biblioteca nativa en C++ (`libCoopAndreasBridge.so`) compilada con el Android NDK.
> - Conecta el motor del juego móvil con un cliente companion moderno en Kotlin / Jetpack Compose para la gestión de salas, chat de voz y diagnóstico.
> 
> Repositorio oficial de Tornamic en PC: **https://github.com/Tornamic/CoopAndreas**

Agradecimientos especiales también a:
- **Alexander Blade**: Por la creación de la librería CLEO Android (`libCLEOMod.so`, `libSAUtils.so`) que hace posible la inyección de scripts `.csa` en móviles.
- La comunidad open-source de ingeniería inversa y modding de GTA San Andreas.

---

## 🏗️ Arquitectura Técnica

```
┌─────────────────────────────────────────────────────────────┐
│              CoopAndreas Companion (Android)                │
│       Kotlin + Jetpack Compose + UDP Network Manager        │
│          (Lobby, Walkie-Talkie PTT, Mod Diagnostic)         │
└──────────────────────────────┬──────────────────────────────┘
                               │ JNI / Bridge State
┌──────────────────────────────▼──────────────────────────────┐
│       libCoopAndreasBridge.so (C++17 / Android NDK)         │
│          Telemetry Engine & Custom Opcode Handler           │
└──────────────────────────────┬──────────────────────────────┘
                               │ Binary IPC (coop_telemetry.bin)
┌──────────────────────────────▼──────────────────────────────┐
│           CLEO Android Runtime & Custom .csa Scripts        │
│   (coop_andreas_main.csa, coop_mission_sync.csa, coop_cam)  │
└──────────────────────────────┬──────────────────────────────┘
                               │ Memory Hooking / Opcodes
┌──────────────────────────────▼──────────────────────────────┐
│           GTA San Andreas Android Engine (libGTASA.so)      │
└─────────────────────────────────────────────────────────────┘
```

### Opcodes Custom Implementados (Rango `0D00` - `0D04`):

| Opcode | Nemónico / Función | Descripción |
|---|---|---|
| `0D00` | `OPCODE_COOP_INIT` | Inicializa el puente de telemetría y valida handshake con el companion. |
| `0D01` | `OPCODE_COOP_SYNC_PLAYER_STATE` | Lee/escribe posición, rotación, velocidad, vida, blindaje y arma activa. |
| `0D02` | `OPCODE_COOP_INDEPENDENT_CAMERA` | Anula el centrado forzado de cámara PS2 y otorga control libre al jugador. |
| `0D03` | `OPCODE_COOP_SYNC_MISSION_CHECKPOINT` | Sincroniza flag `$ONMISSION`, ID de misión y coordenadas de checkpoint. |
| `0D04` | `OPCODE_COOP_SPAWN_PARTNER` | Spawnea el ped del compañero con el skin model seleccionado en el lobby. |

---

## 🚀 Guía de Instalación Rápida (Quick Start)

### Requisitos Previos
1. Dispositivo Android con versión 7.0 (Nougat, API 24) o superior (arquitectura `arm64-v8a` o `armeabi-v7a`).
2. Copia instalada y funcional de **GTA: San Andreas v2.00 o v2.10** en el almacenamiento del dispositivo.
3. Al menos 100 MB de espacio libre.

### Paso a Paso para Usuarios Finales:

1. **Descargar el APK**:
   - Descargá la versión experimental más reciente de `CoopAndreas-debug.apk` desde la sección de **[Releases](../../releases)** o desde los artefactos de **GitHub Actions**.
2. **Instalar y Abrir la Aplicación Companion**:
   - Instalá el APK en tu dispositivo y otorgá los permisos de almacenamiento y micrófono (necesarios para el chat de voz Walkie-Talkie).
3. **Ejecutar el Diagnóstico**:
   - En la pestaña **Instalación**, presioná **"Comprobar Integridad"**. La aplicación detectará automáticamente tu instalación de GTA San Andreas en `/storage/emulated/0/Android/data/com.rockstargames.gtasa/files`.
4. **Desplegar el Paquete CLEO**:
   - Tocá en **"Instalar / Actualizar Mod CLEO"**. La app copiará los scripts `.csa`, la biblioteca `libCoopAndreasBridge.so` y los archivos de configuración (`coop_config.ini`).
5. **Configurar la Sala**:
   - **Host (Anfitrión)**: Seleccioná "Crear Sala", elegí tu skin y compartí el código de sala o tu IP local.
   - **Invitado (Guest)**: Tocá "Unirse a Sala", ingresá la IP del Host o utilizá el botón de autodescubrimiento LAN.
6. **Iniciar el Juego**:
   - Presioná **"Lanzar GTA San Andreas"** desde el companion. ¡A probar el modo cooperativo!

---

## 💻 Guía para Desarrolladores (Build & Development)

### Requisitos del Entorno
- **Android Studio** (Koala / Ladybug o superior)
- **JDK 17** (Temurin recomendado)
- **Android SDK Platform 35** y **Build-Tools 35.0.0**
- **Android NDK** (`r26b` o compatible, versión CMake `3.22.1`)
- **Gradle 8.11.1**

### Compilación desde Línea de Comandos

Clonar el repositorio:
```bash
git clone https://github.com/lucasqfranco/CoopAdrOidEas.git
cd CoopAdrOidEas
```

Compilar el APK de depuración con NDK C++ y Compose:
```bash
# En Linux / macOS
chmod +x ./gradlew
./gradlew assembleDebug --stacktrace

# En Windows
gradlew.bat assembleDebug --stacktrace
```

El APK resultante se generará en:
```
app/build/outputs/apk/debug/app-debug.apk
```

### Integración Continua (CI/CD)
El repositorio incluye un flujo de trabajo automatizado en `.github/workflows/build-apk.yml`. Cada *push* o *pull request* a la rama principal compila automáticamente el código nativo en C++, el frontend en Jetpack Compose y publica el APK como artefacto de compilación descargable.

---

## 🤝 Cómo Contribuir

¡Las contribuciones de la comunidad son bienvenidas! Si querés colaborar mejorando la sincronización de misiones, optimizando el puente nativo en C++ o puliendo la interfaz en Jetpack Compose, leé nuestra guía en [CONTRIBUTING.md](CONTRIBUTING.md).

---

## ⚖️ Licencia y Aviso Legal (Disclaimer)

- **Código Abierto**: Este proyecto está licenciado bajo la **[Licencia Pública General GNU v3.0 (GPLv3)](LICENSE)**.
- **Aviso Legal de Terceros**: Grand Theft Auto, GTA San Andreas y todas las marcas, nombres y logos asociados son marcas comerciales registradas de **Rockstar Games** y **Take-Two Interactive Software, Inc.** Este proyecto es una modificación hecha por fans con fines educativos y de preservación comunitaria, sin fines de lucro ni afiliación comercial con Rockstar Games o Take-Two Interactive.
- **Crédito de Lógica Original**: Arquitectura de scripts y opcodes cooperativos derivados del proyecto original de **[Tornamic](https://github.com/Tornamic/CoopAndreas)**.
