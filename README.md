<h1 align="center">Rane SL3 macOS ARM Driver (Apple Silicon M1 / M2 / M3 / M4)</h1>

<p align="center">
  <strong>Native User-Space CoreAudio Driver + Serato DJ Pro 2.5.5 Bridge (<code>Sl3Api.framework</code>) for Rane SL3 on Modern Macs</strong><br>
  <em>Original idea & project by <strong>pepelee (PPLEE)</strong> — <a href="https://www.instagram.com/pepeleedj/">@pepeleedj</a> &nbsp;|&nbsp; Ultra-low-latency audio engine & CoreAudio architecture with <strong>Ben XO</strong> (<a href="https://github.com/ben-xo">@ben-xo</a>)</em>
</p>

<p align="center">
  <a href="https://github.com/pepeleedj/Rane-SL3-Macos-ARM-Driver/raw/main/Rane-SL3-Installer-Mac-M1-M2-M3-M4.zip"><strong>⬇️ DOWNLOAD INSTALLER (.ZIP)</strong></a>
  &nbsp;|&nbsp;
  <a href="https://github.com/pepeleedj/Rane-SL3-Macos-ARM-Driver/raw/main/Installer/RaneSL3-macOS-Universal.pkg"><strong>📦 DOWNLOAD .PKG DIRECTLY</strong></a>
  &nbsp;|&nbsp;
  <a href="#-español-instrucciones-en-español"><strong>🇪🇸 LEER EN ESPAÑOL</strong></a>
</p>

---

## 🇬🇧 English

### Why does this exist?
When Apple transitioned to **Apple Silicon (M1 / M2 / M3 / M4)** and modern macOS versions, Rane and Serato discontinued support for the legendary **Rane SL3** DVS audio interface. Because the official Rane driver relied on a legacy Intel Kernel Extension (`.kext`), connecting an SL3 to a modern Mac simply did nothing—or required dangerous workarounds like disabling macOS System Integrity Protection (SIP).

**This project brings the Rane SL3 back to life on all Apple Silicon Macs (M1, M2, M3, M4 — Base, Pro, Max, Ultra) and modern macOS (Monterey, Ventura, Sonoma, Sequoia, and newer):**

* ✅ **Works natively with Serato DJ Pro 2.5.5** (the last Serato version that supports the Rane SL3 hardware—unlocks DVS `ABS` / `REL` / `INT` modes and `THRU` control).
* ✅ **NO SIP Disable Required & NO Reboot Required:** Runs 100% in user space via Apple's modern `AudioServerPlugIn` (CoreAudio HAL) and `IOUSBHost` frameworks, plus a custom Universal `Sl3Api.framework` user-space bridge.
* ✅ **Works with the macOS System Settings "SL 3" Control Panel** and includes a terminal control utility (`sl3panel`) to switch between **Phono / Line**, toggle **Thru**, and change sample rates (**44.1 kHz / 48 kHz**).
* ✅ **Works as a 6-In / 6-Out (24-bit) CoreAudio Soundcard** in **Mixxx, Traktor, Ableton Live, Rekordbox, Logic Pro**, and system-wide macOS audio.
* ✅ **Ultra-Low-Latency Audio Engine (~9–15 ms DVS Round-Trip):** Deep-scheduled isochronous USB playback with late writes directly from `WriteMix`, low-latency capture (`IN_SAFETY = 4 ms`, `OUT_SAFETY = 3 ms`), native macOS master + 6-channel volume/mute controls (`-64 dB` to `0 dB` with click-free ramping), and seamless USB hot-plug reconnect.

---

### 🚀 Quick Installation (1-Click)

1. [**Download `Rane-SL3-Installer-Mac-M1-M2-M3-M4.zip`**](https://github.com/pepeleedj/Rane-SL3-Macos-ARM-Driver/raw/main/Rane-SL3-Installer-Mac-M1-M2-M3-M4.zip) and unzip it on your Mac.
2. Open Terminal and clear macOS Gatekeeper quarantine on the downloaded folder (type `xattr -cr ` with a space at the end, drag the unzipped folder into Terminal, and press **Enter**):
   ```bash
   xattr -cr ~/Downloads/Installer
   ```
3. Double-click **`Instalar-Rane-SL3.command`** (or right-click -> **Open** on **`RaneSL3-macOS-Universal.pkg`**).
4. Enter your Mac password when prompted.
5. **Done!** `coreaudiod` reloads automatically (no reboot needed). Plug in your Rane SL3 via USB!

#### Using with Serato DJ Pro 2.5.5
1. Download and install **Serato DJ Pro 2.5.5** from Serato's official archive (the final version with Rane SL3 support).
2. Connect your **Rane SL3** via USB and open Serato DJ Pro 2.5.5 (runs seamlessly via Rosetta 2 on M1/M2/M3/M4).
3. Serato will immediately recognize the SL3 and enable both decks with full DVS timecode control!

---

### 🔗 Also Available: Rane SL 2 & Rane SL 4 macOS ARM Drivers!
Own a **Rane SL 2** or **Rane SL 4**? Download their dedicated Apple Silicon (M1 / M2 / M3 / M4) drivers here:
* 🎛️ **Rane SL 2 Driver:** [pepeleedj/Rane-SL2-Macos-ARM-Driver](https://github.com/pepeleedj/Rane-SL2-Macos-ARM-Driver)
* 🎛️ **Rane SL 4 Driver:** [pepeleedj/Rane-SL4-macOS-ARM-Driver](https://github.com/pepeleedj/Rane-SL4-macOS-ARM-Driver)

---
---

## 🇪🇸 Español (Instrucciones en Español)

### ¿Qué es este proyecto?
Con la llegada de los procesadores **Apple Silicon (M1 / M2 / M3 / M4)** y los nuevos sistemas macOS, las míticas tarjetas **Rane SL3** quedaron abandonadas porque el driver original de Rane dependía de una extensión de kernel antigua (`.kext`) para procesadores Intel.

**Este driver devuelve a la vida tu Rane SL3 en cualquier Mac moderno (M1, M2, M3, M4 y variantes Pro, Max, Ultra):**

* ✅ **Funciona nativamente con Serato DJ Pro 2.5.5** (la última versión de Serato compatible con Rane SL3; activa los modos DVS `ABS` / `REL` / `INT` y `THRU`).
* ✅ **SIN desactivar la seguridad del Mac (SIP) y SIN reiniciar:** Todo funciona en espacio de usuario mediante un plugin HAL de CoreAudio (`SL3Device.driver` sobre `IOUSBHost`) junto con un puente nativo Universal (`Sl3Api.framework`).
* ✅ **Funciona con el panel "SL 3" en Ajustes del Sistema de macOS** e incluye utilidad de control (`Panel-Control-SL3.command` / `sl3panel`) para cambiar entre **Phono / Line**, activar/desactivar **Thru** y elegir entre **44.1 kHz y 48 kHz**.
* ✅ **Funciona como tarjeta de sonido de 6 Entradas y 6 Salidas (24-bit)** en **Ableton Live, Traktor, Mixxx, Rekordbox, Logic Pro** y el audio general de macOS.
* ✅ **Motor de Audio de Ultra-Baja Latencia (~9–15 ms DVS Round-Trip):** Escritura tardía (`late-write`) directa sobre transferencias USB isócronas desde `WriteMix`, captura de baja latencia, control nativo de volumen/mute en macOS (Master + 6 canales con rampa sin clics) y reconexión USB en caliente (`hot-plug`).

---

### 🚀 Instalación Paso a Paso

Tienes dos formas de instalarlo (ambas instalan exactamente lo mismo y **NO requieren reiniciar el Mac**):

1. [**Descarga `Rane-SL3-Installer-Mac-M1-M2-M3-M4.zip`**](https://github.com/pepeleedj/Rane-SL3-Macos-ARM-Driver/raw/main/Rane-SL3-Installer-Mac-M1-M2-M3-M4.zip) y descomprímelo.
2. Si macOS bloquea los archivos descargados de Internet, abre la app **Terminal**, escribe `xattr -cr ` (con un espacio al final), arrastra la carpeta descomprimida dentro de la Terminal y pulsa **ENTER**:
   ```bash
   xattr -cr ~/Downloads/Installer
   ```
3. **Opción A (Automática en 1 clic):** Haz doble clic en **`Instalar-Rane-SL3.command`** (o clic derecho -> Abrir), introduce la contraseña de tu Mac y pulsa ENTER.
4. **Opción B (Asistente gráfico `.pkg`):** Haz clic derecho sobre **`RaneSL3-macOS-Universal.pkg`** -> **Abrir** (si macOS avisa que no está firmado por Apple, ve a *Ajustes del Sistema -> Privacidad y seguridad -> Abrir igualmente*).

#### Uso con Serato DJ Pro 2.5.5
* Instala **Serato DJ Pro 2.5.5** (última versión con soporte para Rane SL3).
* Conecta tu Rane SL3 por USB y abre Serato DJ Pro 2.5.5: detectará la tarjeta al instante y activará los dos platos con control por vinilo/CDJ (DVS).

#### Canales de Audio (6 Entradas / 6 Salidas a 24-bit, 44.1 / 48 kHz)
* **Canales 1–2:** Left Deck (Plato / CDJ Izquierdo)
* **Canales 3–4:** Right Deck (Plato / CDJ Derecho)
* **Canales 5–6:** Aux (Entrada / Salida Auxiliar)

---

### 🔗 También disponibles: Drivers para Rane SL 2 y Rane SL 4 (M1 / M2 / M3 / M4)
* 🎛️ **Rane SL 2:** [pepeleedj/Rane-SL2-Macos-ARM-Driver](https://github.com/pepeleedj/Rane-SL2-Macos-ARM-Driver)
* 🎛️ **Rane SL 3:** [pepeleedj/Rane-SL3-Macos-ARM-Driver](https://github.com/pepeleedj/Rane-SL3-Macos-ARM-Driver)
* 🎛️ **Rane SL 4:** [pepeleedj/Rane-SL4-macOS-ARM-Driver](https://github.com/pepeleedj/Rane-SL4-macOS-ARM-Driver)

---

### 🛠️ Compilar desde el código fuente / Build from Source
Requires Xcode Command Line Tools (`xcode-select --install`):
```bash
make device-plugin
```
* `plugin/SL3Device.m`: CoreAudio HAL AudioServerPlugIn (`SL3Device.driver`) with shared IPC control/status state and EZ-USB FX2 firmware loader.
* `tools/Sl3Api.m`: Replacement `Sl3Api.framework` (`C Sl3Api` C++ ABI bridge) for Serato DJ Pro 2.5.5 & System Settings preference pane.
* `tools/sl3panel.m`: CLI & interactive control panel for Phono/Line, Thru, and sample rate switching.

---

### 🙌 Créditos y Licencia / Credits & License
* **Idea original, puente Serato (`Sl3Api.framework`), utilidad `sl3panel` e instalador macOS Universal:** **pepelee (PPLEE)** — Instagram: [@pepeleedj](https://www.instagram.com/pepeleedj/)
* **Arquitectura de baja latencia (`WriteMix` late-write, controles de volumen/mute CoreAudio, hot-plug):** **Ben XO** — GitHub: [@ben-xo](https://github.com/ben-xo)
* **Base User-Space CoreAudio HAL Plugin (`OpenSL3`):** Basado en el proyecto open-source [OpenSL3](https://github.com/mrbrainz/OpenSL3) bajo licencia [GPL-3.0-or-later](LICENSE).
* *Disclaimer:* Proyecto independiente no afiliado con Rane ni Serato. "Rane", "SL3" y "Serato" son marcas registradas de sus respectivos propietarios.
