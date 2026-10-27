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

#ifndef SKETCHMETA_H
#define SKETCHMETA_H

#include <QPixmap>
#include <QString>

/**
 * @brief Per-sketch "production" bookkeeping for gerber artifacts.
 *
 * Design (final): we never rewrite the user's .fzz (no forced save, no undo-stack
 * dirtying). Instead, at gerber-export time we:
 *   - choose an output folder (a folder picker, defaulting to a deterministic
 *     "<defaultSaveFolder>/<sketchBaseName>/"),
 *   - write the gerber files there,
 *   - write a small sidecar marker into that gerber folder recording the md5 of
 *     the source .fzz at generation time, and
 *   - record (in QSettings keyed by the absolute sketch path) the gerber folder
 *     and the captured thumbnail.
 *
 * Staleness is then a trivial, fast, dependency-free check: recompute md5 of the
 * current .fzz and compare to the stored value. Equal -> up to date; different ->
 * the board changed since the gerbers were produced, so they must not be used for
 * fabrication. The check runs on open/save/landing-select.
 */
class SketchMeta {
public:
    // Marker file written into the gerber output folder at export time.
    static const QString kMarkerName;   // ".fritzing_source_md5"

    SketchMeta() = default;

    bool isValid() const { return !m_gerberDir.isEmpty(); }

    const QString & gerberDir() const { return m_gerberDir; }
    const QString & sourceMd5() const { return m_sourceMd5; }
    QPixmap thumbnail() const { return m_thumbnail; }

    void setGerberDir(const QString & dir) { m_gerberDir = dir; }
    void setSourceMd5(const QString & md5)  { m_sourceMd5 = md5; }
    void setThumbnail(const QPixmap & pix)  { m_thumbnail = pix; }

    // --- persistence ---

    // Load {gerberDir, thumbnail} recorded for the sketch at @p sketchPath.
    static SketchMeta load(const QString & sketchPath);
    // Persist {gerberDir, thumbnail} for the sketch at @p sketchPath.
    static void save(const QString & sketchPath, const SketchMeta & meta);
    // Remove any recorded production metadata for @p sketchPath.
    static void remove(const QString & sketchPath);

    // Read the source-md5 marker stored inside a gerber folder (empty if none).
    static QString readMarker(const QString & gerberDir);
    // Write @p md5 into @p gerberDir's marker file.
    static bool writeMarker(const QString & gerberDir, const QString & md5);

    // --- helpers ---

    // md5 of a file's bytes (empty on error).
    static QString md5OfFile(const QString & path);
    // Deterministic default gerber folder for a sketch: "<defaultSaveFolder>/<base>".
    static QString defaultGerberDirFor(const QString & sketchPath, const QString & defaultSaveFolder);
    // True if the current .fzz md5 matches the marker inside its recorded gerber dir.
    static bool isUpToDate(const QString & sketchPath);

protected:
    QString m_gerberDir;
    QString m_sourceMd5;
    QPixmap m_thumbnail;
};

#endif // SKETCHMETA_H
