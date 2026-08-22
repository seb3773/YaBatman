#!/usr/bin/env bash
set -euo pipefail

PKG_NAME="yabatman"
PKG_VERSION="1.1-2"
PKG_MAINTAINER="seb3773"
PKG_SECTION="admin"
PKG_PRIORITY="optional"

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
ARCH="$(dpkg --print-architecture)"
STATIC_BUILD=false
BUILD_DIR="$SRC_ROOT/build"
OUT_DEB="$SRC_ROOT/${PKG_NAME}_${PKG_VERSION}_${ARCH}.deb"
DEPENDS="tdelibs14-trinity, libtqt3-mt, libtqtinterface, libnotify4, libudev1, libsystemd0, libx11-6, libxss1, libxext6, libxtst6"

if test "${1:-}" = "static" || test "${1:-}" = "--static"; then
	STATIC_BUILD=true
	BUILD_DIR="$SRC_ROOT/build-static"
	OUT_DEB="$SRC_ROOT/${PKG_NAME}_${PKG_VERSION}_${ARCH}_static.deb"
	DEPENDS="libnotify4, libudev1, libsystemd0, libx11-6, libxss1, libxext6, libxtst6"
	echo "=== Packaging in STATIC standalone mode (yabatman_1.0_amd64_static.deb) ==="
fi

PKGROOT="$BUILD_DIR/pkgroot"

need_cmd() {
	command -v "$1" >/dev/null 2>&1 || {
		echo "error: missing required command: $1" >&2
		exit 1
	}
}

need_cmd cmake
need_cmd pkg-config
need_cmd dpkg-deb
need_cmd strip
need_cmd install

mkdir -p -- "$BUILD_DIR"

# Build release binaries via build.sh
if $STATIC_BUILD; then
	"$SRC_ROOT/build.sh" static
else
	"$SRC_ROOT/build.sh"
fi

# Verify binaries exist
for bin in yabatman yabatmand; do
	if test ! -f "$BUILD_DIR/$bin"; then
		echo "error: missing built binary: $BUILD_DIR/$bin" >&2
		exit 1
	fi
done

# ── Stage filesystem layout ──────────────────────────────────────────
rm -rf -- "$PKGROOT"
mkdir -p -- \
	"$PKGROOT/DEBIAN" \
	"$PKGROOT/usr/bin" \
	"$PKGROOT/usr/sbin" \
	"$PKGROOT/usr/share/applications" \
	"$PKGROOT/usr/share/icons/hicolor" \
	"$PKGROOT/etc/xdg/autostart"

# Install binaries
install -m 0755 "$BUILD_DIR/yabatman"  "$PKGROOT/usr/bin/yabatman"
install -m 0755 "$BUILD_DIR/yabatmand" "$PKGROOT/usr/sbin/yabatmand"

# Strip staged binaries
for staged in "$PKGROOT/usr/bin/yabatman" "$PKGROOT/usr/sbin/yabatmand"; do
	if command -v sstrip >/dev/null 2>&1; then
		sstrip "$staged" >/dev/null 2>&1 || true
	else
		strip --strip-all "$staged" >/dev/null 2>&1 || true
	fi
done

# ── Desktop entry ────────────────────────────────────────────────────
cat > "$PKGROOT/usr/share/applications/yabatman.desktop" <<EOF
[Desktop Entry]
Version=1.0
Name=YaBatman
GenericName=Battery Monitor & Power Manager
Comment=Advanced battery monitoring and power management for Linux laptops
Exec=yabatman
Icon=yabatman
Terminal=false
Type=Application
Categories=System;Utility;Monitor;HardwareSettings;
EOF
chmod 0644 "$PKGROOT/usr/share/applications/yabatman.desktop"

# Autostart entry (UI launches at graphical session login)
if $STATIC_BUILD; then
	mkdir -p -- "$PKGROOT/etc/xdg/autostart"
	cat > "$PKGROOT/etc/xdg/autostart/yabatman.desktop" <<EOF
[Desktop Entry]
Version=1.0
Name=YaBatman
GenericName=Battery Monitor & Power Manager
Comment=Battery monitor and power manager
Exec=yabatman
Icon=yabatman
Terminal=false
Type=Application
X-GNOME-Autostart-enabled=true
Categories=System;Utility;Monitor;HardwareSettings;
EOF
	chmod 0644 "$PKGROOT/etc/xdg/autostart/yabatman.desktop"
else
	# TDE / Trinity specific autostart directory (prevents dual startup in TDE)
	mkdir -p -- "$PKGROOT/opt/trinity/share/autostart"
	cat > "$PKGROOT/opt/trinity/share/autostart/yabatman.desktop" <<EOF
[Desktop Entry]
Version=1.0
Name=YaBatman
GenericName=Battery Monitor & Power Manager
Comment=Battery monitor and power manager
Exec=yabatman
Icon=yabatman
Terminal=false
Type=Application
X-TDE-autostart-after=panel
X-TDE-StartupNotify=false
X-TDE-UniqueApplet=true
X-KDE-autostart-after=panel
Categories=System;Utility;Monitor;HardwareSettings;
EOF
	chmod 0644 "$PKGROOT/opt/trinity/share/autostart/yabatman.desktop"
fi

# ── Application icon (hicolor tree with symlinks) ────────────────────
ICON_SRC="$SRC_ROOT/icons/yabatman.png"
if test -f "$ICON_SRC"; then
	real_sz="64x64"
	real_dir="$PKGROOT/usr/share/icons/hicolor/$real_sz/apps"
	mkdir -p -- "$real_dir"
	install -m 0644 "$ICON_SRC" "$real_dir/yabatman.png"
	for sz in 16x16 22x22 24x24 32x32 48x48; do
		dstdir="$PKGROOT/usr/share/icons/hicolor/$sz/apps"
		mkdir -p -- "$dstdir"
		ln -sf "../../$real_sz/apps/yabatman.png" "$dstdir/yabatman.png"
	done
else
	echo "warning: missing $ICON_SRC (application icon will not be installed)" >&2
fi

# ── Debian control file ──────────────────────────────────────────────
INSTALLED_SIZE_KB="$(du -sk "$PKGROOT/usr" "$PKGROOT/etc" 2>/dev/null | awk '{s+=$1} END{print s}')"
DESC_EXTRA=""
if $STATIC_BUILD; then
	DESC_EXTRA=" (Standalone static GUI build - no TDE or TQt3 dependencies required)"
fi

cat > "$PKGROOT/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $PKG_VERSION
Section: $PKG_SECTION
Priority: $PKG_PRIORITY
Architecture: $ARCH
Maintainer: $PKG_MAINTAINER
Installed-Size: $INSTALLED_SIZE_KB
Depends: $DEPENDS
Description: Advanced battery monitor and power manager for Linux laptops$DESC_EXTRA
 YaBatman combines a highly configurable battery monitor with detailed
 history tracking, exhaustive battery information, and a complete power
 management system. Features include per-profile CPU governor/frequency
 control, PCI/USB/SATA power policies, screen brightness management,
 Wi-Fi power saving, process freezing, webcam power control, inactivity
 detection with screensavers, and automatic profile switching on AC/battery
 transitions. The daemon runs as root via systemd; the GUI communicates
 through a Unix socket without requiring polkit or sudo.
EOF

# ── postinst: install & start daemon service ─────────────────────────
cat > "$PKGROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

DAEMON_BIN="/usr/sbin/yabatmand"

if [ "$1" = "configure" ]; then
    detect_init() {
        p1_comm=""
        if [ -f /proc/1/comm ]; then
            p1_comm=$(cat /proc/1/comm)
        fi

        if [ "$p1_comm" = "systemd" ] || [ -d /run/systemd/system ]; then
            echo "systemd"
            return
        fi

        if [ "$p1_comm" = "runit" ] || [ "$p1_comm" = "runsvdir" ] || [ -d /run/runit ]; then
            echo "runit"
            return
        fi

        if which rc-status >/dev/null 2>&1; then
            echo "openrc"
            return
        fi

        if [ -d /etc/init.d ]; then
            if [ -f /etc/inittab ]; then
                echo "sysvinit_inittab"
            else
                echo "sysvinit"
            fi
            return
        fi

        echo "fallback"
    }

    INIT_SYSTEM=$(detect_init)
    echo "yabatman: configuring daemon startup for $INIT_SYSTEM..."

    case "$INIT_SYSTEM" in
        systemd)
            cat <<'INNER_EOF' > /etc/systemd/system/yabatmand.service
[Unit]
Description=YaBatman Power Management Daemon
Documentation=https://github.com/seb3773/yabatman
After=local-fs.target

[Service]
Type=simple
ExecStart=/usr/sbin/yabatmand
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
INNER_EOF
            chmod 644 /etc/systemd/system/yabatmand.service
            systemctl daemon-reload || true
            systemctl enable yabatmand.service || true
            systemctl start yabatmand.service || true
            ;;

        sysvinit_inittab)
            if ! grep -q "^ybd:2345:respawn:$DAEMON_BIN" /etc/inittab; then
                echo "" >> /etc/inittab
                echo "# YaBatman Power Management Daemon" >> /etc/inittab
                echo "ybd:2345:respawn:$DAEMON_BIN" >> /etc/inittab
            fi
            if which telinit >/dev/null 2>&1; then
                telinit q || true
            elif which init >/dev/null 2>&1; then
                init q || true
            fi
            if ! pgrep -f "$DAEMON_BIN" >/dev/null 2>&1; then
                "$DAEMON_BIN" &
            fi
            ;;

        sysvinit|openrc)
            cat <<'INNER_EOF' > /etc/init.d/yabatmand
#!/bin/sh
### BEGIN INIT INFO
# Provides:          yabatmand
# Required-Start:    $local_fs $syslog
# Required-Stop:     $local_fs $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: YaBatman Power Management Daemon
# Description:       Controls CPU governor, power policies and energy management
### END INIT INFO

DAEMON=/usr/sbin/yabatmand
NAME=yabatmand
PIDFILE=/var/run/yabatmand.pid

if ! which start-stop-daemon >/dev/null 2>&1; then
    start_daemon() {
        "$DAEMON" &
        echo $! > "$PIDFILE"
    }
    stop_daemon() {
        if [ -f "$PIDFILE" ]; then
            pid=$(cat "$PIDFILE")
            kill "$pid" 2>/dev/null
            sleep 1
            kill -9 "$pid" 2>/dev/null
            rm -f "$PIDFILE"
        fi
    }
else
    start_daemon() {
        start-stop-daemon --start --background --make-pidfile --pidfile "$PIDFILE" --exec "$DAEMON"
    }
    stop_daemon() {
        start-stop-daemon --stop --pidfile "$PIDFILE" --retry=TERM/10/KILL/5
        rm -f "$PIDFILE"
    }
fi

case "$1" in
  start)
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        exit 0
    fi
    start_daemon
    ;;
  stop)
    stop_daemon
    ;;
  restart)
    $0 stop
    sleep 1
    $0 start
    ;;
  status)
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
      exit 0
    else
      exit 3
    fi
    ;;
esac
INNER_EOF
            chmod 755 /etc/init.d/yabatmand

            if which rc-update >/dev/null 2>&1; then
                rc-update add yabatmand default || true
                rc-service yabatmand start || true
            elif which update-rc.d >/dev/null 2>&1; then
                update-rc.d yabatmand defaults || true
                update-rc.d yabatmand enable || true
                invoke-rc.d yabatmand start || /etc/init.d/yabatmand start || true
            elif which chkconfig >/dev/null 2>&1; then
                chkconfig --add yabatmand || true
                chkconfig yabatmand on || true
                service yabatmand start || /etc/init.d/yabatmand start || true
            else
                /etc/init.d/yabatmand start || true
            fi
            ;;

        runit)
            mkdir -p /etc/sv/yabatmand
            cat <<'INNER_EOF' > /etc/sv/yabatmand/run
#!/bin/sh
exec /usr/sbin/yabatmand
INNER_EOF
            chmod 755 /etc/sv/yabatmand/run

            service_dir=""
            if [ -d /var/service ]; then
                service_dir="/var/service"
            elif [ -d /etc/service ]; then
                service_dir="/etc/service"
            fi

            if [ -n "$service_dir" ]; then
                ln -sf /etc/sv/yabatmand "$service_dir/yabatmand"
                which sv >/dev/null 2>&1 && sv start yabatmand || true
            fi
            ;;

        fallback)
            if [ -f /etc/rc.local ]; then
                if ! grep -q "$DAEMON_BIN" /etc/rc.local; then
                    if grep -q "^exit 0" /etc/rc.local; then
                        sed -i "s/^exit 0/$DAEMON_BIN \&\nexit 0/" /etc/rc.local
                    else
                        echo "$DAEMON_BIN &" >> /etc/rc.local
                    fi
                    chmod +x /etc/rc.local
                fi
            else
                cat << INNER_EOF > /etc/rc.local
#!/bin/sh -e
$DAEMON_BIN &
exit 0
INNER_EOF
                chmod +x /etc/rc.local
            fi
            if ! pgrep -f "$DAEMON_BIN" >/dev/null 2>&1; then
                "$DAEMON_BIN" &
            fi
            ;;
    esac

    # Clean up leftover xdg autostart entry if installing TDE package to avoid double launch
    rm -f /etc/xdg/autostart/yabatman.desktop

    # Refresh icon cache
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
    fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
    fi
fi
exit 0
EOF
chmod 0755 "$PKGROOT/DEBIAN/postinst"

# ── prerm: stop & disable daemon service ─────────────────────────────
cat > "$PKGROOT/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e

DAEMON_BIN="/usr/sbin/yabatmand"

if [ "$1" = "remove" ] || [ "$1" = "deconfigure" ]; then
    detect_init() {
        p1_comm=""
        if [ -f /proc/1/comm ]; then
            p1_comm=$(cat /proc/1/comm)
        fi

        if [ "$p1_comm" = "systemd" ] || [ -d /run/systemd/system ]; then
            echo "systemd"
            return
        fi

        if [ "$p1_comm" = "runit" ] || [ "$p1_comm" = "runsvdir" ] || [ -d /run/runit ]; then
            echo "runit"
            return
        fi

        if which rc-status >/dev/null 2>&1; then
            echo "openrc"
            return
        fi

        if [ -d /etc/init.d ]; then
            if [ -f /etc/inittab ]; then
                echo "sysvinit_inittab"
            else
                echo "sysvinit"
            fi
            return
        fi

        echo "fallback"
    }

    INIT_SYSTEM=$(detect_init)
    echo "yabatman: stopping daemon before removal..."

    case "$INIT_SYSTEM" in
        systemd)
            if systemctl is-active --quiet yabatmand.service 2>/dev/null; then
                systemctl stop yabatmand.service || true
            fi
            systemctl disable yabatmand.service 2>/dev/null || true
            ;;

        sysvinit_inittab)
            if [ -f /etc/inittab ]; then
                sed -i "\@^ybd:2345:respawn:$DAEMON_BIN@d" /etc/inittab
                sed -i '/# YaBatman Power Management Daemon/d' /etc/inittab
                if [ -s /etc/inittab ]; then
                    sed -i -e :a -e '/^\n*$/{$d;N;ba}' /etc/inittab
                fi
                if which telinit >/dev/null 2>&1; then
                    telinit q || true
                elif which init >/dev/null 2>&1; then
                    init q || true
                fi
            fi
            pkill -f "$DAEMON_BIN" || true
            ;;

        sysvinit|openrc)
            if which rc-update >/dev/null 2>&1; then
                rc-service yabatmand stop 2>/dev/null || /etc/init.d/yabatmand stop 2>/dev/null || true
                rc-update del yabatmand default 2>/dev/null || true
            elif which update-rc.d >/dev/null 2>&1; then
                /etc/init.d/yabatmand stop 2>/dev/null || true
                update-rc.d -f yabatmand remove 2>/dev/null || true
            elif which chkconfig >/dev/null 2>&1; then
                /etc/init.d/yabatmand stop 2>/dev/null || true
                chkconfig --del yabatmand 2>/dev/null || true
            else
                /etc/init.d/yabatmand stop 2>/dev/null || true
            fi
            ;;

        runit)
            service_dir=""
            if [ -d /var/service ]; then
                service_dir="/var/service"
            elif [ -d /etc/service ]; then
                service_dir="/etc/service"
            fi

            if [ -n "$service_dir" ] && [ -L "$service_dir/yabatmand" ]; then
                rm -f "$service_dir/yabatmand"
            fi
            which sv >/dev/null 2>&1 && sv stop yabatmand 2>/dev/null || true
            ;;

        fallback)
            if [ -f /etc/rc.local ]; then
                sed -i "\@$DAEMON_BIN@d" /etc/rc.local
            fi
            pkill -f "$DAEMON_BIN" || true
            ;;
    esac

    pkill -f "$DAEMON_BIN" || true

    # Refresh icon cache
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
    fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
    fi
fi
exit 0
EOF
chmod 0755 "$PKGROOT/DEBIAN/prerm"

# ── postrm: purge residual service files ─────────────────────────────
cat > "$PKGROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e

if [ "$1" = "purge" ]; then
    echo "yabatman: purging service files..."
    rm -f /etc/systemd/system/yabatmand.service
    rm -f /etc/init.d/yabatmand
    rm -rf /etc/sv/yabatmand

    if which systemctl >/dev/null 2>&1; then
        systemctl daemon-reload || true
    fi
fi
exit 0
EOF
chmod 0755 "$PKGROOT/DEBIAN/postrm"

# ── Build .deb ───────────────────────────────────────────────────────
rm -f -- "$OUT_DEB"

dpkg-deb --build "$PKGROOT" "$OUT_DEB" >/dev/null

echo "Debian package successfully built: $OUT_DEB"
exit 0
