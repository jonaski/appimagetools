/*
 * AppImage Tools
 * Copyright 2024-2026, Jonas Kvinge <jonas@jkvinge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ELFFILE_H
#define ELFFILE_H

#include <memory>

#include <LIEF/ELF.hpp>

#include <QString>
#include <QStringList>

// Wrapper around LIEF::ELF::Binary for read-only ELF introspection, plus SetRpath(), which shells out to `patchelf --set-rpath`.
//
// SetRpath() does NOT use LIEF::ELF::Binary::write() to rewrite the file in place: LIEF fully re-serializes the ELF (relocations, PLT/GOT, everything), and for a large/complex shared library (e.g. libQt6Core.so.6) this has been observed to silently corrupt the address-identity-sensitive mechanism Qt's function-pointer-based QObject::connect() uses to resolve signals declared in that library - old-style string-based connect() and everything else keeps working, only new-style pointer connects to the library's own signals start silently failing. patchelf's minimal in-place edit does not have this problem.
class ElfFile {

 public:
  explicit ElfFile(const QString &path);
  ~ElfFile();

  ElfFile(ElfFile &&elf_file) noexcept;
  ElfFile &operator=(ElfFile &&elf_file) noexcept;

  ElfFile(const ElfFile &elf_file) = delete;
  ElfFile &operator=(const ElfFile &elf_file) = delete;

  // False if the file could not be parsed as an ELF binary by LIEF.
  bool IsValid() const;

  // Returns true if the first bytes of the file at `path` are the ELF magic number (0x7F 'E' 'L' 'F'). Cheap pre-check before attempting a full LIEF parse.
  static bool IsElf(const QString &path);

  QString path() const { return path_; }

  // DT_NEEDED entries: bare library filenames (e.g. "libQt6Core.so.6"), not full paths.
  QStringList ImportedLibraries() const;

  // Search paths from DT_RUNPATH if present, else DT_RPATH, else empty.
  QStringList ExistingRpaths() const;

  // Sets the rpath to `paths` joined with ':' by shelling out to `patchelf --set-rpath` (see the class comment for why this isn't done via LIEF).
  // Returns true on success.
  bool SetRpath(const QStringList &paths);

  // PT_INTERP value (e.g. "/lib64/ld-linux-x86-64.so.2"), or an empty string if this ELF has none (typical for a plain shared library).
  QString Interpreter() const;

  // "x86_64" / "i686" / "armhf" / "aarch64", or an empty string if unrecognized. Naming matches helpers.GetElfArchitecture's mapping.
  QString Architecture() const;

 private:
  static QString ArchName(const LIEF::ELF::ARCH arch);

 private:
  QString path_;
  std::unique_ptr<LIEF::ELF::Binary> binary_;
};

#endif  // ELFFILE_H
