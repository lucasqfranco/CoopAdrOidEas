# Guía de Contribución a CoopAndreas (CoopAdrOidEas) 🤝

¡Gracias por tu interés en contribuir a **CoopAndreas Android**! Este proyecto es de código abierto y busca llevar la mejor experiencia cooperativa de GTA San Andreas a la comunidad móvil. Todas las contribuciones (código, pruebas en dispositivos, documentación, reporte de bugs o sincronización de misiones específicas) son bienvenidas.

---

## 🛠️ ¿En Qué Podés Colaborar?

1. **Sincronización de Misiones de Campaña (CLEO / C++)**:
   - Ajuste de opcodes (`0D00`-`0D04`) para misiones con cinemáticas o mecánicas especiales (por ejemplo, misiones de vehículos de control remoto, persecuciones complejas, misiones de francotirador).
   - Reporte y corrección de desincronización de `$ONMISSION` o checkpoints en misiones específicas de Los Santos, San Fierro o Las Venturas.
2. **Motor Nativo en C++ (Android NDK)**:
   - Optimización de estructuras de telemetría en `app/src/main/cpp/coop_bridge.cpp`.
   - Reducción del uso de CPU y latencia en la sincronización de matrices de rotación y velocidades vectoriales.
3. **Aplicación Companion (Kotlin + Jetpack Compose)**:
   - Mejoras de UI/UX en las pantallas de Lobby, Chat de voz, Selección de Skins y Diagnóstico.
   - Optimización del motor de red UDP (`CoopNetworkManager.kt`) y del chat de voz PTT (`VoiceChatEngine.kt`).
4. **Pruebas de Compatibilidad en Dispositivos Reales**:
   - Pruebas en diferentes arquitecturas (`arm64-v8a`, `armeabi-v7a`), versiones de Android (desde Android 7 hasta Android 15), y capas de personalización (MIUI/HyperOS, One UI, ColorOS, etc.).
5. **Documentación y Localización**:
   - Traducción de la interfaz y guías a otros idiomas (inglés, portugués, ruso, etc.).

---

## 🐛 Reporte de Errores e Issues

Antes de abrir un Issue, verificá si ya existe uno similar en la pestaña de **Issues**. Si no existe, creá uno nuevo incluyendo la siguiente información:

### Plantilla sugerida para Issues:
- **Dispositivo**: (Ej: Samsung Galaxy S21 / Xiaomi Poco X3 Pro)
- **Versión de Android**: (Ej: Android 12 / 13 / 14)
- **Versión de GTA San Andreas**: (Ej: v2.00 o v2.10)
- **Modo de Juego**: (Host o Guest / Campaña o Freeroam)
- **Misión Afectada**: (Si aplica, ej: `ls_05` - *Sweet & Kendl*)
- **Descripción del Problema**: Explicá qué esperabas que suceda y qué ocurrió en su lugar.
- **Registro de Logcat**:
  Para capturar los logs del motor nativo en C++, ejecutá en tu terminal con el dispositivo conectado por USB:
  ```bash
  adb logcat -s CoopAndreasBridge:V Cleo:V GTASA:V
  ```

---

## 🔀 Flujo para Enviar Pull Requests (PRs)

1. **Hacé un Fork del Repositorio**:
   Creá tu propia copia del repositorio en GitHub.
2. **Creá una Rama Descriptiva**:
   ```bash
   git checkout -b feature/mejora-sync-misiones
   # o para corrección de bugs:
   git checkout -b fix/camera-offset-issue
   ```
3. **Realizá tus Cambios**:
   - Mantené el código limpio y legible.
   - Si modificás código en C++, asegurate de respetar C++17 y compilar con el NDK sin warnings (`-Wall -O2`).
   - Si modificás Kotlin / Jetpack Compose, asegurate de que respete los estándares de formato oficial de Kotlin.
4. **Probá tus Cambios Localmente**:
   Verificá que el proyecto compile sin errores ejecutando:
   ```bash
   ./gradlew assembleDebug --stacktrace
   ```
5. **Hacé Commit con Mensajes Claros**:
   Preferimos commits estructurados (tipo Conventional Commits):
   ```bash
   git commit -m "fix(sync): corregir interpolación de velocidad en coop_bridge.cpp"
   git commit -m "feat(ui): agregar indicador de ping en tiempo real en LobbyScreen"
   ```
6. **Abrí el Pull Request**:
   - Dirigite a tu fork en GitHub y creá un Pull Request hacia la rama `main` del repositorio oficial.
   - Describí detalladamente los cambios realizados, las pruebas efectuadas y qué issue resuelve (si aplica).
   - El flujo automatizado de GitHub Actions compilará automáticamente el APK para validar que todo esté en orden.

---

## 📜 Código de Conducta

- Mantené una comunicación cordial, respetuosa y constructiva con todos los colaboradores.
- No compartas ejecutables ni archivos protegidos por derechos de autor de GTA San Andreas (APKs pirateados, archivos OBB originales o datos propietarios). Este proyecto distribuye exclusivamente código de modding y scripts de código abierto.
