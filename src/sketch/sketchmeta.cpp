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
#include "../utils/folderutils.h"
#include "../utils/textutils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QDomDocument>
#include <QTextStream>

const QString SketchMeta::kMetaName  = QStringLiteral("fritzing_sketch_meta.xml");
const QString SketchMeta::kThumbName = QStringLiteral("fritzing_sketch_thumb.png");

QPixmap SketchMeta::thumbnail() const {
    QPixmap pix;
    if (m_thumbWanted && !m_pendingPixmap.isNull())
        return m_pendingPixmap;
    if (!m_thumbPath.isEmpty())
        pix.load(m_thumbPath);
    return pix;
}

void SketchMeta::setThumbnail(const QPixmap & pix) {
    if (pix.isNull()) return;
    m_thumbWanted = true;
    m_pendingPixmap = pix;
}

QString SketchMeta::hashContent(const QByteArray & pcbContent) {
    return QString::fromLatin1(
        QCryptographicHash::hash(pcbContent, QCryptographicHash::Sha256).toHex());
}

QString SketchMeta::hashDirectory(const QString & dir) {
    QDir d(dir);
    if (!d.exists()) return QString();

    // Gerber/excellon-ish artifacts we produced. Sorted for determinism.
    QStringList filters;
    filters << QStringLiteral("*.gbr") << QStringLiteral("*.gtl") << QStringLiteral("*.gbl")
            << QStringLiteral("*.gts") << QStringLiteral("*.gbs") << QStringLiteral("*.gto")
            << QStringLiteral("*.gbo") << QStringLiteral("*.gtp") << QStringLiteral("*.gbp")
            << QStringLiteral("*.gko") << QStringLiteral("*.gm1") << QStringLiteral("*.gm2")
            << QStringLiteral("*.gml") << QStringLiteral("*.drl") << QStringLiteral("*.xln")
            << QStringLiteral("*.txt");
    QStringList names = d.entryList(filters, QDir::Files, QDir::Name | QDir::IgnoreCase);
    names.sort();   // stable order regardless of filesystem order

    QCryptographicHash acc(QCryptographicHash::Sha256);
    for (const QString & name : names) {
        QFile f(d.absoluteFilePath(name));
        if (!f.open(QFile::ReadOnly)) continue;
        acc.addData(name.toUtf8());
        acc.addData("\n");
        acc.addData(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256));
        acc.addData("\n");
        f.close();
    }
    return QString::fromLatin1(acc.result().toHex());
}


bool SketchMeta::writeToBundle(const QString & bundleDir) const {
    QDir dir(bundleDir);
    if (!dir.exists()) return false;

    bool wroteThumb = false;
    if (m_thumbWanted && !m_pendingPixmap.isNull()) {
        wroteThumb = m_pendingPixmap.save(dir.absoluteFilePath(kThumbName), "PNG");
    }

    QDomDocument doc;
    QDomElement root = doc.createElement(QStringLiteral("fritzingSketchMeta"));
    root.setAttribute(QStringLiteral("version"), QStringLiteral("1"));
    doc.appendChild(root);

    QDomElement gd = doc.createElement(QStringLiteral("gerberDir"));
    gd.appendChild(doc.createTextNode(m_gerberDir));
    root.appendChild(gd);

    QDomElement gh = doc.createElement(QStringLiteral("gerberHash"));
    gh.appendChild(doc.createTextNode(m_gerberHash));
    root.appendChild(gh);

    QDomElement tn = doc.createElement(QStringLiteral("thumbnail"));
    tn.appendChild(doc.createTextNode(wroteThumb ? kThumbName : QString()));
    root.appendChild(tn);

    QFile f(dir.absoluteFilePath(kMetaName));
    if (!f.open(QFile::WriteOnly | QFile::Truncate)) {
        DebugDialog::debug(QStringLiteral("SketchMeta: cannot write %1").arg(f.fileName()));
        return false;
    }
    QTextStream ts(&f);
    doc.save(ts, 1);
    f.close();
    return true;
}

SketchMeta SketchMeta::readFromBundle(const QString & bundleDir, bool * okOut) {
    SketchMeta meta;
    QFile f(QDir(bundleDir).absoluteFilePath(kMetaName));
    if (!f.open(QFile::ReadOnly)) {
        if (okOut) *okOut = false;
        return meta;
    }
    QDomDocument doc;
    QString err;
    int line = 0, col = 0;
    if (!doc.setContent(&f, &err, &line, &col)) {
        f.close();
        DebugDialog::debug(QStringLiteral("SketchMeta: parse error %1 (%2:%3)").arg(err).arg(line).arg(col));
        if (okOut) *okOut = false;
        return meta;
    }
    f.close();

    QDomElement root = doc.documentElement();
    if (root.isNull()) { if (okOut) *okOut = false; return meta; }

    meta.m_gerberDir  = root.firstChildElement(QStringLiteral("gerberDir")).text();
    meta.m_gerberHash = root.firstChildElement(QStringLiteral("gerberHash")).text();
    QString thumbFile = root.firstChildElement(QStringLiteral("thumbnail")).text();
    if (!thumbFile.isEmpty())
        meta.m_thumbPath = QDir(bundleDir).absoluteFilePath(thumbFile);

    if (okOut) *okOut = true;
    return meta;
}

bool SketchMeta::bundleHasMeta(const QString & bundleDir) {
    return QFile::exists(QDir(bundleDir).absoluteFilePath(kMetaName));
}

SketchMeta SketchMeta::readFromFzz(const QString & fzzPath, bool * okOut) {
    QDir t(QDir::temp());
    QString name = TextUtils::getRandText();
    if (!t.mkdir(name)) {
        if (okOut) *okOut = false;
        return SketchMeta();
    }
    QString dest = t.absoluteFilePath(name);

    QString error;
    if (!FolderUtils::unzipTo(fzzPath, dest, error)) {
        if (okOut) *okOut = false;
        FolderUtils::rmdir(dest);
        return SketchMeta();
    }

    bool ok = false;
    SketchMeta m = readFromBundle(dest, &ok);
    SketchMeta out;
    if (ok) {
        QPixmap p = m.thumbnail();          // load from the temp-extracted png
        out.m_gerberDir  = m.m_gerberDir;
        out.m_gerberHash = m.m_gerberHash;
        if (!p.isNull()) {
            out.m_thumbWanted = true;
            out.m_pendingPixmap = p;          // keep in-memory so it survives rmdir
        }
    }
    FolderUtils::rmdir(dest);
    if (okOut) *okOut = ok;
    return out;
}

