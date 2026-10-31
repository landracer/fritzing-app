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

#ifndef PEUNITS_H
#define PEUNITS_H

#include <QString>

// P7 (docs/design/vector-editor.md section F): the pure unit-conversion core, extracted from
// PEUtils so it is golden-testable in the core+xml suite (PEUtils drags QWidget/Connector/
// graphicsutils in, which the lightweight test harness cannot link). Behavior is byte-for-byte
// what PEUtils did inline; the only change is that the DPI is a parameter (defaulting to the
// Fritzing 90 dpi convention) rather than the heavy GraphicsUtils::SVGDPI constant.
//
// Convention (unchanged): Fritzing stores geometry in SVG *user units* at PeSvgDpi (90) user
// units per CSS inch. A "user unit" is therefore 1/90 of an inch; converting to millimeters is
// * 25.4 / 90 and to inches is / 90. An unknown unit name is a no-op passthrough (identity),
// exactly as PEUtils::convertUnits did for anything that was not "in"/"mm".
namespace PEUnits {

// The Fritzing SVG dpi (user units per inch) these conversions assume.
constexpr double SvgDpi = 90.0;

// Convert a value expressed in SVG user units to the display unit named by 'unit'
// ("in" -> inches, "mm" -> millimeters; anything else -> returned unchanged).
double pxToUnits(double px, const QString & unit, double dpi = SvgDpi);
double unitsToPx(double value, const QString & unit, double dpi = SvgDpi);

// The string form of pxToUnits (QString::number of the converted value).
QString pxToUnitsStr(double px, const QString & unit, double dpi = SvgDpi);

}   // namespace PEUnits

#endif   // PEUNITS_H
