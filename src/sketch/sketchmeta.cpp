/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#include "sketchmeta.h"

#include "../debugdialog.h"
#include "../utils/textutils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

const QString SketchMeta::kMarkerName = QStringLiteral(".fritzing_source_md5");

// QSettings group + keys for per-sketch production metadata.
static const QString kSketchesGroup = QStringLiteral("sketches");
static QString md5Key()  { return QStringLiteral("gerberMd5"); }
static QString dirKey()  { return QStringLiteral("gerberDir"); }
static QString dprKey()  { return QStringLiteral("thumbDpr"); }

// Standalone thumbnail storage: a per-sketch PNG under the app data dir, keyed by a
// hash of the absolute sketch path. Independent of the gerber folder so that a board
// preview can exist for sketches that never produced gerbers.
static QString thumbnailsRootDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).absoluteFilePath(QStringLiteral("sketch_thumbnails"));
}

QString SketchMeta::md5OfFile(const QString & path) {
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) return QString();
    const QByteArray digest = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
    f.close();
    return QString::fromLatin1(digest.toHex());
}

QString SketchMeta::defaultGerberDirFor(const QString & sketchPath, const QString & defaultSaveFolder) {
    // Deterministic, always-repeatable: "<defaultSaveFolder>/<sketchBaseName>".
    return QDir(defaultSaveFolder).absoluteFilePath(QFileInfo(sketchPath).completeBaseName());
}

QString SketchMeta::readMarker(const QString & gerberDir) {
    QFile f(QDir(gerberDir).absoluteFilePath(kMarkerName));
    if (!f.open(QFile::ReadOnly)) return QString();
    QTextStream ts(&f);
    const QString v = ts.readAll().trimmed();
    f.close();
    return v;
}

bool SketchMeta::writeMarker(const QString & gerberDir, const QString & md5) {
    QDir().mkpath(gerberDir);
    QSaveFile f(QDir(gerberDir).absoluteFilePath(kMarkerName));
    if (!f.open(QFile::WriteOnly | QFile::Truncate)) return false;
    QTextStream(&f) << md5;
    f.commit();
    return true;
}

QString SketchMeta::thumbnailPathFor(const QString & sketchPath) {
    // Stable, filesystem-safe key from the absolute sketch path.
    const QByteArray digest = QCryptographicHash::hash(sketchPath.toUtf8(), QCryptographicHash::Md5);
    return QDir(thumbnailsRootDir()).absoluteFilePath(QString::fromLatin1(digest.toHex()) + QStringLiteral(".png"));
}

SketchMeta SketchMeta::load(const QString & sketchPath) {
    SketchMeta meta;
    QSettings settings;
    settings.beginGroup(kSketchesGroup);
    // Per-sketch keys live in a sub-group keyed by the absolute sketch path.
    settings.beginGroup(sketchPath);
    meta.m_gerberDir  = settings.value(dirKey()).toString();
    meta.m_sourceMd5  = settings.value(md5Key()).toString();
    meta.m_thumbDpr   = settings.value(dprKey(), 1.0).toReal();
    settings.endGroup();
    settings.endGroup();
    // Thumbnail is independent of gerber metadata: load it from its standalone
    // location whenever the PNG exists, regardless of whether gerbers exist.
    const QString thumbFile = thumbnailPathFor(sketchPath);
    if (QFile::exists(thumbFile)) {
        meta.m_thumbnail.load(thumbFile);
        // Reapply the DPR it was rendered at so HiDPI crispness survives persistence.
        if (!meta.m_thumbnail.isNull()) meta.m_thumbnail.setDevicePixelRatio(meta.m_thumbDpr);
    }
    return meta;
}

void SketchMeta::save(const QString & sketchPath, const SketchMeta & meta) {
    // Persist the thumbnail to its standalone (gerber-independent) location and
    // record gerber metadata (dir + md5) in QSettings keyed by the sketch path.
    const QString thumbPath = thumbnailPathFor(sketchPath);
    if (!meta.m_thumbnail.isNull()) {
        QDir().mkpath(QFileInfo(thumbPath).absolutePath());
        meta.m_thumbnail.save(thumbPath, "PNG");
    }
    QSettings settings;
    settings.beginGroup(kSketchesGroup);
    settings.beginGroup(sketchPath);
    settings.setValue(dirKey(), meta.m_gerberDir);
    settings.setValue(md5Key(), meta.m_sourceMd5);
    settings.setValue(dprKey(), meta.thumbDpr());
    settings.endGroup();
    settings.endGroup();
}

void SketchMeta::remove(const QString & sketchPath) {
    QSettings settings;
    settings.beginGroup(kSketchesGroup);
    settings.beginGroup(sketchPath);
    settings.remove(dirKey());
    settings.remove(md5Key());
    settings.remove(dprKey());
    settings.endGroup();
    settings.endGroup();
    QFile::remove(thumbnailPathFor(sketchPath));
}

bool SketchMeta::isUpToDate(const QString & sketchPath) {
    const SketchMeta meta = load(sketchPath);
    if (!meta.isValid()) return false;
    const QString marker = readMarker(meta.gerberDir());
    if (marker.isEmpty()) return false;
    const QString current = md5OfFile(sketchPath);
    return !current.isEmpty() && current == marker;
}
