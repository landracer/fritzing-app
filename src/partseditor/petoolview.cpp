/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2019 Fritzing

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

#include "peutils.h"
#include "petoolview.h"
#include "pegraphicsitem.h"
#include "../utils/textutils.h"
#include "../utils/graphicsutils.h"
#include "../debugdialog.h"

#include <QHBoxLayout>
#include <QTextStream>
#include <QSplitter>
#include <QPushButton>
#include <QLineEdit>
#include <QPainterPath>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QGridLayout>
#include <QFile>
#include <QApplication>

//////////////////////////////////////

static QPixmap * CheckImage = nullptr;
static QPixmap * NoCheckImage = nullptr;

//////////////////////////////////////

PEDoubleSpinBox::PEDoubleSpinBox(QWidget * parent) : QDoubleSpinBox(parent)
{
}

void PEDoubleSpinBox::stepBy(int steps)
{
	double amount;
	Q_EMIT getSpinAmount(amount);
	setSingleStep(amount);
	QDoubleSpinBox::stepBy(steps);
}

//////////////////////////////////////

// Paint a shape glyph for the tool palette. Everything is drawn with the widget's text colour so the
// glyph reads correctly on both dark and light themes, on a transparent background, at any size. The
// shapes deliberately echo what "Add graphic" actually creates so the icon is a literal preview.
QIcon PEToolView::makeToolIcon(const QString & kind, int size) {
	QPixmap pm(size, size);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);

	// Contrast strategy: never trust a single palette colour, because the palette is often not fully
	// resolved when the tool view is constructed, and a dark/light theme can put a dark glyph on a
	// dark button. Draw a mid grey stroke (reads on both light AND dark button faces) with a subtle
	// contrasting halo underneath, so the glyph is legible whatever the theme.
	const QColor base = qApp->palette().color(QPalette::WindowText);
	const bool lightTheme = base.value() < 128;
	const QColor halo = lightTheme ? QColor(255, 255, 255, 150) : QColor(0, 0, 0, 150);
	const QColor ink  = lightTheme ? QColor(40, 40, 40) : QColor(235, 235, 235);

	// Note the floating-point division: 'size / 16' as ints yielded 1px at size 28 -- a hairline that
	// looked blank at button size. Stroke scales with the icon and is clearly visible now.
	qreal w = qMax(2.0, size / 9.0);

	auto pens = [&](QColor c, qreal width) {
		QPen pen(c);
		pen.setWidthF(width);
		pen.setJoinStyle(Qt::RoundJoin);
		pen.setCapStyle(Qt::RoundCap);
		return pen;
	};
	// Halo pass first (thicker, translucent), then the crisp ink pass; done by re-invoking the shape
	// closure for each colour so every glyph gets the same treatment.
	const qreal m = size * 0.16;                 // margin (was 0.18 -- glyphs read larger now)
	const QRectF box(m, m, size - 2 * m, size - 2 * m);

	auto drawShape = [&](const QColor & c, qreal width) {
		p.setPen(pens(c, width));
		p.setBrush(Qt::NoBrush);

	if (kind == QLatin1String("rect")) {
		p.drawRect(box);
	} else if (kind == QLatin1String("circle")) {
		p.drawEllipse(box);
	} else if (kind == QLatin1String("ellipse")) {
		p.drawEllipse(QRectF(m, size * 0.30, size - 2 * m, size * 0.40));
	} else if (kind == QLatin1String("line")) {
		p.drawLine(QPointF(m, size - m), QPointF(size - m, m));
	} else if (kind == QLatin1String("path")) {
		QPainterPath path;
		path.moveTo(m, size - m);
		path.cubicTo(size * 0.25, m, size * 0.75, size - m, size - m, m);
		p.drawPath(path);
	} else if (kind == QLatin1String("polyline")) {
		QPolygonF poly;
		poly << QPointF(m, size - m) << QPointF(size * 0.4, m) << QPointF(size * 0.62, size * 0.6) << QPointF(size - m, m);
		p.drawPolyline(poly);
	} else if (kind == QLatin1String("polygon")) {
		QPolygonF poly;
		const QPointF c = box.center();
		const qreal r = box.width() / 2.0;
		for (int i = 0; i < 6; ++i) {                       // hexagon
			const qreal a = M_PI / 3.0 * i - M_PI / 2.0;
			poly << QPointF(c.x() + r * qCos(a), c.y() + r * qSin(a));
		}
		p.drawPolygon(poly);
	} else if (kind == QLatin1String("text")) {
		QFont f = p.font();
		f.setPixelSize(int(size * 0.62));
		f.setBold(true);
		p.setFont(f);
		p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("T"));
	} else {
		// Unknown kind: a neutral dot so the button is never blank.
		p.setBrush(c);
		p.drawEllipse(box.center(), size * 0.12, size * 0.12);
	}
	};

	drawShape(halo, w + qMax(1.5, w * 0.9));   // soft outer halo for theme-proofing
	drawShape(ink, w);                          // crisp glyph on top
	p.end();
	return QIcon(pm);
}

PEToolView::PEToolView(QWidget * parent) : QFrame (parent)
{
	m_assignButton = nullptr;

	if (CheckImage == nullptr) CheckImage = new QPixmap(":/resources/images/icons/check.png");
	if (NoCheckImage == nullptr) NoCheckImage = new QPixmap(":/resources/images/icons/nocheck.png");

	this->setObjectName("PEToolView");
	/*

	QFile styleSheet(":/resources/styles/newpartseditor.qss");
	if (!styleSheet.open(QIODevice::ReadOnly)) {
	    DebugDialog::debug("Unable to open :/resources/styles/newpartseditor.qss");
	} else {
	this->setStyleSheet(styleSheet.readAll());
	}
	*/
	m_pegi = nullptr;

	auto * mainLayout = new QVBoxLayout;
	mainLayout -> setObjectName("connectorFrame");
	auto * splitter = new QSplitter(Qt::Vertical);
	mainLayout->addWidget(splitter);

	auto * connectorsFrame = new QFrame;

	auto * connectorsLayout = new QVBoxLayout;

	auto * label = new QLabel(tr("Connector List (a checkmark means the graphic was selected)"));
	connectorsLayout->addWidget(label);


	m_connectorListWidget = new QTreeWidget();
	m_connectorListWidget->setColumnCount(2);
	connect(m_connectorListWidget, SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)), this, SLOT(switchConnector(QTreeWidgetItem *, QTreeWidgetItem *)));
	connectorsLayout->addWidget(m_connectorListWidget);

	m_busModeBox = new QCheckBox(tr("Set Internal Connections"));
	m_busModeBox->setChecked(false);
	m_busModeBox->setToolTip(tr("Set this checkbox to edit internal connections by drawing wires"));
	connect(m_busModeBox, SIGNAL(clicked(bool)), this, SLOT(busModeChangedSlot(bool)));
	connectorsLayout->addWidget(m_busModeBox);

	connectorsFrame->setLayout(connectorsLayout);
	splitter->addWidget(connectorsFrame);

	m_connectorInfoGroupBox = new QGroupBox;
	m_connectorInfoLayout = new QVBoxLayout;

	m_connectorInfoWidget = new QFrame;             // a placeholder for PEUtils::connectorForm
	m_connectorInfoLayout->addWidget(m_connectorInfoWidget);

	m_terminalPointGroupBox = new QGroupBox("Terminal point");
	m_terminalPointGroupBox->setToolTip(tr("Controls for setting the terminal point for a connector. The terminal point is where a wire will attach to the connector. You can also drag the crosshair of the current connector"));
	auto * anchorGroupLayout = new QVBoxLayout;

	auto * posRadioFrame = new QFrame;
	auto * posRadioLayout = new QHBoxLayout;

	QList<QString> positionNames;
	positionNames << "Center"  << "W" << "N" << "S" << "E";
	QList<QString> trPositionNames;
	trPositionNames << tr("Center") << tr("W") << tr("N") << tr("S") << tr("E");
	QList<QString> trLongNames;
	trLongNames << tr("center") << tr("west") << tr("north") << tr("south") << tr("east");
	for (int i = 0; i < positionNames.count(); i++) {
		auto * button = new QPushButton(trPositionNames.at(i));
		button->setProperty("how", positionNames.at(i));
		button->setToolTip(tr("Sets the connector's terminal point to %1.").arg(trLongNames.at(i)));
		connect(button, SIGNAL(clicked()), this, SLOT(buttonChangeTerminalPoint()));
		posRadioLayout->addWidget(button);
		m_buttons.append(button);
	}

	posRadioLayout->addSpacerItem(new QSpacerItem(1, 1, QSizePolicy::Expanding));

	posRadioFrame->setLayout(posRadioLayout);
	anchorGroupLayout->addWidget(posRadioFrame);

	auto * posNumberFrame = new QFrame;
	auto * posNumberLayout = new QHBoxLayout;

	label = new QLabel("x");
	posNumberLayout->addWidget(label);

	m_terminalPointX = new PEDoubleSpinBox;
	m_terminalPointX->setDecimals(4);
	m_terminalPointX->setToolTip(tr("Modifies the x-coordinate of the terminal point"));
	posNumberLayout->addWidget(m_terminalPointX);
	connect(m_terminalPointX, SIGNAL(getSpinAmount(double &)), this, SLOT(getSpinAmountSlot(double &)), Qt::DirectConnection);
	connect(m_terminalPointX, SIGNAL(valueChanged(double)), this, SLOT(terminalPointEntry()));

	posNumberLayout->addSpacing(PEUtils::Spacing);

	label = new QLabel("y");
	posNumberLayout->addWidget(label);

	m_terminalPointY = new PEDoubleSpinBox;
	m_terminalPointY->setDecimals(4);
	m_terminalPointY->setToolTip(tr("Modifies the y-coordinate of the terminal point"));
	posNumberLayout->addWidget(m_terminalPointY);
	connect(m_terminalPointY, SIGNAL(getSpinAmount(double &)), this, SLOT(getSpinAmountSlot(double &)), Qt::DirectConnection);
	connect(m_terminalPointY, SIGNAL(valueChanged(double)), this, SLOT(terminalPointEntry()));

	posNumberLayout->addSpacing(PEUtils::Spacing);

	m_units = new QLabel();
	posNumberLayout->addWidget(m_units);
	posNumberLayout->addSpacing(PEUtils::Spacing);

	m_terminalPointDragState = new QLabel(tr("Dragging disabled"));
	posNumberLayout->addWidget(m_terminalPointDragState);

	posNumberLayout->addSpacerItem(new QSpacerItem(1, 1, QSizePolicy::Expanding));

	posNumberFrame->setLayout(posNumberLayout);
	anchorGroupLayout->addWidget(posNumberFrame);

	m_terminalPointGroupBox->setLayout(anchorGroupLayout);
	m_connectorInfoLayout->addWidget(m_terminalPointGroupBox);

	m_connectorInfoLayout->addSpacerItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));

	m_connectorInfoGroupBox->setLayout(m_connectorInfoLayout);

	splitter->addWidget(m_connectorInfoGroupBox);

	// P3 create-tool, restyled: a graphical palette of shape tools, matching the visual language of
	// every other editor's tool sidebar (icon per tool, hover tooltip). The glyphs are painted with
	// QPainter into a QPixmap rather than shipped as image assets -- no new resource/licensing surface,
	// they stay crisp at any DPI, and they recolour with the active palette so they read on dark and
	// light themes alike.
	auto * graphicToolsGroup = new QGroupBox(tr("Graphics"));
	auto * graphicToolsLayout = new QVBoxLayout;
	auto * graphicGrid = new QGridLayout;
	graphicGrid->setSpacing(6);
	graphicGrid->setContentsMargins(4, 4, 4, 4);

	struct ToolSpec { const char * kind; const char * tip; };
	const QList<ToolSpec> tools {
		{ "rect",     QT_TRANSLATE_NOOP("PEToolView", "Rectangle") },
		{ "circle",   QT_TRANSLATE_NOOP("PEToolView", "Circle") },
		{ "ellipse",  QT_TRANSLATE_NOOP("PEToolView", "Ellipse") },
		{ "line",     QT_TRANSLATE_NOOP("PEToolView", "Line") },
		{ "path",     QT_TRANSLATE_NOOP("PEToolView", "Path") },
		{ "polyline", QT_TRANSLATE_NOOP("PEToolView", "Polyline") },
		{ "polygon",  QT_TRANSLATE_NOOP("PEToolView", "Polygon") },
		{ "text",     QT_TRANSLATE_NOOP("PEToolView", "Text") },
	};
	int ti = 0;
	for (const ToolSpec & t : tools) {
		auto * button = new QPushButton;
		button->setIcon(PEToolView::makeToolIcon(QString::fromLatin1(t.kind)));
		button->setIconSize(QSize(28, 28));
		button->setToolTip(tr(t.tip));
		button->setAccessibleName(tr(t.tip));
		button->setFixedSize(48, 48);
		button->setProperty("kind", QString::fromLatin1(t.kind));
		connect(button, SIGNAL(clicked()), this, SLOT(createPrimitiveSlot()));
		graphicGrid->addWidget(button, ti / 4, ti % 4);
		++ti;
	}
	graphicToolsLayout->addLayout(graphicGrid);
	// P4: author a connector bound to the currently-selected graphic element.
	auto * addConnButton = new QPushButton(tr("Add connector from selected graphic"));
	addConnButton->setToolTip(tr("Create a connector that attaches to the currently selected graphic."));
	connect(addConnButton, SIGNAL(clicked()), this, SLOT(addConnectorSlot()));
	graphicToolsLayout->addWidget(addConnButton);
	graphicToolsGroup->setLayout(graphicToolsLayout);
	mainLayout->addWidget(graphicToolsGroup);

	//this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	//this->setWidget(splitter);

	this->setLayout(mainLayout);

	m_connectorListWidget->resize(m_connectorListWidget->width(), 0);

	enableConnectorChanges(false, false, false, false);

}

PEToolView::~PEToolView()
{
}

void PEToolView::enableConnectorChanges(bool enableTerminalPointDrag, bool enableTerminalPointControls, bool enableInfo, bool enableAssign)
{
	if (m_assignButton != nullptr) {
		m_assignButton->setEnabled(enableAssign);
	}
	if (m_terminalPointGroupBox != nullptr) {
		m_terminalPointGroupBox->setEnabled(enableTerminalPointControls);
	}
	if (m_connectorInfoWidget != nullptr) {
		m_connectorInfoWidget->setEnabled(enableInfo);
	}

	if (m_terminalPointDragState != nullptr) {
		if (enableTerminalPointDrag) {
			m_terminalPointDragState->setText(tr("<font color='black'>Dragging enabled</font>"));
			m_terminalPointDragState->setEnabled(true);
		}
		else {
			m_terminalPointDragState->setText(tr("<font color='gray'>Dragging disabled</font>"));
			m_terminalPointDragState->setEnabled(false);
		}
	}
}

void PEToolView::initConnectors(QList<QDomElement> * connectorList) {
	m_connectorListWidget->blockSignals(true);

	m_connectorListWidget->clear();  // deletes QTreeWidgetItems
	m_connectorList = connectorList;

	for (int ix = 0; ix < connectorList->count(); ix++) {
		QDomElement connector = connectorList->at(ix);
		auto *item = new QTreeWidgetItem;
		item->setData(0, Qt::DisplayRole, connector.attribute("name"));
		item->setData(0, Qt::UserRole, ix);
		item->setData(1, Qt::UserRole, ix);
		item->setData(0, Qt::DecorationRole, *NoCheckImage);
		m_connectorListWidget->addTopLevelItem(item);
		auto * label = new QLabel("");
		//label->setPixmap(*NoCheckImage);
		m_connectorListWidget->setItemWidget(item, 1, label);
	}

	if (connectorList->count() > 0) {
		m_connectorListWidget->setCurrentItem(m_connectorListWidget->topLevelItem(0));
		switchConnector(m_connectorListWidget->currentItem(), nullptr);
	}

	m_connectorListWidget->blockSignals(false);
}

void PEToolView::showAssignedConnectors(const QDomDocument * svgDoc, ViewLayer::ViewID viewID) {
	QDomElement svgRoot = svgDoc->documentElement();

	for (int i = 0; i < m_connectorListWidget->topLevelItemCount(); i++) {
		QTreeWidgetItem * item = m_connectorListWidget->topLevelItem(i);
		int index = item->data(0, Qt::UserRole).toInt();
		QDomElement connector = m_connectorList->at(index);
		QString svgID, terminalID;
		bool ok = ViewLayer::getConnectorSvgIDs(connector, viewID, svgID, terminalID);
		if (!ok) {
			continue;
		}

		QDomElement element = TextUtils::findElementWithAttribute(svgRoot, "id", svgID);
		if (element.isNull()) {
			item->setData(0, Qt::DecorationRole, *NoCheckImage);
		}
		else {
			item->setData(0, Qt::DecorationRole, *CheckImage);
		}
	}

	hideConnectorListStuff();
	m_connectorListWidget->repaint();
}

void PEToolView::switchConnector(QTreeWidgetItem * current, QTreeWidgetItem * previous) {
	Q_UNUSED(previous);

	QWidget * widget = QApplication::focusWidget();
	if (widget != nullptr) {
		QList<QWidget *> children = m_connectorInfoWidget->findChildren<QWidget *>();
		if (children.contains(widget)) {
			widget->blockSignals(true);
		}
	}

	if (m_connectorInfoWidget != nullptr) {
		delete m_connectorInfoWidget;
		m_connectorInfoWidget = nullptr;
	}

	if (current == nullptr) return;

	int index = current->data(0, Qt::UserRole).toInt();
	QDomElement element = m_connectorList->at(index);

	int pos = 99999;
	for (int ix = 0; ix < m_connectorInfoLayout->count(); ix++) {
		QLayoutItem * item = m_connectorInfoLayout->itemAt(ix);
		if (item->widget() == m_terminalPointGroupBox) {
			pos = ix;
			break;
		}
	}

	m_connectorInfoWidget = PEUtils::makeConnectorForm(element, index, this, false);
	m_connectorInfoLayout->insertWidget(pos, m_connectorInfoWidget);
	m_connectorInfoGroupBox->setTitle(tr("Connector %1").arg(element.attribute("name")));
	m_units->setText(QString("(%1)").arg(PEUtils::Units));

	hideConnectorListStuff();

	Q_EMIT switchedConnector(index);
}

void PEToolView::busModeChangedSlot(bool state)
{
	Q_EMIT busModeChanged(state);
}

void PEToolView::nameEntry() {
	changeConnector();
}

void PEToolView::typeEntry() {
	changeConnector();
}

void PEToolView::descriptionEntry() {
	changeConnector();
}

void PEToolView::changeConnector() {
	QTreeWidgetItem * item = m_connectorListWidget->currentItem();
	if (item == nullptr) return;

	int index = item->data(0, Qt::UserRole).toInt();

	ConnectorMetadata cmd;
	if (!PEUtils::fillInMetadata(index, this, cmd)) return;

	Q_EMIT connectorMetadataChanged(&cmd);
}

void PEToolView::setCurrentConnector(const QDomElement & newConnector) {
	for (int ix = 0; ix < m_connectorListWidget->topLevelItemCount(); ix++) {
		QTreeWidgetItem * item = m_connectorListWidget->topLevelItem(ix);
		int index = item->data(0, Qt::UserRole).toInt();
		QDomElement connector = m_connectorList->at(index);
		if (connector.attribute("id") == newConnector.attribute("id")) {
			m_connectorListWidget->setCurrentItem(item);
			hideConnectorListStuff();
			return;
		}
	}
}

int PEToolView::currentConnectorIndex() {
	QTreeWidgetItem * item = m_connectorListWidget->currentItem();
	if (item == nullptr) return -1;

	int index = item->data(0, Qt::UserRole).toInt();
	return index;
}

void PEToolView::setTerminalPointCoords(QPointF p) {
	m_terminalPointX->blockSignals(true);
	m_terminalPointY->blockSignals(true);
	m_terminalPointX->setValue(PEUtils::convertUnits(p.x()));
	m_terminalPointY->setValue(PEUtils::convertUnits(p.y()));
	m_terminalPointX->blockSignals(false);
	m_terminalPointY->blockSignals(false);
}

void PEToolView::setTerminalPointLimits(QSizeF sz) {
	m_terminalPointX->setRange(0, sz.width());
	m_terminalPointY->setRange(0, sz.height());
}

void PEToolView::buttonChangeTerminalPoint() {
	QString how = sender()->property("how").toString();
	Q_EMIT terminalPointChanged(how);
}

void PEToolView::terminalPointEntry()
{
	if (sender() == m_terminalPointX) {
		Q_EMIT terminalPointChanged("x", PEUtils::unconvertUnits(m_terminalPointX->value()));
	}
	else if (sender() == m_terminalPointY) {
		Q_EMIT terminalPointChanged("y", PEUtils::unconvertUnits(m_terminalPointY->value()));
	}
}

void PEToolView::getSpinAmountSlot(double & d) {
	Q_EMIT getSpinAmount(d);
}


void PEToolView::removeConnector() {
	QTreeWidgetItem * item = m_connectorListWidget->currentItem();
	if (item == nullptr) return;

	int index = item->data(0, Qt::UserRole).toInt();
	QDomElement element = m_connectorList->at(index);
	Q_EMIT removedConnector(element);

}

void PEToolView::createPrimitiveSlot() {
	QObject * s = sender();
	if (s == nullptr) return;
	QString kind = s->property("kind").toString();
	if (kind.isEmpty()) return;
	Q_EMIT createPrimitiveRequested(kind);
}

void PEToolView::addConnectorSlot() {
	Q_EMIT addConnectorRequested();
}

void PEToolView::setChildrenVisible(bool vis)
{
	Q_FOREACH (QWidget * widget, findChildren<QWidget *>()) {
		widget->setVisible(vis);
	}
}

void PEToolView::pickModeChangedSlot() {
	Q_EMIT pickModeChanged(true);
}

void PEToolView::hideConnectorListStuff() {
	m_connectorListWidget->setHeaderHidden(true);
	QTreeWidgetItem * current = m_connectorListWidget->currentItem();

	for (int i = 0; i < m_connectorListWidget->topLevelItemCount(); i++) {
		QTreeWidgetItem * item = m_connectorListWidget->topLevelItem(i);
		QWidget * widget = m_connectorListWidget->itemWidget(item, 1);
		if (qobject_cast<QPushButton *>(widget) != nullptr) {
			if (item == current) ;   // button is already there
			else {
				// remove the button and add the label
				m_connectorListWidget->removeItemWidget(item, 1);
				auto * label = new QLabel();
				//label->setPixmap(*NoCheckImage);
				m_connectorListWidget->setItemWidget(item, 1, label);
			}
		}
		else {
			if (item != current) ;		// label is already there
			else {
				m_assignButton = new QPushButton(tr("Select graphic"));
				m_assignButton->setMaximumWidth(150);
				connect(m_assignButton, SIGNAL(clicked()), this, SLOT(pickModeChangedSlot()), Qt::DirectConnection);
				m_assignButton->setToolTip(tr("Use the cursor location and mouse wheel to navigate to the SVG element which you want to assign to the current connector, then mouse down to select it."));
				m_connectorListWidget->setItemWidget(item, 1, m_assignButton);
			}
		}
	}
}

void PEToolView::setCurrentConnector(int ix) {
	m_connectorListWidget->setCurrentItem(m_connectorListWidget->topLevelItem(ix));
	switchConnector(m_connectorListWidget->currentItem(), nullptr);
}
