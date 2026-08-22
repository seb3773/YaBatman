#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# Script de Génération de l'Installeur Q4OS (.qsi) pour YaBatman
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QSI_DIR="$SCRIPT_DIR/qsi_setup"
DEB_DIR="$QSI_DIR/deb_packages"
OUT_DIR="$QSI_DIR/output"
TEMPLATES_DIR="$QSI_DIR/setup_templates"
APP_VERSION="${1:-1.1-2}"
ARCH="$(dpkg --print-architecture)"

echo "=================================================="
echo " YaBatman Q4OS .qsi Installer Builder"
echo " Target Version: $APP_VERSION ($ARCH)"
echo "=================================================="

# 1. Vérification des outils nécessaires
if ! command -v build-qinstaller >/dev/null 2>&1; then
    echo "[Error] 'build-qinstaller' non trouvé. Installez le paquet : sudo apt install q4os-devpack-base" >&2
    exit 1
fi

# 2. Préparation des répertoires de staging
mkdir -p "$DEB_DIR" "$OUT_DIR" "$TEMPLATES_DIR"
rm -f "$DEB_DIR"/*.deb "$OUT_DIR"/*.qsi

# 3. Compilation du paquet .deb si nécessaire
TARGET_DEB="$SCRIPT_DIR/yabatman_${APP_VERSION}_${ARCH}.deb"
if [ ! -f "$TARGET_DEB" ]; then
    echo "Compilation préalable du paquet Debian standard..."
    "$SCRIPT_DIR/build_deb.sh"
fi

if [ ! -f "$TARGET_DEB" ]; then
    echo "[Error] Paquet Debian introuvable : $TARGET_DEB" >&2
    exit 1
fi

cp -a "$TARGET_DEB" "$DEB_DIR/"
DEB_FILENAME=$(basename "$TARGET_DEB")
PACKAGE_NAME="yabatman"
echo "Paquet Debian inclus : $DEB_FILENAME"

# 4. Génération dynamique du fichier qinstaller avec chemins absolus
cat <<EOF > "$QSI_DIR/qinstaller"
#***q4os*setup*config*header*do*not*delete*it***#
PK_NAME="$PACKAGE_NAME"
APPNAME_DESC="YaBatman Power Manager"
APP_ICON="yabatman"
PK_VERS="$APP_VERSION"
SETUP_TYPE="2"
INST_DEBS="$PACKAGE_NAME"
DEBPCKS_DIR="$DEB_DIR"
TEMPLATES_DIR="$TEMPLATES_DIR"
OUT_DIR="$OUT_DIR"
APPLNK_ENTRY="1"
DESKTOP_ENTRY="0"
MENU_ENTRY="1"
DSTR_BASE="debian;ubuntu"
DSTR_EDTN="bullseye;bookworm;trixie;jammy;noble"
Q4VER_MIN="4.0"
CHK_INET="0"
EOF

# 5. Exécution du générateur d'installeur Q4OS
echo "Exécution de build-qinstaller..."
(
    cd "$QSI_DIR"
    build-qinstaller qinstaller
)

# 6. Récupération et placement de l'installeur final
LATEST_QSI=$(ls -t "$OUT_DIR"/*.qsi 2>/dev/null | head -n 1)
if [ -n "$LATEST_QSI" ] && [ -f "$LATEST_QSI" ]; then
    FINAL_QSI_NAME="yabatman_${APP_VERSION}_${ARCH}.qsi"
    cp -a "$LATEST_QSI" "$SCRIPT_DIR/$FINAL_QSI_NAME"
    chmod 0755 "$SCRIPT_DIR/$FINAL_QSI_NAME"
    echo ""
    echo "=================================================="
    echo " SUCCÈS : Installeur Q4OS généré avec succès !"
    echo " Fichier : $SCRIPT_DIR/$FINAL_QSI_NAME"
    echo " Taille  : $(ls -lh "$SCRIPT_DIR/$FINAL_QSI_NAME" | awk '{print $5}')"
    echo "=================================================="
else
    echo "[Error] Échec lors de la génération du fichier .qsi." >&2
    exit 1
fi
