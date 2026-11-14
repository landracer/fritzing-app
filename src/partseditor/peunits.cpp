/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2026 Fritzing

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

#include "peunits.h"

#include <QSettings>

namespace {
// Fixed readout precision: enough to be useful on a 90 dpi board graphic, low enough that the
// golden goldens are exact strings (QString::number with 'g'/4 is fully deterministic).
constexpr int kReadoutPrecision = 4;
}   // namespace

QStringList PEUnits::availableUnits()
{
    return QStringList() << QString::fromLatin1(kUnitPx)
                         << QString::fromLatin1(kUnitIn)
                         << QString::fromLatin1(kUnitMm)
                         << QString::fromLatin1(kUnitMil);
}

bool PEUnits::isKnownUnit(const QString & unit)
{
    return availableUnits().contains(unit, Qt::CaseSensitive);
}

QString PEUnits::readPreference()
{
    QSettings settings;
    const QString stored = settings.value(QLatin1String(kPreferenceKey),
                                         QString::fromLatin1(kDefaultUnit)).toString();
    // An unknown/corrupt stored value is not silently honoured (that would make the readout lie by
    // falling through to the identity passthrough); it falls back to the documented default.
    return isKnownUnit(stored) ? stored : QString::fromLatin1(kDefaultUnit);
}

void PEUnits::writePreference(const QString & unit)
{
    if (!isKnownUnit(unit)) return;   // never poison the store with a name nothing can format
    QSettings settings;
    settings.setValue(QLatin1String(kPreferenceKey), unit);
    settings.sync();
}

double PEUnits::pxToUnits(double px, const QString & unit, double dpi)
{
    if (unit.compare(QLatin1String(kUnitIn)) == 0) {
        return px / dpi;
    }
    else if (unit.compare(QLatin1String(kUnitMm)) == 0) {
        return px * 25.4 / dpi;
    }
    else if (unit.compare(QLatin1String(kUnitMil)) == 0) {
        return px * 1000.0 / dpi;
    }

    return px;   // px (identity) or unknown unit: passthrough (as the old PEUtils::convertUnits did)
}

double PEUnits::unitsToPx(double value, const QString & unit, double dpi)
{
    if (unit.compare(QLatin1String(kUnitIn)) == 0) {
        return value * dpi;
    }
    else if (unit.compare(QLatin1String(kUnitMm)) == 0) {
        return value * dpi / 25.4;
    }
    else if (unit.compare(QLatin1String(kUnitMil)) == 0) {
        return value * dpi / 1000.0;
    }

    return value;   // px (identity) or unknown unit: passthrough
}

QString PEUnits::pxToUnitsStr(double px, const QString & unit, double dpi)
{
    return QString::number(pxToUnits(px, unit, dpi), 'g', kReadoutPrecision);
}

QRectF PEUnits::convertBox(const QRectF & pxBox, const QString & unit, double dpi)
{
    if (!pxBox.isValid()) return QRectF();
    // The input contract (enforced by the validity gate above) is a normalized, non-degenerate box --
    // exactly what PEPrimGeom::localBounds returns (R1 fixed it to min/max over the points). Width and
    // height are converted as the difference of the converted edges, so the reported size never depends on
    // the absolute origin the svg happens to carry. A non-normalized or degenerate box is refused (empty
    // result = "nothing to report"), never silently re-normalized into a fabricated box.
    const QRectF box = pxBox;
    // Width/height are converted as the (positive) difference of the converted edges, so the box
    // never depends on the absolute origin the svg happens to carry and cannot shrink by rounding.
    const double l = pxToUnits(box.left(),   unit, dpi);
    const double t = pxToUnits(box.top(),    unit, dpi);
    const double r = pxToUnits(box.right(),  unit, dpi);
    const double b = pxToUnits(box.bottom(), unit, dpi);
    return QRectF(QPointF(l, t), QPointF(r, b)).normalized();
}

QString PEUnits::formatBox(const QRectF & pxBox, const QString & unit, double dpi)
{
    if (!pxBox.isValid()) return QString();
    const QRectF u = convertBox(pxBox, unit, dpi);
    return QStringLiteral("(%1, %2) (%3, %4) %5")
        .arg(u.x(),      0, 'g', kReadoutPrecision)
        .arg(u.y(),      0, 'g', kReadoutPrecision)
        .arg(u.width(),  0, 'g', kReadoutPrecision)
        .arg(u.height(), 0, 'g', kReadoutPrecision)
        .arg(unit);
}

QString PEUnits::formatPoint(const QPointF & pxPoint, const QString & unit, double dpi)
{
    return QStringLiteral("(%1, %2) %3")
        .arg(pxToUnits(pxPoint.x(), unit, dpi), 0, 'g', kReadoutPrecision)
        .arg(pxToUnits(pxPoint.y(), unit, dpi), 0, 'g', kReadoutPrecision)
        .arg(unit);
}

