#!/bin/bash
#
# AppImage Tools
# Copyright 2024-2026, Jonas Kvinge <jonas@jkvinge.net>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with This program.  If not, see <http://www.gnu.org/licenses/>.
#

ROOT="$(dirname "$(readlink -f "${0}")")"
MAIN=$(grep -r "^Exec=.*" "${ROOT}"/*.desktop | head -n 1 | cut -d "=" -f 2 | cut -d " " -f 1)


# Try to find a binary with the same name as the AppImage or the symlink through which it was invoked, without any suffix
if [ -z "$ARGV0" ] ; then
  # AppRun is being executed outside of an AppImage
  ARGV0="$0"
fi
BINARY_NAME="$(basename "$ARGV0")"
if [ "${BINARY_NAME}" = "AppRun" ] ; then
  unset BINARY_NAME
fi

BINARY_NAME="${BINARY_NAME%.*}"
MAIN_BIN=$(find "${ROOT}/usr/bin" -name "${BINARY_NAME}" | head -n 1)

# Fall back to finding the main binary based on the Exec= line in the desktop file
if [ -z "$MAIN_BIN" ] ; then
  MAIN_BIN=$(find "${ROOT}/usr/bin" -name "${MAIN}" | head -n 1)
fi

LD_LINUX=$(find "${ROOT}" -name 'ld-*.so.*' | head -n 1)


# Set paths
export PATH="${ROOT}"/usr/bin/:"${ROOT}"/usr/sbin/:"${ROOT}"/usr/games/:"${ROOT}"/bin/:"${ROOT}"/sbin/:"${PATH}"
export XDG_DATA_DIRS="${ROOT}"/usr/share/:"${XDG_DATA_DIRS}"
export FONTCONFIG_FILE="${ROOT}/etc/fonts/fonts.conf"
export GCONV_PATH=$(find "${ROOT}" -type d -name gconv 2>/dev/null | head -n 1)
export GSETTINGS_SCHEMA_DIR="${ROOT}"/usr/share/glib-2.0/runtime-schemas/:"${ROOT}"/usr/share/glib-2.0/schemas/:"${GSETTINGS_SCHEMA_DIR}"
export GIO_EXTRA_MODULES=$(find "${ROOT}" -type d -path '*/gio/modules' 2>/dev/null | head -n 1)
export GDK_PIXBUF_MODULEDIR=$(find "${ROOT}" -name loaders -type d -path '*gdk-pixbuf*')
export GDK_PIXBUF_MODULE_FILE=$(find "${ROOT}" -name loaders.cache -type f -path '*gdk-pixbuf*')


# Use the system CA bundle if available.
for CERT_FILE in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt /etc/ssl/ca-bundle.pem /etc/pki/tls/cert.pem /etc/ssl/cert.pem; do
  if [ -e "${CERT_FILE}" ]; then
    export SSL_CERT_FILE="${CERT_FILE}"
    break
  fi
done


# The bundled GnuTLS has the build host's crypto-policy path (/etc/crypto-policies/back-ends/gnutls.config) baked in as its compiled-in default priority/config file.
# Set GNUTLS_SYSTEM_PRIORITY_FILE to where it's bundled (the same path, mirrored inside the AppDir).
if [ -e "${ROOT}/etc/crypto-policies/back-ends/gnutls.config" ]; then
  export GNUTLS_SYSTEM_PRIORITY_FILE="${ROOT}/etc/crypto-policies/back-ends/gnutls.config"
fi



# Set GStreamer paths
GST_PLUGIN_CORE_ELEMENTS="$(find "${ROOT}" -name "libgstcoreelements.so" -type f 2>/dev/null | head -1)"
if [ -n "${GST_PLUGIN_CORE_ELEMENTS}" ] ; then
  export GST_PLUGIN_PATH=$(dirname $(readlink -f "${GST_PLUGIN_CORE_ELEMENTS}"))
  export GST_PLUGIN_SCANNER=$(find "${ROOT}" -name "gst-plugin-scanner*" -type f | head -n 1)
  export GST_PLUGIN_SYSTEM_PATH=$GST_PLUGIN_PATH
fi


# Qt
export QT_PLUGIN_PATH="$(readlink -f "$(dirname "$(find "${ROOT}" -type d -path '*/plugins/platforms' 2>/dev/null)" 2>/dev/null)" 2>/dev/null)"
case "${XDG_CURRENT_DESKTOP}" in
  *GNOME*|*gnome*)
    export QT_QPA_PLATFORMTHEME=gtk3
esac

# Opt-in verbose Qt logging, for diagnosing AppImage-specific runtime issues (set APPIMAGE_QT_DEBUG=1 before launching to enable; off by default since "*.debug=true" is far too noisy for normal use)

if [ -n "$APPIMAGE_QT_DEBUG" ] && [ -e "${ROOT}/qtlogging.ini" ] ; then
  export QT_LOGGING_CONF="${ROOT}/qtlogging.ini"
fi


echo "ROOT: ${ROOT}"
echo "MAIN: ${MAIN}"
echo "ARGV0: ${ARGV0}"
echo "BINARY_NAME: ${BINARY_NAME}"
echo "MAIN_BIN: ${MAIN_BIN}"
echo "LD_LINUX: ${LD_LINUX}"
echo "PATH: ${PATH}"
echo "XDG_DATA_DIRS: ${XDG_DATA_DIRS}"
echo "FONTCONFIG_FILE: ${FONTCONFIG_FILE}"
echo "GCONV_PATH: ${GCONV_PATH}"
echo "GSETTINGS_SCHEMA_DIR: ${GSETTINGS_SCHEMA_DIR}"
echo "GIO_EXTRA_MODULES: ${GIO_EXTRA_MODULES}"
echo "GDK_PIXBUF_MODULEDIR: ${GDK_PIXBUF_MODULEDIR}"
echo "GDK_PIXBUF_MODULE_FILE: ${GDK_PIXBUF_MODULE_FILE}"
echo "SSL_CERT_FILE: ${SSL_CERT_FILE}"
echo "GNUTLS_SYSTEM_PRIORITY_FILE: ${GNUTLS_SYSTEM_PRIORITY_FILE}"
echo "GST_PLUGIN_PATH: ${GST_PLUGIN_PATH}"
echo "GST_PLUGIN_SYSTEM_PATH: ${GST_PLUGIN_SYSTEM_PATH}"
echo "GST_PLUGIN_SCANNER: ${GST_PLUGIN_SCANNER}"
echo "QT_PLUGIN_PATH: ${QT_PLUGIN_PATH}"
echo "QT_QPA_PLATFORMTHEME: ${QT_QPA_PLATFORMTHEME}"
echo "QT_LOGGING_CONF: ${QT_LOGGING_CONF}"


if [ -e "${LD_LINUX}" ] ; then
  case $line in
    "ld-linux"*) exec "${LD_LINUX}" --inhibit-cache "${MAIN_BIN}" "$@" ;;
    *) exec "${LD_LINUX}" "${MAIN_BIN}" "$@" ;;
  esac
else
  exec "${MAIN_BIN}" "$@"
fi
