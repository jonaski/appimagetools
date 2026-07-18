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

#include <string>

#include <LIEF/ELF.hpp>

#include <QString>
#include <QStringList>
#include <QFile>
#include <QProcess>
#include <QDebug>

#include "utilities.h"
#include "elffile.h"

using std::exception;
using namespace Qt::Literals::StringLiterals;

ElfFile::ElfFile(const QString &path) : path_(path) {

  try {
    binary_ = LIEF::ELF::Parser::parse(path.toStdString());
  }
  catch (const exception &e) {
    qWarning() << "ElfFile: failed to parse" << path << ":" << e.what();
    binary_.reset();
  }

}

ElfFile::~ElfFile() = default;

ElfFile::ElfFile(ElfFile &&elf_file) noexcept = default;

ElfFile &ElfFile::operator=(ElfFile &&elf_file) noexcept = default;

bool ElfFile::IsValid() const {
  return binary_ != nullptr;
}

bool ElfFile::IsElf(const QString &path) {

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return false;
  const QByteArray magic = file.read(4);
  return magic.size() == 4 && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';

}

QString ElfFile::ArchName(const LIEF::ELF::ARCH arch) {

  switch (arch) {
    case LIEF::ELF::ARCH::X86_64:  return u"x86_64"_s;
    case LIEF::ELF::ARCH::I386:    return u"i686"_s;
    case LIEF::ELF::ARCH::ARM:     return u"armhf"_s;
    case LIEF::ELF::ARCH::AARCH64: return u"aarch64"_s;
    default:                       return QString();
  }

}

QStringList ElfFile::ImportedLibraries() const {

  QStringList libs;
  if (!binary_) return libs;

  for (const LIEF::ELF::DynamicEntry &entry : binary_->dynamic_entries()) {
    if (entry.tag() == LIEF::ELF::DynamicEntry::TAG::NEEDED) {
      const LIEF::ELF::DynamicEntryLibrary &lib = static_cast<const LIEF::ELF::DynamicEntryLibrary&>(entry);
      libs << QString::fromStdString(lib.name());
    }
  }

  return libs;

}

QStringList ElfFile::ExistingRpaths() const {

  if (!binary_) return QStringList();

  if (const LIEF::ELF::DynamicEntry *entry = binary_->get(LIEF::ELF::DynamicEntry::TAG::RUNPATH)) {
    const LIEF::ELF::DynamicEntryRunPath &runpath = static_cast<const LIEF::ELF::DynamicEntryRunPath&>(*entry);
    return Utilities::SplitPaths(runpath.runpath());
  }
  if (const LIEF::ELF::DynamicEntry *entry = binary_->get(LIEF::ELF::DynamicEntry::TAG::RPATH)) {
    const LIEF::ELF::DynamicEntryRpath &rpath = static_cast<const LIEF::ELF::DynamicEntryRpath&>(*entry);
    return Utilities::SplitPaths(rpath.rpath());
  }

  return QStringList();

}

bool ElfFile::SetRpath(const QStringList &paths) {

  if (!binary_) return false;

  // Deliberately not done via LIEF::ELF::Binary::write() - see the class comment for why.
  QProcess process;
  process.start(u"patchelf"_s, {u"--set-rpath"_s, paths.join(u':'), path_});
  if (!process.waitForFinished(-1)) {
    qWarning() << "ElfFile::SetRpath: patchelf did not finish for" << path_ << ":" << process.errorString();
    return false;
  }
  if (process.exitCode() != 0) {
    qWarning() << "ElfFile::SetRpath: patchelf --set-rpath failed for" << path_ << ":" << QString::fromUtf8(process.readAllStandardError());
    return false;
  }

  return true;

}

QString ElfFile::Interpreter() const {

  if (!binary_ || !binary_->has_interpreter()) return QString();
  return QString::fromStdString(binary_->interpreter());

}

QString ElfFile::Architecture() const {

  if (!binary_) return QString();
  return ArchName(binary_->header().machine_type());

}
