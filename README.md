# :package: AppImage Tools

These AppImage tools contains unofficial utilities for building AppImages. It currently consists of two utilities, covering the two things needed to create a AppImage:

- **`appimagedeploy`** - scans an AppDir's executables for their shared-library dependencies, copies/rpath-patches everything that isn't already assumed present on the target system into the AppDir, writes `AppRun`, and bundles Qt 6 plugins/QML modules and GStreamer plugins as needed.

- **`appimagebuilder`** - packages an already-deployed AppDir into a `.AppImage` file via `mksquashfs` plus embedding the AppImage runtime.

## :dart: Supported applications

`appimagedeploy` targets **Qt 6, C++/CMake-based Linux applications** laid out as a standard FHS-like AppDir (`usr/bin/<binary>`, `usr/share/applications/<id>.desktop`, `usr/share/icons/...`), i.e. the result of a normal `cmake --install` / `make DESTDIR=AppDir install`. It is not a generic linker-closure tool for arbitrary binaries: it specifically knows how to detect and bundle Qt 6 plugins/QML, optional GStreamer 1.0 media backends, and the GNOME/GTK stack (Gdk-Pixbuf, GTK 2/3/4, GIO, GLib schemas) *if and when the application actually depends on them* - none of this is required, an application that only depends on Qt6Core/Qt6Gui works fine too.

The AppDir can either be produced by a single application's own build (e.g. Strawberry Music Player, which this tool was built for and is regularly tested against), or any other Qt 6 project with a similar layout.

## :inbox_tray: What gets bundled

`appimagedeploy` walks the ELF dependency graph of everything already in the AppDir (rpath/runpath first, then `$QTDIR`/`$QT_ROOT_DIR`, `$LD_LIBRARY_PATH`, `ld.so.cache`/`ld.so.conf`, then the standard system library directories), copies every dependency in, and rpath-patches each copy. On top of that plain dependency walk, it detects and bundles the following **only if the application actually depends on them**:

| Subsystem | Detected via | What gets bundled |
|---|---|---|
| Qt 6 platform plugin | always, if Qt 6 is detected | `plugins/platforms/libqxcb.so` (required; deployment fails without it) |
| Qt 6 Network | `libQt6Network.so.6` | `plugins/bearer/`, `plugins/tls/` + OpenSSL (`libssl.so.3`/`.4`) |
| Qt 6 Sql | `libQt6Sql.so.6` | `plugins/sqldrivers/` - see [Qt SQL driver selection](#qt-sql-driver-selection) |
| Qt 6 Gui | `libQt6Gui.so.6` | `plugins/iconengines/`, `plugins/imageformats/`, `plugins/platforminputcontexts/` |
| Qt 6 OpenGL/XCB | Gui, OpenGL, XcbQpa, or `libxcb-glx` | `plugins/xcbglintegrations/` |
| Qt 6 PrintSupport | `libQt6PrintSupport.so.6` | `plugins/printsupport/libcupsprintersupport.so` |
| Qt 6 Positioning | `libQt6Positioning.so.6` | `plugins/position/` |
| Qt 6 Multimedia | `libQt6Multimedia.so.6` | `plugins/multimedia/`, `plugins/mediaservice/`, `plugins/audio/` (names vary across Qt Multimedia releases; whichever exist are bundled) |
| QML | any QML import found by `qmlimportscanner` | The imported QML modules under `qml/` |
| GStreamer 1.0 | `libgstreamer-1.0.so` | The gstreamer-1.0 plugin directory (filtered - see [GStreamer plugin selection](#gstreamer-plugin-selection)) plus `gst-plugin-scanner`, plus `libsoup-3.0.so.0` if `libgstsoup.so` is present (it's `dlopen()`'d, not linked, so the ordinary dependency walk misses it) |
| Gdk-Pixbuf | `libgdk_pixbuf` | Pixbuf loaders + a patched `loaders.cache` |
| GTK 2/3/4 | `libgtk-<2\|3\|4>` | The matching GTK module directory, default theme (GTK ≤ 3), and a patched `immodules.cache` (GTK ≤ 3) |
| ALSA | `libasound` | **Never bundled** - `libasound.so*` is removed from the dependency set so it always resolves against the running system's alsa-lib (plugins, config, and hardware UCM profiles are versioned together and bundling them breaks device access on a mismatched host) |
| PulseAudio | `libpulse` | The PulseAudio plugin directory |
| GIO | `libgio-2.0` | `gio/modules/` + `giomodule.cache`, exposed via `GIO_EXTRA_MODULES` (see [GIO modules and the GnuTLS trust store](#gio-modules-and-the-gnutls-trust-store)) |
| GLib schemas | `usr/share/glib-2.0/schemas` present in the AppDir | Compiles them with `glib-compile-schemas` if not already compiled |
| Fontconfig | always | Symlinks `etc/fonts/fonts.conf` to the build host's fontconfig if the AppDir doesn't ship its own |
| GnuTLS | `libgnutls` | The build host's crypto-policy config file, exposed via `GNUTLS_SYSTEM_PRIORITY_FILE` (see below) |
| Nvidia | any `libnvidia*` found | **Refuses to deploy** - bundling Nvidia's driver libraries alongside the target system's own `libGL` reliably segfaults |

Libraries listed in the bundled exclude list (`data/excludelist.txt`, only consulted when `-s`/`--use-exclude-list` is passed) are assumed already present on every target system and are skipped instead of bundled.

## :wrench: Build from Source

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

## :gear: Requirements

- A C/C++ compiler
- [CMake 3.13 or higher](https://cmake.org/)
- [Qt 6.4 or higher](https://www.qt.io/) (Core, Gui and Svg)
- [mksquashfs 4.4 or higher](https://github.com/plougher/squashfs-tools)
- [patchelf](https://github.com/nixos/patchelf)
- [LIEF](https://lief.re/)

Optional, only needed if the application actually uses them: `gst-inspect-1.0` (for anything but `--gstreamer-plugins=all`), `glib-compile-schemas`, `appstreamcli`, `desktop-file-validate`.

## Usage

```bash
# Deploy dependencies into an AppDir
./appimagedeploy AppDir/usr/share/applications/myapp.desktop

# Package a deployed AppDir into a .AppImage
./appimagebuilder --version 1.0 AppDir
```

### `appimagedeploy` options

| Option | Description |
|---|---|
| `--preserve-cwd` | Preserve the current working directory when running the app (by default `AppRun` behaves like a normal installed binary) |
| `-s`, `--use-exclude-list` | Skip bundling any library named in the built-in exclude list instead of copying it in |
| `--gstreamer-plugins=<set>` | Which GStreamer plugins to bundle: `core`, `audio`, `video`, `all`, or `list` (default `all`) - see below |
| `--gstreamer-plugin-list=<names>` | Comma-separated GStreamer plugin names to bundle exactly, e.g. `coreelements,playback,typefindfunctions,audioconvert,lame,flac,soup`; only used when `--gstreamer-plugins=list` |
| `--qt-sql-plugins=<value>` | Which Qt SQL drivers to bundle: `none`, `all`, or a comma-separated list of driver names, e.g. `sqlite,mysql,psql` (default `all`) - see below |

#### GStreamer plugin selection

`--gstreamer-plugins` picks a subset of the build host's `gstreamer-1.0` plugin directory:

- **`core`** - only generic/infrastructure plugins (`coreelements`, `playback`, `typefindfunctions`, container demuxers/muxers/parsers like `matroska`/`isomp4`/`ogg`, ...) - no codec classified Audio or Video.
- **`audio`** - `core` plus every plugin GStreamer classifies as containing an Audio element (`lame`, `flac`, `vorbis`, `opus`, `mpg123`, ...).
- **`video`** - `core` plus every plugin classified as containing a Video element.
- **`all`** (default) - every plugin found, unfiltered; no `gst-inspect-1.0` classification pass is needed for this mode.
- **`list`** - only the exact plugin names passed via `--gstreamer-plugin-list`, resolved through `gst-inspect-1.0`; nothing else is bundled. This is a manual escape hatch for the handful of plugins GStreamer's own `Klass` metadata classifies ambiguously (e.g. `dvdsub`'s subtitle parser isn't classified Video at all, and `videoframe_audiolevel` is classified Audio despite needing a video frame).

`core`/`audio`/`video`/`list` all require `gst-inspect-1.0` on `$PATH`; `all` does not.

#### Qt SQL driver selection

`--qt-sql-plugins` picks which drivers from `plugins/sqldrivers/` are bundled, by driver name (`sqlite`, `mysql`, `psql`, `odbc`, `oci`, `db2`, `ibase`, `mimer`):

- **`all`** (default) - bundles every driver plugin found, matching the tool's original behavior.
- **`none`** - skips `plugins/sqldrivers/` entirely.
- **A comma-separated list**, e.g. `--qt-sql-plugins=sqlite,mysql` - bundles only those drivers; an unresolvable driver name is a hard error rather than a silent skip.

### `appimagebuilder` options

| Option | Description |
|---|---|
| `--version <string>` | Version string stamped into `X-AppImage-Version` and used in the output filename. If not given, `appimagebuilder` runs the AppDir's main executable (resolved from `Exec=` in its `.desktop` file) with `--version` and extracts a version-like token (e.g. `1.2.24` or `1.2.24-11-g754b469c4`) from the last line of its output. |

The default output filename is `<Name>-<version>-Linux-<arch>.AppImage`.

## :closed_lock_with_key: GIO modules and the GnuTLS trust store

Bundling GLib-based apps (or Qt apps that pull in `libgio-2.0`, e.g. via GStreamer's `soup`/`gio` plugins) means the bundled GIO no longer sees the build host's system-installed GIO modules - it needs its own copy of `gio/modules/` (TLS backend, GSettings backends, proxy resolvers, volume monitors), which `appimagedeploy` bundles and exposes via `GIO_EXTRA_MODULES` in `AppRun`.

Getting TLS working end-to-end inside the AppImage needs two more things, of which only the first is currently solved:

- **GnuTLS priority/crypto-policy config** - the bundled GnuTLS has the build host's crypto-policy path (`/etc/crypto-policies/back-ends/gnutls.config`, the openSUSE/Fedora/RHEL crypto-policies mechanism) baked in as its compiled-in default. On a host that doesn't ship that mechanism at all (Debian/Ubuntu), the bundled GnuTLS otherwise fails outright with `Failed to set GnuTLS session priority with error beginning at %COMPAT`. `appimagedeploy` bundles a copy of that config file and `AppRun` points `GNUTLS_SYSTEM_PRIORITY_FILE` at it, which fixes this failure regardless of the target distribution.
- **Root/trust certificates - known, unfixed limitation.** GnuTLS's system trust store is loaded through the bundled `p11-kit-trust.so` PKCS#11 module, which has its trust-anchor search paths (`/etc/pki/trust`, `/usr/share/pki/trust` - again the openSUSE/Fedora/RHEL convention) compiled in at build time. On a target system that doesn't share that exact layout (Debian/Ubuntu, which uses `/usr/local/share/ca-certificates` + `/etc/ca-certificates.conf` instead), those paths simply don't exist, and the bundled GnuTLS reports `System trust contains zero trusted certificates; please investigate your GnuTLS configuration` even though the priority-file fix above already lets TLS *negotiation* succeed. There is no environment variable to override p11-kit's compiled-in search paths the way `GNUTLS_SYSTEM_PRIORITY_FILE` overrides the priority file. `AppRun` separately exports `SSL_CERT_FILE` (picked from whichever of the usual system CA bundle paths exists on the target), which helps OpenSSL-based TLS consumers, but does not affect GnuTLS/p11-kit's own trust loading at all. Properly fixing this would require either bundling a portable, file-based GnuTLS build instead of the system one, or a persistent, outside-the-AppDir change on the target system; neither is done by this tool today.
