# ⚠️ Estado del Proyecto: Experimental / Pre-Alfa (WIP)

**CoopAndreas Android (CoopAdrOidEas)** es una **prueba de concepto técnica (PoC)** en fase de desarrollo activo.

> ### 🛑 AVISO IMPORTANTE:
> **Este software NO es un producto final, NO está completo y NO es estable.**  
> Si estás buscando una experiencia pulida, libre de errores y lista para jugar toda la campaña de punta a punta sin interrupciones, este proyecto **aún no se encuentra en ese estado**.

---

## 📌 ¿Por qué es experimental?

Llevar la arquitectura de sincronización de GTA San Andreas desde PC al ecosistema móvil de Android involucra interceptar llamadas de bajo nivel en ARM/ARM64, interactuar con el motor del juego cerrado (`libGTASA.so`) mediante ganchos en C++ (NDK) y scripts CLEO, y transmitir paquetes UDP en redes móviles con latencia variable.

Esto significa que:
1. **Desincronizaciones Frecuentes**: En misiones con cinemáticas complejas, persecuciones a alta velocidad o eventos de tiempo estricto, es probable que los jugadores experimenten desfasajes (*desyncs*).
2. **Cierres Inesperados (*Crashes*)**: Ciertas combinaciones de dispositivos, versiones de Android o controladores de GPU (Mali / Adreno) pueden provocar cierres repentinos del juego al interactuar con la memoria nativa.
3. **Mecánicas Incompletas**:
   - Misiones que involucran minijuegos (baile, suspensión de lowriders, misiones de vehículos a control remoto RC) aún no cuentan con opcodes de sincronización terminados.
   - La sincronización en redes Online públicas es sensible a la fluctuación de ping y pérdida de paquetes (funciona con mucha mayor estabilidad en redes locales Wi-Fi LAN).
4. **Riesgo en Partidas Guardadas**: Siempre utilizá partidas guardadas de respaldo. Un crash durante la ejecución de un opcode de misión podría dejar corrupto el archivo de guardado en el slot actual.

---

## ✅ Lo que SÍ funciona actualmente (Fase Pre-Alfa):
- Autodescubrimiento en red local Wi-Fi (UDP broadcast).
- Conexión Host/Invitado por código de sala o IP directa.
- Sincronización básica de posición, rotación, vida, blindaje y arma activa a 30 Hz.
- Cámaras 3D independientes (ambos jugadores se mueven libremente por el mapa).
- Sincronización de vehículos en modo libre y disparo como acompañante (*Drive-By*).
- Chat de voz integrado Push-to-Talk (Walkie-Talkie).
- Selector visual de skins en el lobby.
- Herramienta de diagnóstico e instalación de archivos CLEO en el companion.

---

## 🎯 Objetivo de la Publicación Open-Source
El proyecto se publica en GitHub no como un mod terminado para el público masivo, sino para:
- Invitar a desarrolladores C++ / Android y modders de CLEO a colaborar.
- Diagnosticar fallas específicas en diferentes arquitecturas móviles.
- Avanzar de forma comunitaria en la ingeniería inversa de los opcodes pendientes.

Si encontrás un error, por favor reportalo con el registro de `adb logcat -s CoopAndreasBridge:V` siguiendo las instrucciones de [CONTRIBUTING.md](CONTRIBUTING.md).
