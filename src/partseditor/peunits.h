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
#include <QStringList>
#include <QPointF>
#include <QRectF>

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

// R2 (docs/design/pe-editor-roadmap.md): the readout unit is a real preference, and every consumer
// (the editor's selection/cursor readout, and later the R3 grid snap) must agree on the SAME small
// set of names. "px" is the identity unit -- the svg's own user units -- so a readout can honestly
// say "this is raw user units" instead of silently picking an inch-based scale. An unknown name is
// still an identity passthrough below (never a silent wrong scale).
constexpr const char * kUnitPx  = "px";
constexpr const char * kUnitIn  = "in";
constexpr const char * kUnitMm  = "mm";
constexpr const char * kUnitMil = "mil";   // thousandths of an inch (the PCB convention)

// The QSettings key that stores the user's chosen readout unit. Pinned explicitly in tests.
inline constexpr const char * kPreferenceKey = "partsEditorDisplayUnits";

// The unit a freshly created editor starts in when no preference has been written yet. Per the
// roadmap: millimeters.
inline constexpr const char * kDefaultUnit = kUnitMm;

// The units the selector offers, in menu order.
QStringList availableUnits();
// True when 'unit' is one of the names above (case-sensitive, as the stored value is).
bool isKnownUnit(const QString & unit);

// Read the preference, mapped through isKnownUnit so a corrupt/legacy value falls back to
// kDefaultUnit rather than silently disabling the readout.
QString readPreference();
// Persist the preference (no-op for an unknown unit name, so a bad caller cannot poison the store).
void writePreference(const QString & unit);

// Convert a value expressed in SVG user units to the display unit named by 'unit'
// ("in" -> inches, "mm" -> millimeters, "mil" -> thousandths of an inch, "px" -> unchanged;
//  anything else -> returned unchanged).
double pxToUnits(double px, const QString & unit, double dpi = SvgDpi);
double unitsToPx(double value, const QString & unit, double dpi = SvgDpi);

// The string form of pxToUnits (QString::number of the converted value).
QString pxToUnitsStr(double px, const QString & unit, double dpi = SvgDpi);

// R2: convert a px box into the display unit (componentwise). Width/height are computed as the
// (positive) difference of the CONVERTED edges, so the reported size never depends on the absolute
// origin the svg carries. An invalid/degenerate input box (see QRectF::isValid) yields an empty rect
// -- "nothing to report", never a fabricated box.
QRectF convertBox(const QRectF & pxBox, const QString & unit, double dpi = SvgDpi);

// The readout strings. Fixed precision ('g', 4) so the goldens are exact strings and a unit switch
// re-formats identically. Both carry the unit name so the readout never leaves the user guessing:
//   formatBox   -> "(x, y) (w, h) <unit>"  (position then size)
//   formatPoint -> "(x, y) <unit>"
// formatBox returns an empty string when the box is not valid (nothing to report).
QString formatBox(const QRectF & pxBox, const QString & unit, double dpi = SvgDpi);
QString formatPoint(const QPointF & pxPoint, const QString & unit, double dpi = SvgDpi);

}   // namespace PEUnits

#endif   // PEUNITS_H
