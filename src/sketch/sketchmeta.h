/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2026 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) at your option any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#ifndef SKETCHMETA_H
#define SKETCHMETA_H

#include <QByteArray>
#include <QPixmap>
#include <QString>

/**
 * @brief Per-sketch "production" metadata that travels inside the .fzz bundle.
 *
 * A sketch's gerber artifacts are production files a fab house consumes. Once
 * generated they can go stale the moment the board changes. To keep users from
 * fabricating stale gerbers we record, at gerber-generation time, inside the
 * .fzz itself:
 *
 *   - thumbnail : a PNG snapshot of the PCB view (shown on the landing page),
 *   - gerberDir : the absolute folder the gerbers were written to,
 *   - gerberHash: a hash over the *board/PCB content* at generation time.
 *
 * The hash is deliberately over the board content only, so unrelated edits
 * (a schematic note, a breadboard tweak) never falsely flag the gerbers stale.
 * The landing page re-hashes the current board and compares to flag
 * No gerbers / Up to date / Out of date.
 *
 * Storage is a single XML file (kMetaName) written into the bundle's temp dir
 * (m_fzzFolder) which is then zipped into the .fzz. readFromBundle() unzips a
 * .fzz to a temp dir, reads the XML, and cleans up - so the landing page can
 * preview a sketch without opening a full MainWindow.
 */
class SketchMeta {
public:
    /** File name of the metadata entry inside the .fzz / sketch temp dir. */
    static const QString kMetaName;      // "fritzing_sketch_meta.xml"
    /** Name of the thumbnail PNG written alongside the metadata. */
    static const QString kThumbName;     // "fritzing_sketch_thumb.png"

    SketchMeta() = default;

    bool isValid() const { return !m_gerberDir.isEmpty(); }

    const QString & gerberDir() const { return m_gerberDir; }
    const QString & gerberHash() const { return m_gerberHash; }
    QPixmap thumbnail() const;

    // Sentinel stored in gerberHash when the board changed after gerber-gen.
    static QString staleSentinel() { return QStringLiteral("STALE"); }
    bool isStale() const { return m_gerberHash == staleSentinel(); }
    bool hasGerbers() const { return !m_gerberDir.isEmpty() && !m_gerberHash.isEmpty(); }

    void setGerberDir(const QString & dir) { m_gerberDir = dir; }
    void setGerberHash(const QString & h) { m_gerberHash = h; }
    void setThumbnail(const QPixmap & pix);

    /** Write the thumbnail + metadata XML into @p bundleDir (the sketch temp dir). */
    bool writeToBundle(const QString & bundleDir) const;

    /** Read metadata (and locate the thumbnail) from an on-disk .fzz, without opening it. */
    static SketchMeta readFromFzz(const QString & fzzPath, bool * okOut = nullptr);

    /** Read metadata already present in an unzipped sketch temp dir. */
    static SketchMeta readFromBundle(const QString & bundleDir, bool * okOut = nullptr);

    /** True if a metadata entry exists in the given unzipped sketch temp dir. */
    static bool bundleHasMeta(const QString & bundleDir);

    /** Hash over board/PCB content bytes. Stable across unrelated edits. */
    static QString hashContent(const QByteArray & pcbContent);

    /**
     * @brief Deterministically hash every gerber-ish file under @p dir.
     *
     * Files are sorted by name; each entry contributes "<relname>\n<sha256>\n".
     * Because it is the output of our own deterministic GerberGenerator, the
     * same board regenerates to byte-identical files, so an identical board
     * yields an identical hash (no false staleness), and any real copper/outline/
     * drill change yields a different one. This is the cross-check that flags
     * "gerbers are out of date - do not use for production".
     */
    static QString hashDirectory(const QString & dir);

protected:
    QString m_gerberDir;
    QString m_gerberHash;
    // absolute path to the (already-extracted) thumbnail png, empty if none.
    QString m_thumbPath;
    // set when a thumbnail pixmap is held in-memory (pending write / re-embedded
    // on read so it survives after the temp extraction dir is removed).
    bool m_thumbWanted = false;
    QPixmap m_pendingPixmap;
};

#endif // SKETCHMETA_H
