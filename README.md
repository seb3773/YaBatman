# Yabatman (Yet Another Battery Manager)

![Yabatman](konqi_yabatman.png)

**Yabatman** is a comprehensive and highly configurable battery monitor and advanced power management tool for Linux. Built natively for the Trinity Desktop Environment (TQt3), Yabatman can also be compiled in **standalone static mode** to run seamlessly on any standard Linux desktop environment (GNOME, KDE Plasma, XFCE, MATE, LXQt, i3, Hyprland, etc.) without requiring any Trinity or TQt3 system packages.

It combines the exhaustive hardware insights of a premium battery monitor with the aggressive power-saving techniques of advanced daemons like TLP — all in a single, cohesive application.

Designed with both performance and footprint in mind, Yabatman consists of an ultra-lightweight C daemon (`yabatmand`, ~38KB) and a rich, fast C++/TQt3 graphical client (`yabatman`, ~350KB in dynamic mode, ~2.9MB in standalone static mode). Together, they deliver what is probably one of the most feature-complete battery monitor and power management suites available on Linux.

## Philosophy

Most Linux power management solutions are either a graphical battery monitor with limited control, or a CLI-only daemon with no visual feedback or with the need of another ui to control it. Yabatman does both.

- **No polkit, no sudo prompts**: The daemon runs as root via systemd; the GUI communicates through a simple Unix socket. No policy frameworks, no authentication popups — just instant, transparent control.
- **Zero bloat**: Both binaries are compiled with aggressive LTO, section GC, and `sstrip`. The daemon idles at virtually zero CPU. The GUI loads instantly.
- **Universal compatibility**: Can be compiled natively for Trinity (TDE) or in **standalone static mode** (`./build.sh static`) to run on *any* Linux distribution and desktop environment without installing TDE/TQt3 dependencies.
- **Everything in one place**: Battery status, hardware details, charge history, power profiles, process freezing, screen management, screensavers — all accessible from one system tray icon.
- **Fully configurable**: 10 dedicated settings panels let you tune every aspect of power behavior, from USB autosuspend exclusions to per-SSID security policies.

## Key Features

Yabatman aims to be one of the most complete battery and power management suites available on Linux, offering features that range from hardcore system optimization to optional visual candy :-)

### Comprehensive Battery Monitoring
- **Dynamic System Tray Integration**: Fully customizable tray icon with adaptive colors, charging animations, and critical level blinking. Choose between symbolic or coloured icon styles with per-level color configuration.
- **Custom Popup Dashboard**: A sleek, dark/light mode compatible floating panel for quick access to brightness control, performance profiles, and instant metrics (charge rate, discharge rate, estimated time remaining).
- **Exhaustive Battery Info Dialog**: Deep insights into your hardware — Vendor, Technology, Design/Full/Current Capacity, precise Voltage statistics (min/max/current), instantaneous charge/discharge rates in Watts, cycle count, battery health percentage, and manufacturing data.
- **Battery History Logger**: A built-in graphical history viewer with a compact binary format. Track charge/discharge curves over 24h, 48h, or 72h periods, overlaid with system suspend events and screen on/off transitions. Average charge/discharge rates are computed from real historical data.

### Advanced Power Management
Yabatman incorporates aggressive power-saving logic inspired by the fantastic work of the **TLP project** (huge thanks to the TLP developers for paving the way in Linux power management). In many areas, Yabatman goes further by offering real-time GUI control and features not available in TLP.

- **Dynamic Profiles**: Three switchable profiles — *Eco*, *Balanced*, and *Performance* — each with independent settings for AC and Battery mode.
- **CPU Governor & Frequency Control**: Set CPU governor (`powersave`, `performance`, `schedutil`), configure maximum frequency caps, and limit active CPU cores.
- **PCI & SATA Power Policies**: Control PCI device power management (`default` / `power_supersave`) and SATA link power management (`med_power_with_dipm`, `max_performance`, etc.).
- **USB Autosuspend**: Enable/disable USB autosuspend globally with automatic exclusion of HID devices (mice, keyboards) and audio interfaces to prevent input lag.
- **Wi-Fi Power Saving**: Toggle WiFi power management on/off per profile, with auto-disable based on network inactivity.
- **Webcam Power Control**: Automatically unbind/rebind USB webcam drivers to eliminate idle power draw.
- **Backlight Management**: Direct sysfs brightness control with adaptive dimming on inactivity.
- **Services Freezing**: Automatically freeze non-essential systemd services (e.g., `baloo`, `updatedb`, `tracker`) when on battery. Fully configurable whitelist/blacklist.
- **Processes Freezing**: Freeze heavy user-space processes on critical battery. Configurable whitelist/blacklist with regex-based process matching.
- **Smart Inactivity Detection**: Adaptive screen dimming → display sleep → system suspend pipeline with configurable timeouts for AC and Battery independently. Integrated MPRIS detection prevents sleep during media playback, video calls, or presentations.

### Security & Session Management
- **Session Locking**: Configurable lock-on-display-off and lock-on-sleep behavior.
- **Trusted SSIDs**: Define trusted Wi-Fi networks where session locking is automatically disabled (e.g., your home network).
- **Power & Sleep Button Actions**: Configure system response to hardware buttons (Sleep, Hibernate, Hybrid Sleep, Shutdown, Ask User, Do Nothing).
- **Lid Close Actions**: Independent lid-close behavior for AC and Battery modes.

### Battery Health & Calibration
- **Charge Limiting**: Hardware-supported charge end thresholds to prolong battery lifespan (e.g., cap charging at 80%). Automatically detects hardware capability.
- **Calibration Assistant**: A dedicated state machine with overlay UI to guide you through full charge/discharge calibration cycles for accurate capacity reporting.

### Visuals & Screensavers (Optional)
Because power management doesn't have to be boring, Yabatman includes optional visual features:
- **7 Built-in Screensavers**: Digital Clock (bouncing), Analog Clock (with sweep second hand and burn-in protection orbit), Matrix Digital Rain, 3D Pipes, Plasma Clouds, Pictures Slideshow (with Ken Burns zoom effect and crossfade transitions), and Starfield Warp.
- **Slideshow Configuration**: Choose image directory, enable random order.
- **Transition Effects**: Smooth sleep/shutdown transition animations — "Old TV turn-off", Circular Wipe, Fade Out, or Random.
- **Presentation Mode**: One-click toggle to suppress all power actions (dimming, sleep, screensaver) during presentations.
- **Powernap Mode**: Prevents system sleep/suspension when closing the laptop lid while connected to AC power. Blackouts the laptop display and applies reduced power settings (Low CPU profile, optional Wi-Fi/Bluetooth disabling) so background tasks (downloads, builds, encoding, background scripts) can continue running safely without overheating or sleeping.

### Appearance & Customization
- **10 Settings Panels**: General, Battery Profile, AC Profile, Adaptive Features, Security, Services Freezing, Processes Freezing, Transitions & Screensavers, Appearance, and Advanced.
- **Tray Icon Styles**: Symbolic (monochrome) or Coloured icons with per-level color customization (Normal, Warning, Critical).
- **Popup Transparency**: Adjustable popup opacity.


---

## Architecture & Mechanisms

Yabatman is split into two robust components:

1. **`yabatmand` (The Daemon)**: Written in pure C, compiled to a tiny footprint (~38KB). It runs as `root` and handles all privileged operations (sysfs writing for CPU governor/frequency/power policies, USB autosuspend, PCI power management, SATA link policies, backlight control, process freezing via SIGSTOP/SIGCONT, systemd service management, Udev monitoring for AC/battery transitions). It consumes virtually zero CPU cycles while idling, waking only on socket commands or udev events.
2. **`yabatman` (The GUI)**: A native C++/TQt3 application (~350KB). It connects to the daemon via a local Unix socket (`/run/yabatmand/daemon.sock`) with `chmod 0666` permissions — no polkit or sudo required. It handles all complex logic: battery polling, history logging, rendering screensavers, managing inactivity timers, MPRIS detection, and presenting the full settings UI.

> **Technical Deep Dive**: For a complete overview of the power-saving pipeline, internal state machines, timeout calculations, and the GUI ↔ Daemon socket protocol, please refer to the detailed [Energy Management Logic Specification](energy_management_logic.md).

### Class Architecture Diagram

```mermaid
classDiagram
    class YabatmanApp {
        +YabatmanApp()
        -YabatmanTrayIcon *m_trayIcon
        -InactivityManager *m_inactivityManager
        -ConfigManager *m_configManager
        -BatteryLogger *m_batteryLogger
        -CalibrationManager *m_calibrationManager
    }
    class YabatmanTrayIcon {
        +YabatmanTrayIcon()
        -TDEPopupMenu *m_menu
        -YabatmanPopup *m_customPopup
        -void showCustomPopup()
        -void updateIcon()
    }
    class InactivityManager {
        +InactivityManager()
        -TQTimer *m_idleTimer
        -TQTimer *m_mprisTimer
        -void checkIdle()
        -void detectMprisPlayback()
        -void handleLidChange()
    }
    class BatteryLogger {
        +BatteryLogger()
        -TQValueList~BatterySample~ m_history
        -TQValueList~SystemEvent~ m_events
        +void loadHistory()
        +void saveHistory()
        +void addSample()
        +void addEvent()
        +double getAverageChargeRate()
        +double getAverageDischargeRate()
    }
    class CalibrationManager {
        +CalibrationManager()
        -CalibrationState m_state
        -CalibrationOverlay *m_overlay
        +void startCalibration()
        +void cancelCalibration()
        +void handleBatteryUpdate()
    }
```

---



## Build & Installation

### Prerequisites

**Build dependencies:**
- CMake >= 3.10
- TQt3 (Trinity Qt3) development headers and `tqmoc`
- TDE development headers (`tdelibs14-trinity-dev` or equivalent)
- `pkg-config`
- Development libraries: `glib-2.0`, `gio-2.0`, `libsystemd`, `libudev`, `libnotify`
- X11 development headers: `libx11-dev`, `libxss-dev`, `libxext-dev`, `libxtst-dev`

**Runtime dependencies (Dynamic TDE mode):**
- `tdelibs14-trinity`, `libtqt3-mt`, `libtqtinterface`
- `libnotify4`, `libudev1`, `libsystemd0`
- `libx11-6`, `libxss1`, `libxext6`, `libxtst6`

**Runtime dependencies (Standalone Static mode):**
- Standard distro libraries only: `libnotify4`, `libudev1`, `libsystemd0`, `libx11-6`, `libxss1`, `libxext6`, `libxtst6` (No TDE or TQt3 packages required!)

---

### Build Modes

Yabatman can be compiled in two modes:

#### 1. Dynamic Mode (Default for TDE)
Builds natively against system TDE/TQt3 libraries. Produces an ultra-compact UI binary (~350KB).

```bash
./build.sh
```

#### 2. Standalone Static Mode (Universal Linux)
Statically embeds TQt3 (`libs/libtqt-mt.a`) into the UI binary and removes all TDE dependencies using pure TQt3 abstraction wrappers. The resulting UI binary (~2.9MB) runs on **any Linux desktop environment** (GNOME, KDE, XFCE, MATE, LXQt, i3, Hyprland, etc.) without installing Trinity or TQt3 packages.

```bash
./build.sh static
```

Yabatman uses highly aggressive compilation flags (LTO, GC sections, `-Os` for UI code, and `sstrip` if available) to ensure minimal binary sizes without sacrificing performance.

---

### Debian Packages (.deb)

You can build `.deb` packages for both modes:

#### Dynamic TDE Package (`yabatman_1.0_amd64.deb`, ~123KB)
```bash
./build_deb.sh
sudo dpkg -i yabatman_1.0_amd64.deb
```

#### Standalone Static Package (`yabatman_1.0_amd64_static.deb`, ~946KB)
```bash
./build_deb.sh static
sudo dpkg -i yabatman_1.0_amd64_static.deb
```

The `.deb` package includes:
- **`/usr/bin/yabatman`** — GUI application
- **`/usr/sbin/yabatmand`** — Power management daemon
- **`/usr/share/applications/yabatman.desktop`** — Desktop launcher
- **`/etc/xdg/autostart/yabatman.desktop`** — Session autostart entry
- **`/usr/share/icons/hicolor/*/apps/yabatman.png`** — Application icon

The `postinst` script automatically detects your init system (systemd, sysvinit, OpenRC, runit) and installs, enables, and starts the `yabatmand` daemon service. Removal via `dpkg -r` cleanly stops and uninstalls the service.

### Manual Installation (without .deb)

If you prefer a manual install after building:

```bash
# Install binaries
sudo install -m 0755 build/yabatman  /usr/bin/yabatman
sudo install -m 0755 build/yabatmand /usr/sbin/yabatmand

# Create and enable systemd service
sudo tee /etc/systemd/system/yabatmand.service <<EOF
[Unit]
Description=YaBatman Power Management Daemon
After=local-fs.target

[Service]
Type=simple
ExecStart=/usr/sbin/yabatmand
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now yabatmand.service
```

### Running for Development

For development/testing without installing system-wide:

```bash
# Start the daemon (requires root)
sudo ./build/yabatmand

# In another terminal, start the GUI
./build/yabatman
```

---

## Regenerating Embedded Icons

All icons displayed by the GUI are embedded directly into the binary at compile time as C byte arrays (no external icon files needed at runtime). If you modify any icon in the `icons/` directory, you must regenerate the header file before rebuilding:

```bash
python3 convert_images.py ./icons/
```

This script:
1. Reads all PNG icons from the specified directory
2. Pre-scales menu icons to their target sizes (24×24 for most, 14×14 for checkmarks) using Lanczos resampling for crisp rendering
3. Generates `src/battery_icons.h` containing the raw PNG byte arrays as C `static const unsigned char[]` data

**Requirements**: Python 3 with `Pillow` (PIL) for menu icon resizing. Without Pillow, icons are embedded at their original resolution.

After regenerating, simply rebuild with `./build.sh`.

---

## Configuration

All user configuration is stored in `~/.config/yabatman/` and managed through the Settings dialog (right-click tray icon → Settings). Battery history data is stored as a compact binary file in the same directory.

The daemon itself is stateless — it receives all profile parameters from the GUI via the Unix socket on each AC/battery transition or profile change.

---

## Acknowledgements

A special thanks to the [TLP](https://linrunner.de/tlp/) project. TLP's extensive documentation and scripting provided immense inspiration for the power-saving strategies implemented within Yabatman. While Yabatman reimplements these strategies natively in C for maximum performance and adds a rich GUI layer, TLP's pioneering work in cataloguing Linux power-saving mechanisms was invaluable.

---

## Screenshots

| | | |
| :---: | :---: | :---: |
| <a href="screenshots/screenshot_popup.jpg"><img src="screenshots/screenshot_popup.jpg" width="230" alt="screenshot 1"></a> | <a href="screenshots/screenshot_battery_info.jpg"><img src="screenshots/screenshot_battery_info.jpg" width="230" alt="screenshot 2"></a> | <a href="screenshots/screenshot_usage_history.jpg"><img src="screenshots/screenshot_usage_history.jpg" width="230" alt="screenshot 3"></a> |
| <a href="screenshots/screenshot_settings1.jpg"><img src="screenshots/screenshot_settings1.jpg" width="230" alt="screenshot 4"></a> | <a href="screenshots/screenshot_settings2.jpg"><img src="screenshots/screenshot_settings2.jpg" width="230" alt="screenshot 5"></a> | <a href="screenshots/screenshot_settings3.jpg"><img src="screenshots/screenshot_settings3.jpg" width="230" alt="screenshot 6"></a> |
| <a href="screenshots/screenshot_screensavers.jpg"><img src="screenshots/screenshot_screensavers.jpg" width="230" alt="screenshot 7"></a> | <a href="screenshots/screenshot_calibration.jpg"><img src="screenshots/screenshot_calibration.jpg" width="230" alt="screenshot 8"></a> | |


