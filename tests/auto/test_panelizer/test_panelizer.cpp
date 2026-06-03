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

#include <QtTest/QtTest>

#include "autoroute/panelpresets.h"

class TestPanelizer : public QObject
{
    Q_OBJECT

private slots:
    // The preset table is the single source of truth shared by the
    // panel-size page and the auto-fit walker, so its invariants are
    // what actually keep panelization correct. These assert them.

    void presetListIsPopulated() {
        QVERIFY(!PanelPresets::list().isEmpty());
    }

    void presetsHavePositiveDimsAndLabels() {
        const QVector<PanelPresets::Preset> presets = PanelPresets::list();
        for (const PanelPresets::Preset & p : presets) {
            QVERIFY2(p.widthMm  > 0.0, "preset width must be positive");
            QVERIFY2(p.heightMm > 0.0, "preset height must be positive");
            QVERIFY2(!p.label.trimmed().isEmpty(), "preset label must be non-empty");
        }
    }

    void presetsAreSmallestAreaFirst() {
        // The auto-fit walker stops at the first preset that fits, so the
        // list MUST be non-decreasing by area or it would pick an
        // oversized panel. This guards that contract.
        const QVector<PanelPresets::Preset> presets = PanelPresets::list();
        double previousArea = -1.0;
        for (const PanelPresets::Preset & p : presets) {
            const double area = p.widthMm * p.heightMm;
            QVERIFY2(area >= previousArea, "presets must be ordered smallest-area first");
            previousArea = area;
        }
    }

    void autoFitPicksSmallestFittingPreset() {
        // Replicate the walker's decision for a 40 x 40 mm board: the
        // smallest preset whose dimensions accommodate it must win.
        const QVector<PanelPresets::Preset> presets = PanelPresets::list();
        const double boardW = 40.0, boardH = 40.0;
        const PanelPresets::Preset * chosen = nullptr;
        for (const PanelPresets::Preset & p : presets) {
            if (p.widthMm >= boardW && p.heightMm >= boardH) { chosen = &p; break; }
        }
        QVERIFY2(chosen != nullptr, "a board smaller than the largest preset must fit");
        // 50 x 50 mm is the smallest entry and accommodates 40 x 40.
        QCOMPARE(chosen->widthMm, 50.0);
        QCOMPARE(chosen->heightMm, 50.0);
    }
};

QTEST_MAIN(TestPanelizer)
#include "test_panelizer.moc"
