#include "peviewwriter.h"

#include "../utils/textutils.h"

#include <QSaveFile>
#include <QFileInfo>
#include <QDir>

namespace {

// Guarantee the parent directory exists so an atomic rename-onto-target has a place to land. QSaveFile
// creates the temp file next to the target, so the target's dir must already be there.
bool ensureParentDir(const QString & path) {
    QFileInfo info(path);
    if (info.absoluteDir().exists()) return true;
    return QDir().mkpath(info.absolutePath());
}

} // namespace

namespace PEViewWriter {

QString normalizedSvg(const QDomDocument & doc) {
    // toString(4) matches the 4-space indentation the legacy PE round-trip used (removeGorn returns
    // doc.toString(4)); svgNSOnly is the same namespace normalization writeXml applied on every save, so
    // the bytes the new path produces are the convention the rest of the app already expects.
    return TextUtils::svgNSOnly(doc.toString(4));
}

bool writeViewSvg(const QDomDocument & doc, const QString & path) {
    if (path.isEmpty()) return false;
    if (doc.isNull() || doc.documentElement().isNull()) return false;
    if (!ensureParentDir(path)) return false;

    const QString text = normalizedSvg(doc);

    // Atomic: QSaveFile writes to a temp sibling and only renames onto @p path on commit(). A crash or
    // full disk mid-write leaves the previous svg intact instead of a truncated half-file.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(text.toUtf8());
    if (!file.commit()) return false;
    return true;
}

} // namespace PEViewWriter
