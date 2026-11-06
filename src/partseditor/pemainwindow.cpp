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

/* TO DO ******************************************


    ///////////////////////////////// first release ///////////////////////////////

    clicking set bus mode fucks up anchor points

    terminal point area is not highlighting properly

    ////////////////////// second release /////////////////////////////////

		icon editor?

        need simplified infoview

        change color of breadboard text item

        for svg import check for flaws:
            multiple connector or terminal ids
			trash any other matching id

		loading tht into smd or v.v.
			can't seem to assign connectors after adding connectors

		why isn't swapping available when a family has new parts with multiple variant values?

		test button with export etchable to make sure the part is right?
			export etchable is disabled without a board

		align to grid when moving?

		restore editable pin labels functionality
			requires storing labels in the part rather than in the sketch

		hidden connectors
			allow multiple hits to the same place and add copies of the svg element if necessary

		restore parts bin

        buses
			secondary representation (list view)
				display during bus mode instead of connector list
				allow to delete bus, delete nodemember, add nodemember using right-click for now

		use the actual svg shape instead of rectangles
			construct a new svg with the width and height of the bounding box of the element
			translate the element by the current top-left of the element
			set the fill color and kill the stroke width

		properties and tags entries allow duplicates

		delete temp files after crash

        show in OS button only shows folder and not file in linux

        smd vs. tht
            after it's all over remove copper0 if part is all smd
            split copper0 and copper1
            allow mixed tht/smd parts in fritzing but for now disable flipsmd
            allow smd with holes to flip?

        import
            kicad sch files?
				kicad schematic does not match our schematic

        on svg import detect all connector IDs
            if any are invisible, tell user this is obsolete

        sort connector list alphabetically or numerically?

        full svg outline view

        bendable legs
			same as terminalID, but must be a line
			use a radio buttons to distinguish?

        set flippable

        connector duplicate op

        add layers:  put everything in silkscreen, then give copper1, copper0 checkbox
            what about breadboardbreadboard or other odd layers?
            if you click something as a connector, automatically move it into copper
                how to distinguish between both and top--default to both, let user set "pad"
			setting layer for top level group sets all children?

        swap connector metadata op

        delete op

        move connectors with arrow keys, or typed coordinates
	    drag and drop later

        import
            eagle lbr
            eagle brd

        for schematic view
            offer lines-or-pins, rects, and a selection of standard schematic icons in the parts bin

        for breadboard view
            import
            generate ICs, dips, sips, breakouts

        for pcb view
            pads, pins (circle, rect, oblong), holes
            lines and curves?
            import silkscreen

        hybrids

        flip and rotate images w/in the view?

        taxonomy entry like tag entry?

        new schematic layout specs

        give users a family popup with all family names
            ditto for other properties

        matrix problem with move and duplicate (i.e. if element inherits a matrix from far above)
            even a problem when inserting hole, pad, or pin
            eliminate internal transforms, then insert inverted matrix, then use untransformed coords based on viewbox


***************************************************/

#include "pemainwindow.h"
#include "pemetadataview.h"
#include "peconnectorsview.h"
#include "pecommands.h"
#include "pehistoryentrydialog.h"
#include "petoolview.h"
#include "pesvgview.h"
#include <QtSvgWidgets/QGraphicsSvgItem>
#include "pegraphicsitem.h"
#include "pedocument.h"
#include "peprimitive.h"
#include "petransform.h"   // M1: shapeFromElement -> real bounds for path/text/polyline/polygon

#include <QPainterPath>
#include "peconnector.h"
#include "peblankpart.h"
#include "pesvgimport.h"
#include "kicadmoduledialog.h"
#include "../debugdialog.h"
#include "../sketch/breadboardsketchwidget.h"
#include "../sketch/schematicsketchwidget.h"
#include "../sketch/pcbsketchwidget.h"
#include "../mainwindow/sketchareawidget.h"
#include "../referencemodel/referencemodel.h"
#include "../utils/graphicsutils.h"
#include "../svg/svgfilesplitter.h"
#include "../utils/textutils.h"
#include "../utils/folderutils.h"
#include "../utils/s2s.h"
#include "../mainwindow/fdockwidget.h"
#include "../fsvgrenderer.h"
#include "../partsbinpalette/binmanager/binmanager.h"
#include "../svg/gedaelement2svg.h"
#include "../svg/kicadmodule2svg.h"
#include "../svg/kicadschematic2svg.h"
#include "../sketchtoolbutton.h"
#include "../items/virtualwire.h"
#include "../layerattributes.h"
#include "../items/paletteitem.h"
#include "../items/paletteitembase.h"
#include "../model/modelpart.h"
#include "../connectors/connectoritem.h"
#include "../connectors/bus.h"
#include "../installedfonts.h"
#include "../dock/layerpalette.h"
#include "../utils/svgcursorbuilder.h"
#include "../infoview/htmlinfoview.h"

#include <QtDebug>
#include <QInputDialog>
#include <QApplication>
#include <QSvgGenerator>
#include <QMenuBar>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <limits>

////////////////////////////////////////////////////

static bool RubberBandLegWarning = false;

static bool GotZeroConnector = false;

static const QString ReferenceFileString("referenceFile");

static constexpr int IconViewIndex = 3;
static constexpr int MetadataViewIndex = 4;
static constexpr int ConnectorsViewIndex = 5;

constexpr static int PegiZ = 5000;
constexpr static int RatZ = 6000;

static long FakeGornSiblingNumber = 0;

void removeGornAux(QDomElement & element) {
	bool hasGorn = element.hasAttribute("gorn");
	bool hasOld = element.hasAttribute("oldid");
	if (hasGorn && hasOld) {
		element.removeAttribute("gorn");
		QString oldid = element.attribute("oldid");
		if (oldid.isEmpty()) element.removeAttribute("id");
		else element.setAttribute("id", oldid);
		element.removeAttribute("oldid");
	}
	QDomElement child = element.firstChildElement();
	while (!child.isNull()) {
		removeGornAux(child);
		child = child.nextSiblingElement();
	}
}

QString removeGorn(QString & svg) {
	QDomDocument doc;

	QDomDocument::ParseResult parseResult = doc.setContent(svg);
	if (!parseResult) {
		// shouldn't happen
		DebugDialog::debug(QString("remove gorn failure: %1 %2 %3 %4").arg(parseResult.errorMessage).arg(parseResult.errorLine).arg(parseResult.errorColumn).arg(svg));
		return svg;
	}

	QDomElement root = doc.documentElement();
	removeGornAux(root);

	return doc.toString(4);
}

bool byID(QDomElement & c1, QDomElement & c2)
{
	int c1id = -1;
	int c2id = -1;
	QRegularExpressionMatch match;
	int ix = c1.attribute("id").indexOf(IntegerFinder, 0, &match);
	if (ix >= 0) c1id = match.captured(0).toInt();
	match = QRegularExpressionMatch();
	ix = c2.attribute("id").indexOf(IntegerFinder, 0, &match);
	if (ix >= 0) c2id = match.captured(0).toInt();

	if (c1id == 0 || c2id == 0) GotZeroConnector = true;

	return c1id < c2id;
}

void removeID(QDomElement & root, const QString & value) {
	if (root.attribute("id") == value) {
		root.removeAttribute("id");
	}

	QDomNodeList nodeList = root.childNodes();
	for (int i = 0; i < nodeList.count(); i++) {
		QDomNode node = nodeList.item(i);
		if (node.nodeType() == QDomNode::ElementNode) {
			QDomElement element = node.toElement();
			removeID(element, value);
		}
	}
}

////////////////////////////////////////////////////

IconSketchWidget::IconSketchWidget(ViewLayer::ViewID viewID, QWidget *parent)
	: SketchWidget(viewID, parent)
{
	m_shortName = QObject::tr("ii");
	m_viewName = QObject::tr("Icon View");
	initBackgroundColor();
}

void IconSketchWidget::addViewLayers() {
	setViewLayerIDs(ViewLayer::Icon, ViewLayer::Icon, ViewLayer::Icon, ViewLayer::Icon, ViewLayer::Icon);
	addViewLayersAux(ViewLayer::layersForView(ViewLayer::IconView), ViewLayer::layersForViewFromBelow(ViewLayer::IconView));
}

////////////////////////////////////////////////////

PEMainWindow::PEMainWindow(ReferenceModel * referenceModel, QWidget * parent)
	: MainWindow(referenceModel, parent),
	m_connectorsView(nullptr),
	m_peToolView(nullptr),
	m_peSvgView(nullptr),
	m_guid(TextUtils::getRandText()),
	m_prefix("prefix0000"),
	m_fileIndex(0),
	m_canSave(false),
	m_gaveSaveWarning(false),
	m_inPickMode(false),
	m_useNextPick(false)
{
    m_settingsPrefix = "pe/";
	m_viewThings.insert(ViewLayer::BreadboardView, new ViewThing);
	m_viewThings.insert(ViewLayer::SchematicView, new ViewThing);
	m_viewThings.insert(ViewLayer::PCBView, new ViewThing);
	m_viewThings.insert(ViewLayer::IconView, new ViewThing);

	m_autosaveTimer.stop();
	disconnect(&m_autosaveTimer, SIGNAL(timeout()), this, SLOT(backupSketch()));
	m_userPartsFolderPath = FolderUtils::getLocalPartsPath()+"/user/";
	m_userPartsFolderSvgPath = m_userPartsFolderPath;
}

PEMainWindow::~PEMainWindow()
{
	// PEGraphicsItems are still holding QDomElement so delete them before m_fzpDocument is deleted
	killPegi();

	// kill temp files
	Q_FOREACH (QString string, m_filesToDelete) {
		QFile::remove(string);
	}
	QDir dir = QDir::temp();
	dir.rmdir(makeDirName());
}

void PEMainWindow::closeEvent(QCloseEvent *event)
{
	qDebug() << "close event";

	QStringList messages;

	if (m_inFocusWidgets.count() > 0) {
		bool gotOne = false;
		// should only be one in-focus widget
		Q_FOREACH (QWidget * widget, m_inFocusWidgets) {
			auto * lineEdit = qobject_cast<QLineEdit *>(widget);
			if (lineEdit != nullptr) {
				if (lineEdit->isModified()) {
					lineEdit->clearFocus();
					lineEdit->setModified(false);
					gotOne = true;
				}
			}
			else {
				auto * textEdit = qobject_cast<QTextEdit *>(widget);
				if (textEdit != nullptr) {
					if (textEdit->document()->isModified()) {
						textEdit->clearFocus();
						textEdit->document()->setModified(false);
						gotOne = true;
					}
				}
			}
		}
		if (gotOne) {
			messages << tr("There is one last edit still pending.");
		}
	}

	QString family = m_metadataView->family();
	if (family.isEmpty()) {
		messages << tr("The 'family' property can not be blank.");
	}

	QStringList keys = m_metadataView->properties().keys();

	if (keys.contains("family", Qt::CaseInsensitive)) {
		messages << tr("A duplicate 'family' property is not allowed");
	}

	if (keys.contains("variant", Qt::CaseInsensitive)) {
		messages << tr("A duplicate 'variant' property is not allowed");
	}

	bool discard = true;
	if (messages.count() > 0) {
		QMessageBox messageBox(this);
		messageBox.setWindowTitle(tr("Close without saving?", "dialog title"));

		QString message = tr("This part cannot be saved as-is:\n\n");
		for (const QString &string : messages) {
			message.append('\t');
			message.append(string);
			message.append("\n\n");
		}

		message += tr("Do you want to keep working or close without saving?");

		messageBox.setText(message);
		messageBox.setIcon(QMessageBox::Warning);
		messageBox.setWindowModality(Qt::WindowModal);

		messageBox.addButton(tr("Close without saving"), QMessageBox::AcceptRole);
		auto *keepWorkingButton = messageBox.addButton(tr("Keep working"), QMessageBox::RejectRole);

		messageBox.setDefaultButton(keepWorkingButton);

		messageBox.exec();
		if (messageBox.clickedButton() == keepWorkingButton) {
			event->ignore();
			return;
		}
	}
	else if (!beforeClosing(true, discard)) {
		event->ignore();
		return;
	}

	if (!discard) {
		// if all connectors not found, put up a message
		connectorWarning();
	}

	QSettings settings;
	settings.setValue(m_settingsPrefix + "state", saveState());
	settings.setValue(m_settingsPrefix + "geometry", saveGeometry());

	QMainWindow::closeEvent(event);
}

void PEMainWindow::initLockedFiles(bool) {
}

void PEMainWindow::initSketchWidgets(bool whatever)
{
	Q_UNUSED(whatever);

	MainWindow::initSketchWidgets(false);

	m_iconGraphicsView = new IconSketchWidget(ViewLayer::IconView, this);
	initSketchWidget(m_iconGraphicsView);
	m_iconWidget = new SketchAreaWidget(m_iconGraphicsView,this);
	addTab(m_iconWidget, tr("Icon"));
	initSketchWidget(m_iconGraphicsView);

	ViewThing * viewThing = m_viewThings.value(m_breadboardGraphicsView->viewID());
	viewThing->sketchWidget = m_breadboardGraphicsView;
	viewThing->document = &m_breadboardDocument;

	viewThing = m_viewThings.value(m_schematicGraphicsView->viewID());
	viewThing->sketchWidget = m_schematicGraphicsView;
	viewThing->document = &m_schematicDocument;

	viewThing = m_viewThings.value(m_pcbGraphicsView->viewID());
	viewThing->sketchWidget = m_pcbGraphicsView;
	viewThing->document = &m_pcbDocument;

	viewThing = m_viewThings.value(m_iconGraphicsView->viewID());
	viewThing->sketchWidget = m_iconGraphicsView;
	viewThing->document = &m_iconDocument;

	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		viewThing->sketchWidget->setAcceptWheelEvents(true);
		viewThing->sketchWidget->setChainDrag(false);				// no bendpoints
		viewThing->firstTime = true;
		viewThing->everZoomed = true;
		connect(viewThing->sketchWidget, SIGNAL(newWireSignal(Wire *)), this, SLOT(newWireSlot(Wire *)));
		connect(viewThing->sketchWidget, SIGNAL(showing(SketchWidget *)), this, SLOT(showing(SketchWidget *)));
		connect(viewThing->sketchWidget, SIGNAL(itemMovedSignal(ItemBase *)), this, SLOT(itemMovedSlot(ItemBase *)));
		connect(viewThing->sketchWidget, SIGNAL(resizedSignal(ItemBase *)), this, SLOT(resizedSlot(ItemBase *)));
		connect(viewThing->sketchWidget, SIGNAL(clickedItemCandidateSignal(QGraphicsItem *, bool &)), this, SLOT(clickedItemCandidateSlot(QGraphicsItem *, bool &)), Qt::DirectConnection);
		connect(viewThing->sketchWidget, SIGNAL(itemAddedSignal(ModelPart *, ItemBase *, ViewLayer::ViewLayerPlacement, const ViewGeometry &, long, SketchWidget *)),
		        this, SLOT(itemAddedSlot(ModelPart *, ItemBase *, ViewLayer::ViewLayerPlacement, const ViewGeometry &, long, SketchWidget *)));


	}

	m_metadataView = new PEMetadataView(m_referenceModel, this);
	auto * sketchAreaWidget = new SketchAreaWidget(m_metadataView, this, false, false);
	addTab(sketchAreaWidget, tr("Metadata"));
	connect(m_metadataView, SIGNAL(metadataChanged(const QString &, const QString &)), this, SLOT(metadataChanged(const QString &, const QString &)), Qt::DirectConnection);
	connect(m_metadataView, SIGNAL(tagsChanged(const QStringList &)), this, SLOT(tagsChanged(const QStringList &)), Qt::DirectConnection);
	connect(m_metadataView, SIGNAL(propertiesChanged(const QHash<QString, QString> &)), this, SLOT(propertiesChanged(const QHash<QString, QString> &)), Qt::DirectConnection);
	connect(m_metadataView, SIGNAL(historyChanged(const QList<HistoryEntry> &)), this, SLOT(historyChanged(const QList<HistoryEntry> &)), Qt::DirectConnection);

	m_connectorsView = new PEConnectorsView(this);
	sketchAreaWidget = new SketchAreaWidget(m_connectorsView, this, false, false);
	addTab(sketchAreaWidget, tr("Connectors"));

	connect(m_connectorsView, SIGNAL(connectorMetadataChanged(ConnectorMetadata *)), this, SLOT(connectorMetadataChanged(ConnectorMetadata *)), Qt::DirectConnection);
	connect(m_connectorsView, SIGNAL(connectorsTypeChanged(Connector::ConnectorType)), this, SLOT(connectorsTypeChanged(Connector::ConnectorType)));
	connect(m_connectorsView, SIGNAL(removedConnectors(QList<ConnectorMetadata *> &)), this, SLOT(removedConnectors(QList<ConnectorMetadata *> &)), Qt::DirectConnection);
	connect(m_connectorsView, SIGNAL(connectorCountChanged(int)), this, SLOT(connectorCountChanged(int)), Qt::DirectConnection);
	connect(m_connectorsView, SIGNAL(smdChanged(const QString &)), this, SLOT(smdChanged(const QString &)));

}

void PEMainWindow::initDock()
{
	m_layerPalette = new LayerPalette(this);

	// P5 (library-at-hand): the parts editor previously shipped with no part browser at all
	// (this block was commented out), so "start from an existing part" was only reachable from the
	// main app's own palette. Construct the full bin/palette system here so the whole library is
	// browsable and searchable inside the editor itself.
	m_infoView = new HtmlInfoView();
	m_infoView->init(false);

	m_binManager = new BinManager(m_referenceModel, m_infoView, m_undoStack, this);
	// initStandardBins() loads every discoverable bin (core + contrib + user) *and* the Search bin,
	// so the user can browse or search the entire library and pick any part as the base to edit.
	// Picking "Edit Part" in that bin routes through MainWindow::getPartsEditorNewAnd (inherited),
	// which qobject_casts the top-level to this PEMainWindow and calls setInitialItem() on it -- so
	// "use an existing part as base" is now first-class inside the editor, with no extra routing.
	m_binManager->initStandardBins();
	m_binManager->hideTempPartsBin();
}

void PEMainWindow::moreInitDock()
{
	static int BinMinHeight = 75;
	static int ToolDefaultHeight = 150;
	static int SvgDefaultHeight = 50;


	m_peToolView = new PEToolView();
	connect(m_peToolView, SIGNAL(getSpinAmount(double &)), this, SLOT(getSpinAmount(double &)), Qt::DirectConnection);
	connect(m_peToolView, SIGNAL(terminalPointChanged(const QString &)), this, SLOT(terminalPointChanged(const QString &)));
	connect(m_peToolView, SIGNAL(terminalPointChanged(const QString &, double)), this, SLOT(terminalPointChanged(const QString &, double)));
	connect(m_peToolView, SIGNAL(switchedConnector(int)), this, SLOT(switchedConnector(int)));
	connect(m_peToolView, SIGNAL(removedConnector(const QDomElement &)), this, SLOT(removedConnector(const QDomElement &)));
	connect(m_peToolView, SIGNAL(pickModeChanged(bool)), this, SLOT(pickModeChanged(bool)));
	connect(m_peToolView, SIGNAL(busModeChanged(bool)), this, SLOT(busModeChanged(bool)));
	connect(m_peToolView, SIGNAL(connectorMetadataChanged(ConnectorMetadata *)), this, SLOT(connectorMetadataChanged(ConnectorMetadata *)), Qt::DirectConnection);
	connect(m_peToolView, SIGNAL(createPrimitiveRequested(const QString &)), this, SLOT(createPrimitiveRequested(const QString &)));
	connect(m_peToolView, SIGNAL(addConnectorRequested()), this, SLOT(addConnectorRequested()));
	makeDock(tr("Connectors"), m_peToolView, DockMinWidth, ToolDefaultHeight);
	m_peToolView->setMinimumSize(DockMinWidth, ToolDefaultHeight);

	m_peSvgView = new PESvgView();
	makeDock(tr("SVG"), m_peSvgView, DockMinWidth, SvgDefaultHeight);
	m_peSvgView->setMinimumSize(DockMinWidth, SvgDefaultHeight);

	if (m_binManager != nullptr) {
		QDockWidget * dockWidget = makeDock(BinManager::Title, m_binManager, DockMinWidth, BinMinHeight);
		dockWidget->resize(0, 0);
	}

	if (m_infoView != nullptr) {
		makeDock(tr("Inspector"), m_infoView, InfoViewMinHeight, InfoViewHeightDefault);
		this -> setObjectName("PEInspector");
	}

	//: Title of the view-layers visibility palette (show/hide display layers).
	//: "Layers" in the graphics sense — not the copper stackup.
	makeDock(tr("Layers"), m_layerPalette, DockMinWidth, DockMinHeight)->hide();
	m_layerPalette->setMinimumSize(DockMinWidth, DockMinHeight);
	m_layerPalette->setShowAllLayersAction(m_showAllLayersAct);
	m_layerPalette->setHideAllLayersAction(m_hideAllLayersAct);

}

void PEMainWindow::createFileMenuActions() {
	MainWindow::createFileMenuActions();

	m_reuseBreadboardAct = new QAction(tr("Reuse breadboard image"), this);
	m_reuseBreadboardAct->setStatusTip(tr("Reuse the breadboard image in this view"));
	connect(m_reuseBreadboardAct, SIGNAL(triggered()), this, SLOT(reuseBreadboard()));

	m_reuseSchematicAct = new QAction(tr("Reuse schematic image"), this);
	m_reuseSchematicAct->setStatusTip(tr("Reuse the schematic image in this view"));
	connect(m_reuseSchematicAct, SIGNAL(triggered()), this, SLOT(reuseSchematic()));

	m_reusePCBAct = new QAction(tr("Reuse PCB image"), this);
	m_reusePCBAct->setStatusTip(tr("Reuse the PCB image in this view"));
	connect(m_reusePCBAct, SIGNAL(triggered()), this, SLOT(reusePCB()));

	// P6 create-from-blank + svg-import file actions.
	m_newBlankPartAct = new QAction(tr("New blank part"), this);
	m_newBlankPartAct->setStatusTip(tr("Start a new empty part from scratch"));
	connect(m_newBlankPartAct, SIGNAL(triggered()), this, SLOT(createBlankPart()));

	m_importSvgAct = new QAction(tr("Import SVG..."), this);
	m_importSvgAct->setStatusTip(tr("Import an SVG file as the graphic for the current view"));
	connect(m_importSvgAct, SIGNAL(triggered()), this, SLOT(importSvgFile()));

}

void PEMainWindow::createActions()
{
	createFileMenuActions();

	m_openAct->setText(tr("Load image for view..."));
	m_openAct->setStatusTip(tr("Open a file to use as the image for this view of the part."));
	disconnect(m_openAct, SIGNAL(triggered()), this, SLOT(mainLoad()));
	connect(m_openAct, SIGNAL(triggered()), this, SLOT(loadImage()));

	m_showInOSAct = new QAction(tr("Show in Folder"), this);
	m_showInOSAct->setStatusTip(tr("On the desktop, open the folder containing the current svg file."));
	connect(m_showInOSAct, SIGNAL(triggered()), this, SLOT(showInOS()));

	createEditMenuActions();

	m_convertToTenthAct = new QAction(tr("Convert schematic to 0.1 inch standard"), this);
	m_convertToTenthAct->setStatusTip(tr("Convert pre-0.8.6 schematic image to new 0.1 inch standard"));
	connect(m_convertToTenthAct, SIGNAL(triggered()), this, SLOT(convertToTenth()));

	m_deleteBusConnectionAct = new WireAction(tr("Remove Internal Connection"), this);
	connect(m_deleteBusConnectionAct, SIGNAL(triggered()), this, SLOT(deleteBusConnection()));

	createViewMenuActions(false);
	createHelpMenuActions();
	createWindowMenuActions();
	createActiveLayerActions();

}

void PEMainWindow::createMenus()
{
	createFileMenu();
	createEditMenu();
	createViewMenu();
	createWindowMenu();
	createHelpMenu();
}

void PEMainWindow::createFileMenu() {
	m_fileMenu = menuBar()->addMenu(tr("&File"));
	m_fileMenu->addAction(m_openAct);
	m_fileMenu->addAction(m_reuseBreadboardAct);
	m_fileMenu->addAction(m_reuseSchematicAct);
	m_fileMenu->addAction(m_reusePCBAct);
	m_fileMenu->addSeparator();
	m_fileMenu->addAction(m_newBlankPartAct);
	m_fileMenu->addAction(m_importSvgAct);

	//m_fileMenu->addAction(m_revertAct);

	m_fileMenu->addSeparator();
	m_fileMenu->addAction(m_closeAct);
	m_fileMenu->addAction(m_saveAct);
	m_fileMenu->addAction(m_saveAsAct);
	m_saveAsAct->setText(tr("Save as new part"));
	m_saveAsAct->setStatusTip(tr("Make a copy of the part and save it in the 'My Parts' Bin"));

	m_fileMenu->addSeparator();
	m_exportMenu = m_fileMenu->addMenu(tr("&Export"));
	//m_fileMenu->addAction(m_pageSetupAct);
	m_fileMenu->addAction(m_printAct);
	m_fileMenu->addAction(m_showInOSAct);

	m_fileMenu->addSeparator();
	m_fileMenu->addAction(m_quitAct);

	populateExportMenu();

	connect(m_fileMenu, SIGNAL(aboutToShow()), this, SLOT(updateFileMenu()));
	connect(m_exportMenu, SIGNAL(aboutToShow()), this, SLOT(updateExportMenu()));
}

void PEMainWindow::createEditMenu()
{
	m_editMenu = menuBar()->addMenu(tr("&Edit"));
	m_editMenu->addAction(m_undoAct);
	m_editMenu->addAction(m_redoAct);
	m_editMenu->addSeparator();
	m_editMenu->addAction(m_convertToTenthAct);

	updateEditMenu();
	connect(m_editMenu, SIGNAL(aboutToShow()), this, SLOT(updateEditMenu()));
}

bool PEMainWindow::activeLayerWidgetAlwaysOn() {
	return true;
}

void PEMainWindow::connectPairs() {
	bool succeeded = true;
	succeeded =  succeeded && (connect(qApp, SIGNAL(spaceBarIsPressedSignal(bool)), m_breadboardGraphicsView, SLOT(spaceBarIsPressedSlot(bool))) != nullptr);
	succeeded =  succeeded && (connect(qApp, SIGNAL(spaceBarIsPressedSignal(bool)), m_schematicGraphicsView, SLOT(spaceBarIsPressedSlot(bool))) != nullptr);
	succeeded =  succeeded && (connect(qApp, SIGNAL(spaceBarIsPressedSignal(bool)), m_pcbGraphicsView, SLOT(spaceBarIsPressedSlot(bool))) != nullptr);

	succeeded =  succeeded && (connect(m_pcbGraphicsView, SIGNAL(cursorLocationSignal(double, double, double, double)), this, SLOT(cursorLocationSlot(double, double, double, double))) != nullptr);
	succeeded =  succeeded && (connect(m_breadboardGraphicsView, SIGNAL(cursorLocationSignal(double, double, double, double)), this, SLOT(cursorLocationSlot(double, double, double, double))) != nullptr);
	succeeded =  succeeded && (connect(m_schematicGraphicsView, SIGNAL(cursorLocationSignal(double, double, double, double)), this, SLOT(cursorLocationSlot(double, double, double, double))) != nullptr);

	if (!succeeded) {
		DebugDialog::debug("PEMainWindow::connectPairs connect partly failed.");
	}

	connect(m_breadboardGraphicsView, SIGNAL(setActiveWireSignal(Wire *)), this, SLOT(setActiveWire(Wire *)));
	connect(m_schematicGraphicsView, SIGNAL(setActiveWireSignal(Wire *)), this, SLOT(setActiveWire(Wire *)));
	connect(m_pcbGraphicsView, SIGNAL(setActiveWireSignal(Wire *)), this, SLOT(setActiveWire(Wire *)));

}

QMenu *PEMainWindow::breadboardWireMenu() {
	auto *menu = new QMenu(QObject::tr("Internal Connections"), this);

	menu->addAction(m_deleteBusConnectionAct);
	connect( menu, SIGNAL(aboutToShow()), this, SLOT(updateWireMenu()));
	return menu;
}

QMenu *PEMainWindow::breadboardItemMenu() {
	return nullptr;
}

QMenu *PEMainWindow::schematicWireMenu() {
	return breadboardWireMenu();
}

QMenu *PEMainWindow::schematicItemMenu() {
	return nullptr;
}

QMenu *PEMainWindow::pcbWireMenu() {
	return breadboardWireMenu();
}

QMenu *PEMainWindow::pcbItemMenu() {
	return nullptr;
}

bool PEMainWindow::setInitialItem(PaletteItem * paletteItem)
{
	m_pcbGraphicsView->setBoardLayers(2, true);
	m_pcbGraphicsView->setLayerActive(ViewLayer::Copper1, true);
	m_pcbGraphicsView->setLayerActive(ViewLayer::Copper0, true);
	m_pcbGraphicsView->setLayerActive(ViewLayer::Silkscreen1, true);
	m_pcbGraphicsView->setLayerActive(ViewLayer::Silkscreen0, true);

	ModelPart * originalModelPart = nullptr;
	if (paletteItem == nullptr) {
		// this shouldn't happen
		originalModelPart = m_referenceModel->retrieveModelPart("generic_ic_dip_8_300mil");
	}
	else {
		originalModelPart = paletteItem->modelPart();
	}

	m_originalFzpPath = originalModelPart->path();
	m_originalModuleID = originalModelPart->moduleID();

	// Use moduleID subfolder for temp SVGs, consistent with saveAs() and PartFactory search paths
	QString sanitizedModuleID = FolderUtils::sanitizeForFolder(m_originalModuleID);
	if (!sanitizedModuleID.isEmpty()) {
		m_userPartsFolderSvgPath = m_userPartsFolderPath + sanitizedModuleID + "/";
	}

	QFileInfo info(originalModelPart->path());
	QString basename = info.completeBaseName();
	int ix = basename.indexOf(GuidMatcher, 0);
	if (ix > 1 && basename.at(ix - 1) == '_')  {
		int dix = ix + 32 + 1;
		if (basename.size() > dix) {
			bool gotPrefix = true;
			for (int i = dix; i < basename.size(); i++) {
				if (!basename.at(i).isDigit()) {
					gotPrefix = false;
					break;
				}
			}
			if (gotPrefix) {
				m_prefix = basename.left(ix - 1);
			}
		}
	}

	//DebugDialog::debug(QString("%1, %2").arg(info.absoluteFilePath()).arg(m_userPartsFolderPath));
	m_canSave = info.absoluteFilePath().contains(m_userPartsFolderPath);

	if (!loadFzp(originalModelPart->path())) return false;

	QDomElement fzpRoot = m_fzpDocument.documentElement();
	QString referenceFile = fzpRoot.attribute(ReferenceFileString);
	if (referenceFile.isEmpty()) {
		fzpRoot.setAttribute(ReferenceFileString, info.fileName());
	}
	QDomElement views = fzpRoot.firstChildElement("views");
	QDomElement author = fzpRoot.firstChildElement("author");
	if (author.isNull()) {
		author = m_fzpDocument.createElement("author");
		fzpRoot.appendChild(author);
	}
	if (author.text().isEmpty()) {
		TextUtils::replaceChildText(author, QString(getenvUser()));
	}
	QDomElement date = fzpRoot.firstChildElement("date");
	if (date.isNull()) {
		date = m_fzpDocument.createElement("date");
		fzpRoot.appendChild(date);
	}
	TextUtils::replaceChildText(date, QDate::currentDate().toString(Qt::ISODate));

	QDomElement properties = fzpRoot.firstChildElement("properties");
	if (properties.isNull()) {
		properties = m_fzpDocument.createElement("properties");
		fzpRoot.appendChild(properties);
	}
	QHash<QString,QString> props = originalModelPart->properties();
	Q_FOREACH (QString key, props.keys()) {
		replaceProperty(key, props.value(key), properties);
	}
	// record "local" properties
	Q_FOREACH (QByteArray byteArray, originalModelPart->dynamicPropertyNames()) {
		replaceProperty(byteArray, originalModelPart->property(byteArray).toString(), properties);
	}

	bool hasLegID = false;
	QDomElement connectors = fzpRoot.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		QString localName = originalModelPart->connectorLocalName(connector.attribute("id"));
		if (!localName.isEmpty()) {
			connector.setAttribute("name", localName);
		}
		if (!hasLegID) {
			QDomNodeList plist = connector.elementsByTagName("p");
			for (int i = 0; i < plist.count(); i++) {
				QDomElement p = plist.at(i).toElement();
				if (!p.attribute("legId").isEmpty()) {
					hasLegID = true;
					break;
				}
			}
		}
		connector = connector.nextSiblingElement("connector");
	}

	if (hasLegID && !RubberBandLegWarning) {
		RubberBandLegWarning = true;
		QMessageBox::warning(nullptr, tr("Parts Editor", "dialog title"),
		                     tr("This part has bendable legs. "
		                        "This version of the Parts Editor does not yet support editing bendable legs, "
		                        "and the legs may not be displayed correctly in breadboard view. "
		                        "If you make changes to breadboard view, or change connector metadata, "
		                        "the legs may no longer work. "
		                        "You can safely make changes to Schematic or PCB view.\n\n"
		                        "This warning will not be repeated in this session of Fritzing")
		                    );
	}



	// for now kill the editable pin labels property, otherwise the saved part will try to use the labels that are only found in the sketch
	QDomElement epl = TextUtils::findElementWithAttribute(properties, "name", "editable pin labels");
	if (!epl.isNull()) {
		TextUtils::replaceChildText(epl, "false");
	}

	QDomElement family = TextUtils::findElementWithAttribute(properties, "name", "family");
	if (family.isNull()) {
		replaceProperty("family", m_guid, properties);
	}
	QDomElement variant = TextUtils::findElementWithAttribute(properties, "name", "variant");
	if (variant.isNull()) {
		QString newVariant = makeNewVariant(family.text());
		replaceProperty("variant", newVariant, properties);
	}

	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		if (viewThing->sketchWidget == nullptr) continue;

		ItemBase * itemBase = originalModelPart->viewItem(viewThing->sketchWidget->viewID());
		if (itemBase == nullptr) continue;

		viewThing->referenceFile = getSvgReferenceFile(itemBase->filename());

		if (!itemBase->hasCustomSVG()) {
			continue;
		}

		QHash<QString, QString> svgHash;
		QStringList svgList;
		double factor;
		Q_FOREACH (ViewLayer * vl, viewThing->sketchWidget->viewLayers().values()) {
			QString string = itemBase->retrieveSvg(vl->viewLayerID(), svgHash, false, GraphicsUtils::StandardFritzingDPI, factor);
			if (!string.isEmpty()) {
				svgList.append(string);
			}
		}

		if (svgList.count() == 0) {
			DebugDialog::debug(QString("pe: missing custom svg %1").arg(originalModelPart->moduleID()));
			continue;
		}


		QString svg;
		if (svgList.count() == 1) {
			svg = svgList.at(0);
		}
		else {
			// deal with copper0 and copper1 layers as parent/child
			// have to remove whitespace in order to compare the two svgs
			QRegularExpression white("\\s");
			QStringList whiteList;
			Q_FOREACH (QString string, svgList) {
				string.remove(white);
				whiteList << string;
			}
			bool keepGoing = true;
			while (keepGoing) {
				keepGoing = false;
				for (int i = 0; i < whiteList.count() - 1; i++)  {
					for (int j = i + 1; j < whiteList.count(); j++) {
						if (whiteList.at(i).contains(whiteList.at(j))) {
							keepGoing = true;
							whiteList.removeAt(j);
							svgList.removeAt(j);
							break;
						}
						if (whiteList.at(j).contains(whiteList.at(i))) {
							keepGoing = true;
							whiteList.removeAt(i);
							svgList.removeAt(i);
							break;
						}
					}
					if (keepGoing) break;
				}
			}
			svg = svgList.join("\n");
		}

		QSizeF size = itemBase->size();
		QString header = TextUtils::makeSVGHeader(GraphicsUtils::SVGDPI, GraphicsUtils::StandardFritzingDPI, size.width(), size.height());
		header += makeDesc(viewThing->referenceFile);
		svg = header + svg + "</svg>";
		QString svgPath = makeSvgPath2(viewThing->sketchWidget);
		QString actualPath = m_userPartsFolderSvgPath + svgPath;
		FolderUtils::ensureDirectoryExists(actualPath);
		bool result = writeXml(actualPath, removeGorn(svg), true);
		if (!result) {
			QMessageBox::critical(nullptr, tr("Parts Editor", "dialog title"), tr("Unable to write svg to  %1").arg(svgPath));
			return false;
		}

		QDomElement view = views.firstChildElement(ViewLayer::viewIDXmlName(viewThing->sketchWidget->viewID()));
		QDomElement layers = view.firstChildElement("layers");
		if (layers.isNull()) {
			QMessageBox::critical(nullptr, tr("Parts Editor", "dialog title"), tr("Unable to parse fzp file  %1").arg(originalModelPart->path()));
			return false;
		}

		setImageAttribute(layers, svgPath);
	}

	reload(true);

	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		if (viewThing->itemBase != nullptr) {
			viewThing->originalSvgPath = viewThing->itemBase->filename();
		}
		viewThing->svgChangeCount = 0;
	}

	setTitle();
	createRaiseWindowActions();
	return true;
}

void PEMainWindow::initZoom() {
	if (m_peToolView == nullptr) return;
	if (m_currentGraphicsView == nullptr) return;

	ViewThing * viewThing = m_viewThings.value(m_currentGraphicsView->viewID());
	if (viewThing->itemBase == nullptr) return;

	if (!viewThing->everZoomed) {
		viewThing->everZoomed = true;
		m_currentGraphicsView->fitInWindow();
	}

	m_peSvgView->setFilename(viewThing->referenceFile);
}

void PEMainWindow::setTitle() {
	QString title = tr("Fritzing (New) Parts Editor");
	QString partTitle = getPartTitle();

	QString viewName;
	if (m_currentGraphicsView != nullptr) viewName = m_currentGraphicsView->viewName();
	else if (currentTabIndex() == IconViewIndex) viewName = tr("Icon View");
	else if (currentTabIndex() == MetadataViewIndex) viewName = tr("Metadata View");
	else if (currentTabIndex() == ConnectorsViewIndex) viewName = tr("Connectors View");

	setWindowTitle(QString("%1: %2 [%3]%4").arg(title).arg(partTitle).arg(viewName).arg(QtFunkyPlaceholder));
}

void PEMainWindow::createViewMenuActions(bool showWelcome) {
	MainWindow::createViewMenuActions(showWelcome);

	m_showIconAct = new QAction(tr("Show Icon"), this);
	m_showIconAct->setShortcut(tr("Ctrl+4"));
	m_showIconAct->setStatusTip(tr("Show the icon view"));
	connect(m_showIconAct, SIGNAL(triggered()), this, SLOT(showIconView()));

	m_showMetadataViewAct = new QAction(tr("Show Metadata"), this);
	m_showMetadataViewAct->setShortcut(tr("Ctrl+5"));
	m_showMetadataViewAct->setStatusTip(tr("Show the metadata view"));
	connect(m_showMetadataViewAct, SIGNAL(triggered()), this, SLOT(showMetadataView()));

	m_showConnectorsViewAct = new QAction(tr("Show Connectors"), this);
	m_showConnectorsViewAct->setShortcut(tr("Ctrl+6"));
	m_showConnectorsViewAct->setStatusTip(tr("Show the connector metadata in a list view"));
	connect(m_showConnectorsViewAct, SIGNAL(triggered()), this, SLOT(showConnectorsView()));

	m_hideOtherViewsAct = new QAction(tr("Make only this view visible"), this);
	m_hideOtherViewsAct->setStatusTip(tr("The part will only be visible in this view and icon view"));
	connect(m_hideOtherViewsAct, SIGNAL(triggered()), this, SLOT(hideOtherViews()));

}

void PEMainWindow::createViewMenu() {
	MainWindow::createViewMenu();

	bool afterNext = false;
	Q_FOREACH (QAction * action, m_viewMenu->actions()) {
		if (action == m_setBackgroundColorAct) {
			afterNext = true;
		}
		else if (afterNext) {
			m_viewMenu->insertSeparator(action);
			m_viewMenu->insertAction(action, m_hideOtherViewsAct);
			break;
		}
	}

	afterNext = false;
	Q_FOREACH (QAction * action, m_viewMenu->actions()) {
		if (action == m_showPCBAct) {
			afterNext = true;
		}
		else if (afterNext) {
			m_viewMenu->insertAction(action, m_showIconAct);
			m_viewMenu->insertAction(action, m_showMetadataViewAct);
			m_viewMenu->insertAction(action, m_showConnectorsViewAct);
			break;
		}
	}
	m_numFixedActionsInViewMenu = m_viewMenu->actions().size();
}

void PEMainWindow::showMetadataView() {
	setCurrentTabIndex(MetadataViewIndex);
}

void PEMainWindow::showConnectorsView() {
	setCurrentTabIndex(ConnectorsViewIndex);
}

void PEMainWindow::showIconView() {
	setCurrentTabIndex(IconViewIndex);
}

void PEMainWindow::changeSpecialProperty(const QString & name, const QString & value)
{
	QHash<QString, QString> oldProperties = getOldProperties();

	if (value.isEmpty()) {
		QMessageBox::warning(nullptr, tr("Blank not allowed", "dialog title"), tr("The value of '%1' can not be blank.").arg(name));
		m_metadataView->resetProperty(name, value);
		return;
	}

	if (oldProperties.value(name) == value) {
		return;
	}

	QHash<QString, QString> newProperties(oldProperties);
	newProperties.insert(name, value);

	auto * cpc = new ChangePropertiesCommand(this, oldProperties, newProperties, nullptr);
	cpc->setText(tr("Change %1 to %2").arg(name).arg(value));
	cpc->setSkipFirstRedo();
	changeProperties(newProperties, false);
	m_undoStack->waitPush(cpc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::metadataChanged(const QString & name, const QString & value)
{
	qDebug() << "metadata changed";

	if (name.compare("family") == 0) {
		changeSpecialProperty(name, value);
		return;
	}

	if (name.compare("variant") == 0) {
		QString family = m_metadataView->family();
		QMultiHash<QString, QString> variants = m_referenceModel->allPropValues(family, "variant");
		QStringList values = variants.values(value);
		if (m_canSave) {
			QString moduleID = m_fzpDocument.documentElement().attribute("moduleId");
			values.removeOne(moduleID);
		}
		if (values.count() > 0) {
			QMessageBox::warning(nullptr, tr("Must be unique", "dialog title"), tr("Variant '%1' is in use. The variant name must be unique.").arg(value));
			return;
		}

		changeSpecialProperty(name, value);
		return;
	}

	QString menuText = (name.compare("description") == 0) ? tr("Change description") : tr("Change %1 to '%2'").arg(name).arg(value);

	// called from metadataView
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement element = root.firstChildElement(name);
	QString oldValue = element.text();
	if (oldValue == value) return;

	auto * cmc = new ChangeMetadataCommand(this, name, oldValue, value, nullptr);
	cmc->setText(menuText);
	cmc->setSkipFirstRedo();
	changeMetadata(name, value, false);
	m_undoStack->waitPush(cmc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::changeMetadata(const QString & name, const QString & value, bool updateDisplay)
{
	// called from command object
	QDomElement root = m_fzpDocument.documentElement();
	TextUtils::replaceElementChildText(root, name, value);

	//QString test = m_fzpDocument.toString();

	if (updateDisplay) {
		m_metadataView->initMetadata(m_fzpDocument);
	}
}

void PEMainWindow::tagsChanged(const QStringList & newTags)
{

	// called from metadataView
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement tags = root.firstChildElement("tags");
	QDomElement tag = tags.firstChildElement("tag");
	QStringList oldTags;
	while (!tag.isNull()) {
		oldTags << tag.text();
		tag = tag.nextSiblingElement("tag");
	}

	auto * ctc = new ChangeTagsCommand(this, oldTags, newTags, nullptr);
	ctc->setText(tr("Change tags"));
	ctc->setSkipFirstRedo();
	changeTags(newTags, false);
	m_undoStack->waitPush(ctc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::changeTags(const QStringList & newTags, bool updateDisplay)
{
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement tags = root.firstChildElement("tags");
	QDomElement tag = tags.firstChildElement("tag");
	while (!tag.isNull()) {
		tags.removeChild(tag);
		tag = tags.firstChildElement("tag");
	}

	Q_FOREACH (QString newTag, newTags) {
		QDomElement tag = m_fzpDocument.createElement("tag");
		tags.appendChild(tag);
		TextUtils::replaceChildText(tag, newTag);
	}

	if (updateDisplay) {
		m_metadataView->initMetadata(m_fzpDocument);
	}
}

void PEMainWindow::historyChanged(const QList<HistoryEntry> & newHistory)
{
	// called from metadataView (mirrors tagsChanged)
	QDomElement root = m_fzpDocument.documentElement();
	QList<HistoryEntry> oldHistory = readHistory(root);

	auto * chc = new ChangeHistoryCommand(this, oldHistory, newHistory, nullptr);
	chc->setText(tr("Change revision history"));
	chc->setSkipFirstRedo();
	// updateDisplay=true: the edit came from a modal dialog, so the table must be rebuilt to show it
	// (unlike tags/properties, whose inline widget already reflects the user's change).
	changeHistory(newHistory, true);
	m_undoStack->waitPush(chc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::changeHistory(const QList<HistoryEntry> & history, bool updateDisplay)
{
	// called from command object
	QDomElement root = m_fzpDocument.documentElement();
	writeHistory(root, history);

	if (updateDisplay) {
		m_metadataView->initMetadata(m_fzpDocument);
	}
}

QList<HistoryEntry> PEMainWindow::readHistory(const QDomElement & root)
{
	QList<HistoryEntry> history;
	for (QDomElement h = root.firstChildElement("history"); !h.isNull(); h = h.nextSiblingElement("history")) {
		HistoryEntry entry;
		entry.date = h.attribute("date");
		entry.author = h.attribute("author");
		entry.mode = h.attribute("mode", "optional");   // default when mode= omitted
		entry.description = h.text().trimmed();
		history.append(entry);
	}

	// Legacy parts carry a top-level <date>/<author> but no <history> yet. Synthesize a single
	// oldest entry (empty text is allowed for this auto-generated one) so the table isn't empty;
	// writeHistory materializes it into a real <history> element when the history is next written.
	if (history.isEmpty()) {
		QString date = root.firstChildElement("date").text().trimmed();
		QString author = root.firstChildElement("author").text().trimmed();
		if (!date.isEmpty() || !author.isEmpty()) {
			HistoryEntry entry;
			entry.date = date;
			entry.author = author;
			entry.mode = "optional";
			history.append(entry);
		}
	}

	// oldest -> newest; null (undated) dates sort first
	std::stable_sort(history.begin(), history.end(), [](const HistoryEntry & a, const HistoryEntry & b) {
		return a.parsedDate() < b.parsedDate();
	});
	return history;
}

void PEMainWindow::writeHistory(QDomElement & root, const QList<HistoryEntry> & history)
{
	QDomDocument doc = root.ownerDocument();

	// drop the existing <history> children
	QDomElement h = root.firstChildElement("history");
	while (!h.isNull()) {
		QDomElement next = h.nextSiblingElement("history");
		root.removeChild(h);
		h = next;
	}

	QList<HistoryEntry> sorted = history;
	std::stable_sort(sorted.begin(), sorted.end(), [](const HistoryEntry & a, const HistoryEntry & b) {
		return a.parsedDate() < b.parsedDate();
	});

	// re-insert oldest -> newest, grouped just after the part's metadata so re-saving an existing
	// part doesn't shove <history> to the bottom of the file
	QDomElement anchor = root.firstChildElement("date");
	if (anchor.isNull()) anchor = root.firstChildElement("author");
	if (anchor.isNull()) anchor = root.firstChildElement("title");
	if (anchor.isNull()) anchor = root.firstChildElement("version");
	QDomNode after = anchor;
	for (const HistoryEntry & entry : sorted) {
		QDomElement he = doc.createElement("history");
		he.setAttribute("date", entry.date);
		he.setAttribute("author", entry.author);
		he.setAttribute("mode", entry.mode.isEmpty() ? QString("optional") : entry.mode);
		if (!entry.description.isEmpty()) {
			he.appendChild(doc.createTextNode(entry.description));
		}
		if (after.isNull()) root.insertBefore(he, root.firstChild());
		else root.insertAfter(he, after);
		after = he;
	}

	// mirror the newest entry into the top-level <author>/<date> for backward compat
	if (!sorted.isEmpty()) {
		const HistoryEntry & newest = sorted.last();
		TextUtils::replaceElementChildText(root, "author", newest.author);
		TextUtils::replaceElementChildText(root, "date", newest.date);
	}
}

void PEMainWindow::propertiesChanged(const QHash<QString, QString> & newProperties)
{
	qDebug() << "properties changed";

	QStringList keys = newProperties.keys();
	if (keys.contains("family", Qt::CaseInsensitive)) {
		QMessageBox::warning(nullptr, tr("Duplicate problem", "dialog title"), tr("Duplicate 'family' property not allowed"));
		return;
	}

	if (keys.contains("variant", Qt::CaseInsensitive)) {
		QMessageBox::warning(nullptr, tr("Duplicate problem", "dialog title"), tr("Duplicate 'variant' property not allowed"));
		return;
	}

	// called from metadataView
	QHash<QString, QString> oldProperties = getOldProperties();

	auto * cpc = new ChangePropertiesCommand(this, oldProperties, newProperties, nullptr);
	cpc->setText(tr("Change properties"));
	cpc->setSkipFirstRedo();
	changeProperties(newProperties, false);
	m_undoStack->waitPush(cpc, SketchWidget::PropChangeDelay);
}


void PEMainWindow::changeProperties(const QHash<QString, QString> & newProperties, bool updateDisplay)
{
	bool incomingFamily = newProperties.keys().contains("family");
	bool incomingVariant = newProperties.keys().contains("variant");

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement properties = root.firstChildElement("properties");
	QDomElement prop = properties.firstChildElement("property");
	while (!prop.isNull()) {
		QDomElement next = prop.nextSiblingElement("property");
		bool doRemove = true;
		if (prop.attribute("name") == "family" && !incomingFamily) doRemove = false;
		if (prop.attribute("name") == "variant" && !incomingVariant) doRemove = false;
		if (doRemove) {
			properties.removeChild(prop);
		}
		prop = next;
	}

	Q_FOREACH (QString name, newProperties.keys()) {
		QDomElement prop = m_fzpDocument.createElement("property");
		properties.appendChild(prop);
		prop.setAttribute("name", name);
		TextUtils::replaceChildText(prop, newProperties.value(name));
	}

	if (updateDisplay) {
		m_metadataView->initMetadata(m_fzpDocument);
	}
}

QHash<QString, QString> PEMainWindow::getOldProperties()
{
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement properties = root.firstChildElement("properties");
	QDomElement prop = properties.firstChildElement("property");
	QHash<QString, QString> oldProperties;
	while (!prop.isNull()) {
		QString name = prop.attribute("name");
		QString value = prop.text();
		oldProperties.insert(name, value);
		prop = prop.nextSiblingElement("property");
	}

	return oldProperties;
}

void PEMainWindow::connectorMetadataChanged(ConnectorMetadata * cmd)
{
	int index;
	QDomElement connector = findConnector(cmd->connectorID, index);
	if (connector.isNull()) return;

	ConnectorMetadata oldcmd;
	fillInMetadata(connector, oldcmd);

	auto * ccmc = new ChangeConnectorMetadataCommand(this, &oldcmd, cmd, nullptr);
	ccmc->setText(tr("Change connector %1").arg(cmd->connectorName));
	bool skipFirstRedo = (sender() == m_connectorsView);
	if (skipFirstRedo) {
		ccmc->setSkipFirstRedo();
		changeConnectorElement(connector, cmd);
		initConnectors(false);
	}
	m_undoStack->waitPush(ccmc, SketchWidget::PropChangeDelay);
}

QDomElement PEMainWindow::findConnector(const QString & id, int & index)
{
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	index = 0;
	while (!connector.isNull()) {
		if (id.compare(connector.attribute("id")) == 0) {
			return connector;
		}
		connector = connector.nextSiblingElement("connector");
		index++;
	}

	return QDomElement();
}


void PEMainWindow::changeConnectorMetadata(ConnectorMetadata * cmd, bool updateDisplay) {
	int index;
	QDomElement connector = findConnector(cmd->connectorID, index);
	if (connector.isNull()) return;

	changeConnectorElement(connector, cmd);
	if (updateDisplay) {
		initConnectors(true);
		m_peToolView->setCurrentConnector(index);
	}
}

void PEMainWindow::changeConnectorElement(QDomElement & connector, ConnectorMetadata * cmd)
{
	connector.setAttribute("name", cmd->connectorName);
	connector.setAttribute("id", cmd->connectorID);
	connector.setAttribute("type", Connector::connectorNameFromType(cmd->connectorType));
	TextUtils::replaceElementChildText(connector, "description", cmd->connectorDescription);

#ifndef QT_NO_DEBUG
	QDomElement description = connector.firstChildElement("description");
	QString text = description.text();
	DebugDialog::debug(QString("description %1").arg(text));
#endif

}

void PEMainWindow::initSvgTree(SketchWidget * sketchWidget, ItemBase * itemBase, QDomDocument & svgDocument)
{
	QDomDocument tempSvgDoc;
	if (itemBase == nullptr) {
		return;
	}
	QFile file(itemBase->filename());
	if (!file.open(QIODevice::ReadOnly)) {
		DebugDialog::debug(QString("Unable to open :%1").arg(itemBase->filename()));
	}
	QDomDocument::ParseResult parseResult = tempSvgDoc.setContent(&file, QDomDocument::ParseOption::UseNamespaceProcessing);
	if (!parseResult) {
		DebugDialog::debug(QString("unable to parse svg: %1 %2 %3").arg(parseResult.errorMessage).arg(parseResult.errorLine).arg(parseResult.errorColumn));
		return;
	}

	// Record this view's layer structure so graphic creates can be routed into the right <g>
	// (see PEViewLayerMap and addPrimitive). Done here because this is where a view's svg is (re)loaded,
	// so the map always describes the document the editor is currently working from.
	m_viewLayerMap.inspect(tempSvgDoc.documentElement());

	if (itemBase->viewID() == ViewLayer::PCBView) {
		QDomElement root = tempSvgDoc.documentElement();
		QDomElement copper0 = TextUtils::findElementWithAttribute(root, "id", "copper0");
		QDomElement copper1 = TextUtils::findElementWithAttribute(root, "id", "copper1");
		if (!copper0.isNull() && !copper1.isNull()) {
			QDomElement parent0 = copper0.parentNode().toElement();
			QDomElement parent1 = copper1.parentNode().toElement();
			if (parent0.attribute("id") == "copper1") ;
			else if (parent1.attribute("id") == "copper0") ;
			else {
				// The part nests its two copper layers in the form this editor cannot fully handle. New
				// graphics are still placed correctly now (addPrimitive routes them via m_viewLayerMap),
				// so the wording is softened from "editing may produce an invalid image" to the accurate
				// statement of what is actually unusual about the file.
				DebugDialog::debug(QString("initSvgTree: PCB view '%1' has an unexpected copper0/copper1 nesting").arg(itemBase->filename()));
			}
		}
	}

	int z = PegiZ;

	// M0.3: initSvgTree() is called from reload(), showing(), setInitialItem() and after every graphic
	// edit (moveGraphic / resizeGraphic / editPathAnchor / addPrimitive / removePrimitive), and it used
	// to only ever ADD items. Nothing removed the previous set, so each edit left the whole prior batch
	// of PEGraphicsItems behind in the scene: duplicates stacked on every graphic, and getPegiList() --
	// which simply scans the scene -- then returned stale items whose QDomElement pointed at nodes from
	// an earlier parse. That is the direct cause of "click a shape, it drags the wrong thing / nothing
	// happens", and it made the per-view visibility pass in showing() hide items at random.
	//
	// Rebuilding the scene from the document is the cheap, always-correct resync here: the document is
	// the source of truth and the item count is small (a part view is tens to a few hundred graphics).
	// Any other item in the scene (wires, connector items, the reference svg item) is left alone.
	Q_FOREACH (QGraphicsItem * sceneItem, sketchWidget->scene()->items()) {
		if (auto * stale = dynamic_cast<PEGraphicsItem *>(sceneItem)) {
			sketchWidget->scene()->removeItem(stale);
			delete stale;
		}
	}

	FSvgRenderer tempRenderer;
	QByteArray rendered = tempRenderer.loadSvg(tempSvgDoc.toByteArray(), "", false);
	// PE_TRACE (temporary): this loadSvg -> setContent round trip REPLACES the live document with a
	// re-serialised copy. The field log shows the interactive item count collapsing back to the
	// pristine part count (11) right after a move/create, which means shapes added by the command
	// layer are not surviving this pass. Count what goes in and what comes out to confirm before
	// changing anything, because this pass is also what "cleans up the svg" for the renderer.
	{
		int before = tempSvgDoc.documentElement().elementsByTagName("*").count();
		int after  = 0;
		QDomDocument probe;
		if (probe.setContent(rendered, QDomDocument::ParseOption::UseNamespaceProcessing))
			after = probe.documentElement().elementsByTagName("*").count();
		DebugDialog::debug(QString("PE_TRACE svgCleanse view=%1 elements %2 -> %3 (%4 bytes)")
		                       .arg(sketchWidget->viewID()).arg(before).arg(after).arg(rendered.size()));
	}
	// cleans up the svg
	QDomDocument::ParseResult parseResult2 = svgDocument.setContent(rendered, QDomDocument::ParseOption::UseNamespaceProcessing);
	if (!parseResult2) {
		DebugDialog::debug(QString("unable to parse svg (2): %1 %2 %3").arg(parseResult2.errorMessage).arg(parseResult2.errorLine).arg(parseResult2.errorColumn));
		return;
	}

	// gornTree() rewrites every id in @p svgDocument to an index path ("0", "0.0", "0.1.0.0", ...),
	// stashing the real id in @c oldid, and removeGorn() reverses that on the serialised string before
	// it is written. That is fine for its original purpose (undo needs stable ids across structural
	// edits), but the command layer -- addPrimitive / moveGraphic / resizeGraphic / removePrimitive --
	// reads and writes this SAME document and matches elements by id. Left gorn'd, the editor's
	// authoritative document and the file it produces do not share an id space, which is what made the
	// part editor feel dead: layer routing looked up 'silkscreen' and found nothing (the live tree was
	// '0.1'), and PEGraphicsItems were built from gorn'd ids so every highlight/move trace reported ids
	// like '0.3' instead of the element the user actually clicked.
	//
	// Gorn only the throwaway copy the overlay items are derived from, and keep the live document in
	// canonical id space. The items carry QDomElement handles into this document, so they must be built
	// from it -- hence re-applying gorn per element id is avoided entirely: PEElement/PEDocument are
	// re-read from the canonical tree below.

	// P1: drive the per-element overlay items off the typed model. PEDocument walks the
	// DOM depth-first / document-order (same order the previous manual traverse produced);
	// each modelled (non-Unknown) element becomes one PEGraphicsItem, keyed by id, exactly
	// as before. Behavior (item set, z-order, positions, signals) is unchanged.
	//
	// M0.2 (non-destructive transform handling): the view is built straight over svgDocument and the
	// group transform is NO LONGER baked into the elements. flattenInPlace() here used to rewrite every
	// leaf's geometry attrs and strip every ancestor <g transform> -- on the very QDomDocument that
	// gets serialized on save. Merely opening a part and saving it therefore rewrote the library file,
	// and lossily: a <circle> under a non-translation lost its r, and every <path> was re-encoded to
	// absolute. It also could not be undone, since it ran outside the undo stack at load.
	//
	// Instead the transform is carried per item (PEGraphicsItem::setAbsoluteTransform) as a display and
	// hit-test property, exactly like its existing m_offset/m_docBounds. Geometry attrs stay local, the
	// document round-trips byte-for-byte, and groups keep their transforms so group-as-unit selection
	// stays possible. Where a transform is non-identity the item maps through it; the overwhelming
	// majority of Fritzing part SVGs carry none, so hasTransformedContent() keeps them on the existing
	// flat path.
	PEDocument typed;
	typed.build(svgDocument);
	const bool needTransformMapping = typed.hasTransformedContent();

	QHash<QString, PEGraphicsItem *> pegiHash;

	const QList<PEElement> & elements = typed.elements();
	int traceMade = 0, traceSkipped = 0;
	for (const PEElement & pe : elements) {
		// An unmodelled tag (the old 'else continue') produces no overlay item.
		if (pe.kind == PEElementKind::Unknown) continue;

		QDomElement element = pe.element;
		QRectF bounds = getPixelBounds(element);
		// M0.2: getPixelBounds reads the element's own (local) attrs, so under a non-identity transform
		// the box has the right size but the wrong place. Map it to absolute user units -- the space the
		// scene, the item position and the grips all live in. This is what the destructive flatten used
		// to achieve by rewriting the attrs.
		if (needTransformMapping) {
			const QTransform abs = typed.absoluteTransform(element.attribute("id"));
			if (!abs.isIdentity()) {
				bounds = abs.mapRect(bounds);
			}
		}
		// known Qt bug: boundsOnElement returns zero width and height for text elements.
		if (bounds.width() > 0 && bounds.height() > 0) {
			// Connector pads and nonconnector terminals are the part's electrical geometry, not free
			// art: they must remain selectable but not freely movable/resizable (see makePegi). Detected
			// by the same id conventions the rest of the file uses (connectorNpad / nonconnN).
			const QString eid = element.attribute("id");
			const bool isConnectorGeometry =
				eid.startsWith(QLatin1String("nonconn"))
				|| (eid.startsWith(QLatin1String("connector")) && eid.contains(QLatin1String("pad")));
			PEGraphicsItem * pegi = makePegi(bounds.size(), bounds.topLeft(), itemBase, element, z++, !isConnectorGeometry);
			if (pegi == nullptr) continue;   // M0.4: no live item without a real backing graphic
			// (makePegi now sets docBounds + interactive for every kind; path node grips are enabled inside
			// setInteractive, so no per-kind special-casing is needed here any more.)
			if (needTransformMapping) {
				pegi->setAbsoluteTransform(typed.absoluteTransform(element.attribute("id")));
			}
			pegiHash.insert(element.attribute("id"), pegi);
			++traceMade;
		} else {
			++traceSkipped;
		}
	}

	// PE_TRACE (temporary): the single most common "nothing happened" cause is an element that lands in
	// the document but never gets a live item (getPixelBounds returns a null rect for path/polyline/
	// polygon/text), plus items positioned far outside the visible scene rect.
	DebugDialog::debug(QString("PE_TRACE initSvgTree view=%1 itemsMade=%2 skippedZeroBounds=%3 itemBasePos=(%4,%5) scene=%6")
	                       .arg(sketchWidget->viewID())
	                       .arg(traceMade).arg(traceSkipped)
	                       .arg(itemBase->pos().x()).arg(itemBase->pos().y())
	                       .arg(sketchWidget->scene()->items().count()));

	// FRESH-LOOK DIAGNOSTIC: enumerate every svg-rendering item in this scene with z-order, visibility
	// and renderer identity. If the part is drawn by more than one QGraphicsSvgItem (e.g. layerKin
	// copper0/copper1 children alongside itemBase), that competing renderer is the "old way laid over
	// our work" and it names itself here rather than via another guess.
	{
		const QList<QGraphicsItem *> all = sketchWidget->scene()->items();
		int svgCount = 0;
		for (QGraphicsItem * it : all) {
			QGraphicsSvgItem * svg = dynamic_cast<QGraphicsSvgItem *>(it);
			if (svg == nullptr) continue;
			++svgCount;
			ItemBase * ib = dynamic_cast<ItemBase *>(svg);
			DebugDialog::debug(QString("PE_TRACE SCENE-SVG #%1 class=%2 z=%3 visible=%4 pos=(%5,%6) rend=%7")
			                       .arg(svgCount)
			                       .arg(ib ? QString("ItemBase(view=%1,layer=%2)").arg(ib->viewID()).arg(ib->viewLayerID()) : QStringLiteral("plainSvgItem"))
			                       .arg(svg->zValue())
			                       .arg(svg->isVisible())
			                       .arg(svg->pos().x()).arg(svg->pos().y())
			                       .arg(svg->renderer() ? QString::number(reinterpret_cast<quintptr>(svg->renderer())) : QStringLiteral("NULL")));
		}
		DebugDialog::debug(QString("PE_TRACE SCENE-SVG total svg items=%1 (expect 1/view; >1 = competing renderer)").arg(svgCount));

		// The user reports a NON-INTERACTIVE old svg sitting ON TOP of the interactive work, with the part's
		// own sizing box peeking out top-left. The svg dump above shows view=3 has exactly 3 ItemBases and no
		// plain overlay -- so the "top" item is one of them. Log which svg item is topmost by z AND whether it
		// is selectable/movable (the interactive ones are our PEGraphicsItems). This identifies, by z-order,
		// the exact item painting over the rest, and confirms whether itemBase itself is the inert top layer.
		{
			QGraphicsItem * top = nullptr;
			for (QGraphicsItem * it : sketchWidget->scene()->items()) {
				if (dynamic_cast<QGraphicsSvgItem *>(it) == nullptr) continue;
				if (!top || it->zValue() > top->zValue()) top = it;
			}
			if (top != nullptr) {
				DebugDialog::debug(QString("PE_TRACE TOPSVG z=%1 selectable=%2 movable=%3 flags=%4")
				                       .arg(top->zValue())
				                       .arg(top->flags().testFlag(QGraphicsItem::ItemIsSelectable))
				                       .arg(top->flags().testFlag(QGraphicsItem::ItemIsMovable))
				                       .arg((int)top->flags()));
			}
			int pegiCount = 0;
			for (QGraphicsItem * it : sketchWidget->scene()->items()) {
				if (dynamic_cast<PEGraphicsItem *>(it) != nullptr) ++pegiCount;
			}
			DebugDialog::debug(QString("PE_TRACE PEGI items in scene=%1 (our interactive overlay count)").arg(pegiCount));
		}
	}

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		QString svgID, terminalID;
		bool ok = ViewLayer::getConnectorSvgIDs(connector, sketchWidget->viewID(), svgID, terminalID);
		if (ok) {
			PEGraphicsItem * terminalItem = pegiHash.value(terminalID, nullptr);
			PEGraphicsItem * pegi = pegiHash.value(svgID);
			if (pegi == nullptr) {
				DebugDialog::debug(QString("missing pegi for svg id %1").arg(svgID));
			}
			else {
				QPointF terminalPoint = pegi->rect().center();
				if (terminalItem != nullptr) {
					terminalPoint = terminalItem->pos() - pegi->pos() + terminalItem->rect().center();
				}
				pegi->setTerminalPoint(terminalPoint);
				pegi->showTerminalPoint(true);
			}
		}

		connector = connector.nextSiblingElement("connector");
	}
	// PIECE 1: keep everything hidden here (reload() end is the single source of truth for visibility).
	if (itemBase != nullptr) {
		itemBase->setVisible(false);
		PaletteItem * pe = dynamic_cast<PaletteItem *>(itemBase);
		if (pe != nullptr) {
			Q_FOREACH (ItemBase * lk, pe->layerKin()) { if (lk) lk->setVisible(false); }
		}
	}
	Q_FOREACH (QGraphicsItem * it, sketchWidget->scene()->items()) {
		if (dynamic_cast<PEGraphicsItem *>(it) != nullptr) it->setVisible(false);
	}
	DebugDialog::debug(QString("PE_TRACE PIECE1 initSvgTree view=%1: all hidden (reload controls visibility)").arg(sketchWidget->viewID()));
}

void PEMainWindow::highlightSlot(PEGraphicsItem * pegi) {
	DebugDialog::debug(QString("PE_TRACE highlightSlot id=%1 interactive=%2")
	                       .arg(pegi ? pegi->element().attribute("id") : QString("NULL"))
	                       .arg(pegi ? pegi->interactive() : false));
	if (m_peToolView != nullptr) {
		bool enableTerminalPointControls = anyMarquee();
		bool vis = anyVisible();
		m_peToolView->enableConnectorChanges(vis && pegi->showingMarquee(), vis && enableTerminalPointControls, m_connectorList.count() > 0, vis);
	}

	if (m_peSvgView != nullptr) {
		m_peSvgView->highlightElement(pegi);
	}
}

void PEMainWindow::initConnectors(bool updateConnectorsView) {
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	m_connectorList.clear();
	while (!connector.isNull()) {
		m_connectorList.append(connector);
		connector = connector.nextSiblingElement("connector");
	}

	std::sort(m_connectorList.begin(), m_connectorList.end(), byID);

	if (updateConnectorsView) {
		m_connectorsView->initConnectors(&m_connectorList);
	}
	m_peToolView->initConnectors(&m_connectorList);
	updateAssignedConnectors();
}

void PEMainWindow::switchedConnector(int ix)
{
	if (m_currentGraphicsView == nullptr) return;

	switchedConnector(ix, m_currentGraphicsView);
}

void PEMainWindow::switchedConnector(int ix, SketchWidget * sketchWidget)
{
	if (m_connectorList.count() == 0) return;

	QDomElement element = m_connectorList.at(ix);
	if (element.isNull()) return;

	QString svgID, terminalID;
	if (!ViewLayer::getConnectorSvgIDs(element, sketchWidget->viewID(), svgID, terminalID)) return;

	QList<PEGraphicsItem *> pegiList = getPegiList(sketchWidget);
	bool gotOne = false;
	Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
		QDomElement pegiElement = pegi->element();
		if (pegiElement.attribute("id").compare(svgID) == 0) {
			gotOne = true;
			pegi->showMarquee(true);
			pegi->setHighlighted(true);
			m_peToolView->setTerminalPointCoords(pegi->terminalPoint());
			m_peToolView->setTerminalPointLimits(pegi->rect().size());
			break;
		}
	}

	if (!gotOne) {
		Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
			pegi->showMarquee(false);
			pegi->setHighlighted(false);
		}
	}
}

void PEMainWindow::loadImage()
{
	if (m_currentGraphicsView == nullptr) return;

	QStringList extras;
	extras.append("");
	extras.append("");
	QString imageFiles;
	if (m_currentGraphicsView->viewID() == ViewLayer::PCBView) {
		imageFiles = tr("Image & Footprint Files (%1 %2 %3 %4 %5);;SVG Files (%1);;JPEG Files (%2);;PNG Files (%3);;gEDA Footprint Files (%4);;Kicad Module Files (%5)");   //
		extras[0] = "*.fp";
		extras[1] = "*.mod";
	}
	else {
		imageFiles = tr("Image Files (%1 %2 %3);;SVG Files (%1);;JPEG Files (%2);;PNG Files (%3)%4%5");
	}

	/*
	if (m_currentGraphicsView->viewID() == ViewLayer::SchematicView) {
		extras[0] = "*.lib";
		imageFiles = tr("Image & Footprint Files (%1 %2 %3 %4);;SVG Files (%1);;JPEG Files (%2);;PNG Files (%3);;Kicad Schematic Files (%4)%5");   //
	}
	*/

	QString initialPath = FolderUtils::openSaveFolder();
	ViewThing * viewThing = m_viewThings.value(m_currentGraphicsView->viewID());
	ItemBase * itemBase = viewThing->itemBase;
	if (itemBase != nullptr) {
		initialPath = itemBase->filename();
	}
	QString origPath = FolderUtils::getOpenFileName(this,
	                   tr("Open Image"),
	                   initialPath,
	                   imageFiles.arg("*.svg").arg("*.jpg *.jpeg").arg("*.png").arg(extras[0]).arg(extras[1])
	                                               );

	if (origPath.isEmpty()) {
		return; // Cancel pressed
	}

	QString newReferenceFile;
	QString svg;
	if (origPath.endsWith(".svg")) {
		newReferenceFile = getSvgReferenceFile(origPath);
		QFile origFile(origPath);
		if (!origFile.open(QFile::ReadOnly)) {
			QMessageBox::warning(nullptr, tr("Conversion problem", "dialog title"), tr("Unable to load '%1'").arg(origPath));
			return;
		}

		svg = origFile.readAll();
		origFile.close();
		if (svg.contains("coreldraw", Qt::CaseInsensitive) && svg.contains("cdata", Qt::CaseInsensitive)) {
			QMessageBox::warning(nullptr, tr("Conversion problem", "dialog title"),
			                     tr("The SVG file '%1' appears to have been exported from CorelDRAW without the 'presentation attributes' setting. ").arg(origPath) +
			                     tr("Please re-export the SVG file using that setting, and try loading again.")
			                    );
			return;
		}

		QStringList availFonts = InstalledFonts::InstalledFontsList.values();
		if (availFonts.count() > 0) {
			QString destFont = availFonts.at(0);
			Q_FOREACH (QString f, availFonts) {
				if (f.contains("droid", Qt::CaseInsensitive)) {
					destFont = f;
					break;
				}
			}
			bool reallyFixed = false;
			TextUtils::fixFonts(svg, destFont, reallyFixed);
			if (reallyFixed) {
				QMessageBox::information(nullptr, tr("Fonts", "dialog title"),
				                         tr("Fritzing currently only supports OCRA and Droid fonts--these have been substituted in for the fonts in '%1'").arg(origPath));
			}
		}
	}
	else {
		newReferenceFile = origPath;
		if (origPath.endsWith("png") || origPath.endsWith("jpg") || origPath.endsWith("jpeg")) {
			QString message = tr("You may use a PNG or JPG image to construct your part, but it is better to use an SVG. "
			                     "PNG and JPG images retain their nature as bitmaps and do not look good when scaled--"
			                     "so for Fritzing parts it is best to use PNG and JPG only as placeholders.");

			QMessageBox::information(nullptr, tr("Use of PNG and JPG discouraged", "dialog title"), message);

		}

		try {
			svg = createSvgFromImage(origPath);
		}
		catch (const QString & msg) {
			QMessageBox::warning(nullptr, tr("Conversion problem", "dialog title"), tr("Unable to load image file '%1':\n\n%2").arg(origPath).arg(msg));
			return;
		}
	}

	if (svg.isEmpty()) {
		QMessageBox::warning(nullptr, tr("Conversion problem", "dialog title"), tr("Unable to load image file '%1'").arg(origPath));
		return;
	}

	QDomDocument doc;
	QDomDocument::ParseResult parseResult = doc.setContent(svg.toUtf8());
	if (!parseResult) {
		QMessageBox::warning(nullptr, tr("SVG problem", "dialog title"), tr("Unable to parse '%1': %2 line:%3 column:%4").arg(origPath).arg(parseResult.errorMessage).arg(parseResult.errorLine).arg(parseResult.errorColumn));
		return;
	}

	if (m_currentGraphicsView == m_pcbGraphicsView) {
		QDomElement root = doc.documentElement();
		QDomElement check = TextUtils::findElementWithAttribute(root, "id", "copper1");
		if (check.isNull()) {
			check = TextUtils::findElementWithAttribute(root, "id", "copper0");
		}
		if (check.isNull()) {
			QString message = tr("There are no copper layers defined in: %1. "
			                     "See <a href=\"http://fritzing.org/learning/tutorials/creating-custom-parts/providing-part-graphics/\">this explanation</a>."
			                     "<br/><br/>This will not be a problem in the next release of the Parts Editor, "
			                     "but for now please modify the file according to the instructions in the link.").arg(origPath);

			QMessageBox::warning(nullptr, tr("SVG problem", "dialog title"), message);
			return;
		}
	}

	TextUtils::fixMuch(svg, true);
	insertDesc(newReferenceFile, svg);
	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(m_currentGraphicsView);
	FolderUtils::ensureDirectoryExists(newPath);
	bool success = writeXml(newPath, removeGorn(svg), true);
	if (!success) {
		QMessageBox::warning(nullptr, tr("Copy problem", "dialog title"), tr("Unable to make a local copy of: '%1'").arg(origPath));
		return;
	}

	QFileInfo info(origPath);
	auto * parentCommand = new QUndoCommand(QString("Load '%1'").arg(info.fileName()));

	QString oldPath;
	if (itemBase != nullptr) {
		oldPath = itemBase->filename();
	}
	new ChangeSvgCommand(this, m_currentGraphicsView, oldPath, newPath, parentCommand);
	m_undoStack->waitPush(parentCommand, SketchWidget::PropChangeDelay);
}

QString PEMainWindow::createSvgFromImage(const QString &origFilePath) {
	if (origFilePath.endsWith(".fp")) {
		// this is a geda footprint file
		GedaElement2Svg geda;
		QString svg = geda.convert(origFilePath, false);
		return svg;
	}

	if (origFilePath.endsWith(".lib")) {
		// Kicad schematic library file
		QStringList defs = KicadSchematic2Svg::listDefs(origFilePath);
		if (defs.count() == 0) {
			throw tr("no schematics found in %1").arg(origFilePath);
		}

		QString def;
		if (defs.count() > 1) {
			KicadModuleDialog kmd(tr("schematic part"), origFilePath, defs, this);
			int result = kmd.exec();
			if (result != QDialog::Accepted) {
				return "";
			}

			def = kmd.selectedModule();
		}
		else {
			def = defs.at(0);
		}

		KicadSchematic2Svg kicad;
		QString svg = kicad.convert(origFilePath, def);
		return svg;
	}

	if (origFilePath.endsWith(".mod")) {
		// Kicad footprint (Module) library file
		QStringList modules = KicadModule2Svg::listModules(origFilePath);
		if (modules.count() == 0) {
			throw tr("no footprints found in %1").arg(origFilePath);
		}

		QString module;
		if (modules.count() > 1) {
			KicadModuleDialog kmd("footprint", origFilePath, modules, this);
			int result = kmd.exec();
			if (result != QDialog::Accepted) {
				return "";
			}

			module = kmd.selectedModule();
		}
		else {
			module = modules.at(0);
		}

		KicadModule2Svg kicad;
		QString svg = kicad.convert(origFilePath, module, false);
		return svg;
	}

	// deal with png, jpg:
	QBuffer buffer;
	QImage img(origFilePath);
	QSvgGenerator svgGenerator;
	svgGenerator.setResolution(90);
	svgGenerator.setOutputDevice(&buffer);
	QSize sz = img.size();
	svgGenerator.setSize(sz);
	svgGenerator.setViewBox(QRect(0, 0, sz.width(), sz.height()));
	QPainter svgPainter(&svgGenerator);
	svgPainter.drawImage(QPoint(0,0), img);
	svgPainter.end();

	return QString(buffer.buffer());
}

/*
QString PEMainWindow::makeSvgPath(const QString & referenceFile, SketchWidget * sketchWidget, bool useIndex)
{
    QString rf = referenceFile;
    if (!rf.isEmpty()) rf += "_";
    QString viewName = ViewLayer::viewIDNaturalName(sketchWidget->viewID());
    QString indexString;
    if (useIndex) indexString = QString("_%3").arg(m_fileIndex++);
    return QString("%1/%2%3_%1%4.svg").arg(viewName).arg(rf).arg(m_guid).arg(indexString);
}
*/

QString PEMainWindow::makeSvgPath2(SketchWidget * sketchWidget)
{
	QString viewName = ViewLayer::viewIDNaturalName(sketchWidget->viewID());
	return QString("%1/%2_%3_%4_%1.svg").arg(viewName).arg(m_prefix).arg(m_guid).arg(m_fileIndex);
}

void PEMainWindow::changeSvg(SketchWidget * sketchWidget, const QString & filename, int changeDirection) {
	QDomElement fzpRoot = m_fzpDocument.documentElement();
	setImageAttribute(fzpRoot, filename, sketchWidget->viewID());

	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		if (viewThing->itemBase != nullptr) {
			Q_FOREACH(ItemBase * lk, viewThing->itemBase->layerKin()) {
				delete lk;
			}
		}
		delete viewThing->itemBase;
		viewThing->itemBase = nullptr;
	}

	updateChangeCount(sketchWidget, changeDirection);

	reload(false);
}

QString PEMainWindow::makeDirName() {
	return "fritzing_pe_" + m_guid;
}

QString PEMainWindow::saveFzp() {
	QDir dir = QDir::temp();
	QString dirName = makeDirName();
	dir.mkdir(dirName);
	dir.cd(dirName);
	QString fzpPath = dir.absoluteFilePath(QString("%1_%2_%3.fzp").arg(m_prefix).arg(m_guid).arg(m_fileIndex++));
	DebugDialog::debug("temp path " + fzpPath);
	writeXml(fzpPath, m_fzpDocument.toString(), true);
	return fzpPath;
}

void PEMainWindow::reload(bool firstTime)
{
	Q_UNUSED(firstTime);

	SvgCursorBuilder::instance()->addCursor(this, Qt::WaitCursor);

	QList<ItemBase *> toDelete;

	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		if (viewThing->sketchWidget == nullptr) continue;

		Q_FOREACH (QGraphicsItem * item, viewThing->sketchWidget->scene()->items()) {
			auto * itemBase = dynamic_cast<ItemBase *>(item);
			if (itemBase != nullptr) toDelete << itemBase;
		}
	}

	Q_FOREACH (ItemBase * itemBase, toDelete) {
		delete itemBase;
	}

	killPegi();

	QString fzpPath = saveFzp();   // needs a document somewhere to set up connectors--not part of the undo stack
	auto * modelPart = new ModelPart(m_fzpDocument, fzpPath, ModelPart::Part);

	long newID = ItemBase::getNextID();
	ViewGeometry viewGeometry;
	viewGeometry.setLoc(QPointF(0, 0));

	QList<ItemBase *> itemBases;
	itemBases << m_iconGraphicsView->addItem(modelPart, m_iconGraphicsView->defaultViewLayerPlacement(modelPart), BaseCommand::SingleView, viewGeometry, newID, -1, nullptr);
	itemBases <<  m_breadboardGraphicsView->addItem(modelPart, m_breadboardGraphicsView->defaultViewLayerPlacement(modelPart), BaseCommand::SingleView, viewGeometry, newID, -1, nullptr);
	itemBases <<  m_schematicGraphicsView->addItem(modelPart, m_schematicGraphicsView->defaultViewLayerPlacement(modelPart), BaseCommand::SingleView, viewGeometry, newID, -1, nullptr);
	itemBases <<  m_pcbGraphicsView->addItem(modelPart, m_pcbGraphicsView->defaultViewLayerPlacement(modelPart), BaseCommand::SingleView, viewGeometry, newID, -1, nullptr);

	Q_FOREACH (ItemBase * itemBase, itemBases) {
		if (itemBase == nullptr) continue;
		ViewThing * viewThing = m_viewThings.value(itemBase->viewID());
		viewThing->itemBase = itemBase;
		viewThing->referenceFile = getSvgReferenceFile(itemBase->filename());
	}

	m_metadataView->initMetadata(m_fzpDocument);

	QList<QWidget *> widgets;
	widgets << m_metadataView << m_peToolView << m_connectorsView;
	Q_FOREACH (QWidget * widget, widgets) {
		QList<QLineEdit *> lineEdits = widget->findChildren<QLineEdit *>();
		Q_FOREACH (QLineEdit * lineEdit, lineEdits) {
			lineEdit->installEventFilter(this);
		}
		QList<QTextEdit *> textEdits = widget->findChildren<QTextEdit *>();
		Q_FOREACH (QTextEdit * textEdit, textEdits) {
			textEdit->installEventFilter(this);
		}
	}

	if (m_currentGraphicsView != nullptr) {
		showing(m_currentGraphicsView);
	}

	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		initSvgTree(viewThing->sketchWidget, viewThing->itemBase, *viewThing->document);
	}

	initConnectors(true);
	m_connectorsView->setSMD(modelPart->flippedSMD());

	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		if (viewThing->itemBase == nullptr) { continue; }
		// The whole-part QtSvg item (itemBase) is a BACKDROP, not an editable object. Left interactive and
		// on its default z it intercepts every click and paints the full part over the per-element overlay
		// (PEGraphicsItem) -- the user's "old SVG with zero interaction sitting on top, our work hidden
		// underneath it, sizing box top-left." (These were commented out by the original author; the symptom
		// proves they must be on.) Strip all mouse interaction + selection and push it BELOW the overlay items
		// so the live PEGraphicsItems are the top, clickable layer. The backdrop still paints (so the part is
		// visible) but never steals a click nor draws over a selected/live element.
		viewThing->itemBase->setAcceptHoverEvents(false);
		viewThing->itemBase->setEnabled(false);
		viewThing->itemBase->setFlag(QGraphicsItem::ItemIsSelectable, false);
		viewThing->itemBase->setFlag(QGraphicsItem::ItemIsMovable, false);
		viewThing->itemBase->setFlag(QGraphicsItem::ItemIsFocusable, false);
		viewThing->itemBase->setZValue(-1000.0);   // strictly under the pegi overlay (PegiZ = 5000)
		viewThing->itemBase->setAcceptsMousePressLegEvent(false);
		viewThing->itemBase->setSwappable(false);
		// The backdrop's per-layer layerKin siblings (Copper0 / Silkscreen1 for a PCB part) are SEPARATE
		// QGraphicsSvgItems with their own z -- setZValue/flags on the chief do NOT propagate to them, so
		// they would still sit above the overlay and keep the same "old svg on top" symptom. Apply the exact
		// same inert-backdrop treatment to every member of the family.
		{
			PaletteItem * pe = dynamic_cast<PaletteItem *>(viewThing->itemBase);
			if (pe != nullptr) {
				Q_FOREACH (ItemBase * lk, pe->layerKin()) {
					if (lk == nullptr) continue;
					lk->setAcceptHoverEvents(false);
					lk->setEnabled(false);
					lk->setFlag(QGraphicsItem::ItemIsSelectable, false);
					lk->setFlag(QGraphicsItem::ItemIsMovable, false);
					lk->setFlag(QGraphicsItem::ItemIsFocusable, false);
					lk->setZValue(-1000.0);
				}
			}
		}
		viewThing->sketchWidget->hideConnectors(true);
		viewThing->everZoomed = false;
	}

	switchedConnector(m_peToolView->currentConnectorIndex());

	// processEventBlocker might be enough?
	QTimer::singleShot(10, this, SLOT(initZoom()));

	SvgCursorBuilder::instance()->removeCursor(this);

	// PIECE 2: show EVERY view's own svg items -- the chief + its layerKin siblings on each view (PCB has 3
	// renderers: Copper0/Copper1/Silkscreen1; icon/breadboard/schematic have their own set). So each tab now
	// paints its own part render. ALL PEGraphicsItem overlay items stay hidden -- interactivity is added next,
	// piece by piece, so a rendering regression can never be confused with an interaction one.
	{
		int shown = 0;
		Q_FOREACH (ViewThing * vt, m_viewThings.values()) {
			const bool show = (vt->sketchWidget != nullptr);

			if (vt->itemBase != nullptr) {
				vt->itemBase->setVisible(show);
				if (show) ++shown;
				PaletteItem * pe = dynamic_cast<PaletteItem *>(vt->itemBase);
				if (pe != nullptr) {
					Q_FOREACH (ItemBase * lk, pe->layerKin()) {
						if (lk == nullptr) continue;
						lk->setVisible(show);
						if (show) ++shown;
					}
				}
			}
			if (vt->sketchWidget != nullptr) {
				Q_FOREACH (QGraphicsItem * it, vt->sketchWidget->scene()->items()) {
					if (dynamic_cast<PEGraphicsItem *>(it) != nullptr) it->setVisible(false);
				}
			}
		}
		DebugDialog::debug(QString("PE_TRACE PIECE2: svgItemsShown=%1 across all views (overlay items hidden)").arg(shown));
	}

}

void PEMainWindow::busModeChanged(bool state) {
	if (m_currentGraphicsView == nullptr) return;

	if (!state) {
		Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
			viewThing->busMode = false;
			viewThing->sketchWidget->hideConnectors(true);
		}

		deleteBuses();

		reload(false);
		return;
	}

	QList<PEGraphicsItem *> pegiList = getPegiList(m_currentGraphicsView);

	reload(false);

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");

	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		// items on pegiList no longer exist after reload so get them now
		pegiList = getPegiList(viewThing->sketchWidget);
		viewThing->sketchWidget->hideConnectors(true);
		Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
			pegi->setVisible(false);
		}

		// show connectorItems that have connectors assigned
		QStringList connectorIDs;
		QDomElement connector = connectors.firstChildElement("connector");
		while (!connector.isNull()) {
			QDomElement p = ViewLayer::getConnectorPElement(connector, viewThing->sketchWidget->viewID());
			QString id = p.attribute("svgId");
			Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
				QDomElement pegiElement = pegi->element();
				if (pegiElement.attribute("id").compare(id) == 0) {
					connectorIDs.append(connector.attribute("id"));
					break;
				}
			}
			connector = connector.nextSiblingElement("connector");
		}

		if (connectorIDs.count() > 0) {
			viewThing->itemBase->showConnectors(connectorIDs);
		}
	}

	displayBuses();
	m_peToolView->enableConnectorChanges(false, false, m_connectorList.count() > 0, false);
}

void PEMainWindow::pickModeChanged(bool state) {
	if (m_currentGraphicsView == nullptr) return;

	// never actually get state == false;
	m_inPickMode = state;
	if (m_inPickMode) {
		QApplication::setOverrideCursor(Qt::PointingHandCursor);
		Q_FOREACH (QGraphicsItem * item, m_currentGraphicsView->scene()->items()) {
			auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
			if (pegi != nullptr) pegi->setPickAppearance(true);
		}
		qApp->installEventFilter(this);
	}
}

void PEMainWindow::pegiTerminalPointMoved(PEGraphicsItem * pegi, QPointF p)
{
	// called while terminal point is being dragged, no need for an undo operation

	Q_UNUSED(pegi);
	m_peToolView->setTerminalPointCoords(p);
}

void PEMainWindow::pegiTerminalPointChanged(PEGraphicsItem * pegi, QPointF before, QPointF after)
{
	// called at the end of terminal point dragging

	terminalPointChangedAux(pegi, before, after);
}

void PEMainWindow::pegiMousePressed(PEGraphicsItem * pegi, bool & ignore)
{
	ignore = false;

	if (m_useNextPick) {
		m_useNextPick = false;
		relocateConnector(pegi);
		return;
	}

	ViewThing * viewThing = m_viewThings.value(m_currentGraphicsView->viewID());
	if (pegi->itemBase() != viewThing->itemBase) {
		ignore = true;
		return;
	}

	QString id = pegi->element().attribute("id");
	if (id.isEmpty()) return;

	int index = m_peToolView->currentConnectorIndex();
	if (index >= 0) {
		QDomElement current = m_connectorList.at(index);
		if (current.attribute("id").compare(id) == 0) {
			// already there
			return;
		}
	}

	// if a connector has been clicked, make it the current connector
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		QDomElement p = ViewLayer::getConnectorPElement(connector, m_currentGraphicsView->viewID());
		if (p.attribute("svgId") == id || p.attribute("terminalId") == id) {
			m_peToolView->setCurrentConnector(connector);
			return;
		}
		connector = connector.nextSiblingElement("connector");
	}
}

void PEMainWindow::pegiMouseReleased(PEGraphicsItem *) {
}

void PEMainWindow::relocateConnector(PEGraphicsItem * pegi)
{
	// The svg is no longer gorn'd for the lifetime of the edit session (see initSvgTree), so elements are
	// addressed by their real id. The gorn indirection existed to keep ids stable across structural
	// undo; connector relocation only needs to name two existing elements, which their ids already do.
	QString newGorn = pegi->element().attribute("id");
	QDomDocument * svgDoc = m_viewThings.value(m_currentGraphicsView->viewID())->document;
	QDomElement svgRoot = svgDoc->documentElement();
	QDomElement newGornElement = TextUtils::findElementWithAttribute(svgRoot, "id", newGorn);
	if (newGornElement.isNull()) {
		return;
	}

	QDomElement fzpRoot = m_fzpDocument.documentElement();
	QDomElement connectors = fzpRoot.firstChildElement("connectors");
	QDomElement currentConnectorElement = m_connectorList.at(m_peToolView->currentConnectorIndex());
	QString svgID, terminalID;
	if (!ViewLayer::getConnectorSvgIDs(currentConnectorElement, m_currentGraphicsView->viewID(), svgID, terminalID)) {
		return;
	}

	QDomElement oldGornElement = TextUtils::findElementWithAttribute(svgRoot, "id", svgID);
	QString oldGorn = oldGornElement.attribute("id");
	QString oldGornTerminal;
	if (!terminalID.isEmpty()) {
		QDomElement element = TextUtils::findElementWithAttribute(svgRoot, "id", terminalID);
		oldGornTerminal = element.attribute("id");
	}

	if (newGornElement.attribute("id").compare(oldGornElement.attribute("id")) == 0) {
		// no change
		return;
	}

	auto * rcsc = new RelocateConnectorSvgCommand(this, m_currentGraphicsView, svgID, terminalID, oldGorn, oldGornTerminal, newGorn, "", nullptr);
	rcsc->setText(tr("Relocate connector %1").arg(currentConnectorElement.attribute("name")));
	m_undoStack->waitPush(rcsc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::relocateConnectorSvg(SketchWidget * sketchWidget, const QString & svgID, const QString & terminalID,
                                        const QString & oldGorn, const QString & oldGornTerminal, const QString & newGorn, const QString & newGornTerminal,
                                        int changeDirection)
{
	ViewLayer::ViewID viewID = sketchWidget->viewID();
	ViewThing * viewThing = m_viewThings.value(viewID);
	QDomDocument * svgDoc = viewThing->document;
	QDomElement svgRoot = svgDoc->documentElement();

	QDomElement oldGornElement = TextUtils::findElementWithAttribute(svgRoot, "id", oldGorn);
	QDomElement oldGornTerminalElement;
	if (!oldGornTerminal.isEmpty()) {
		oldGornTerminalElement = TextUtils::findElementWithAttribute(svgRoot, "id", oldGornTerminal);
	}
	QDomElement newGornElement = TextUtils::findElementWithAttribute(svgRoot, "id", newGorn);
	QDomElement newGornTerminalElement;
	if (!newGornTerminal.isEmpty()) {
		newGornTerminalElement = TextUtils::findElementWithAttribute(svgRoot, "id", newGornTerminal);
	}

	if (!oldGornElement.isNull()) {
		oldGornElement.removeAttribute("id");
		oldGornElement.removeAttribute("oldid");            // remove oldid so removeGorn() call later doesn't restore anything by accident
	}
	if (!oldGornTerminalElement.isNull()) {
		oldGornTerminalElement.removeAttribute("id");
		oldGornTerminalElement.removeAttribute("oldid");
	}
	if (!newGornElement.isNull()) {
		newGornElement.setAttribute("id", svgID);
		newGornElement.removeAttribute("oldid");
	}
	if (!newGornTerminalElement.isNull()) {
		newGornTerminalElement.setAttribute("id", terminalID);
		newGornTerminalElement.removeAttribute("oldid");
	}

	QDomElement fzpRoot = m_fzpDocument.documentElement();
	QDomElement connectors = fzpRoot.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		QString cSvgID, cTerminalID;
		if (ViewLayer::getConnectorSvgIDs(connector, viewID, cSvgID, cTerminalID)) {
			if (cSvgID == svgID) break;
		}
		connector = connector.nextSiblingElement("connector");
	}
	if (connector.isNull()) return;

	QDomElement p = ViewLayer::getConnectorPElement(connector, viewID);
	if (terminalID.isEmpty()) {
		p.removeAttribute("terminalId");
	}
	else {
		p.setAttribute("terminalId", terminalID);
	}

	// update svg in case there is a subsequent call to reload
	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(sketchWidget);
	FolderUtils::ensureDirectoryExists(newPath);
	QString svg = TextUtils::svgNSOnly(svgDoc->toString());
	writeXml(newPath, removeGorn(svg), true);
	setImageAttribute(fzpRoot, newPath, viewID);

	Q_FOREACH (QGraphicsItem * item, sketchWidget->scene()->items()) {
		auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
		if (pegi == nullptr) continue;

		QDomElement element = pegi->element();
		if (element.attribute("gorn").compare(newGorn) == 0) {
			pegi->setHighlighted(true);
			pegi->showMarquee(true);
			pegi->showTerminalPoint(true);
			switchedConnector(m_peToolView->currentConnectorIndex(), sketchWidget);

		}
		else if (element.attribute("gorn").compare(oldGorn) == 0) {
			pegi->showTerminalPoint(false);
			pegi->showMarquee(false);
		}
	}

	updateAssignedConnectors();
	updateChangeCount(sketchWidget, changeDirection);
}

bool PEMainWindow::save() {
	if (!canSave()) {
		return saveAs(false);
	}

	return saveAs(true);
}

bool PEMainWindow::saveAs() {
	return saveAs(false);
}

// Next version for the save-gate "bump" checkbox: integer "4" -> "5"; dotted "1.1" -> "1.2" (bump the
// last numeric run); empty -> "1"; otherwise unchanged.
static QString bumpedVersion(const QString & current) {
	QString v = current.trimmed();
	if (v.isEmpty()) return QStringLiteral("1");
	bool ok = false;
	int n = v.toInt(&ok);
	if (ok) return QString::number(n + 1);
	int i = v.length() - 1;
	while (i >= 0 && v.at(i).isDigit()) --i;
	bool tailOk = false;
	int tn = v.mid(i + 1).toInt(&tailOk);
	if (tailOk) return v.left(i + 1) + QString::number(tn + 1);
	return v;
}

bool PEMainWindow::saveAs(bool overWrite)
{
	QStringList peAlienFiles;

	// Save gate: when the part has unsaved changes, offer to record what changed in this
	// revision (and optionally bump the version) before the file is written. A clean part saves with
	// no gate; Cancel aborts the save. Reuses the shared history-entry dialog.
	{
		QDomElement gateRoot = m_fzpDocument.documentElement();
		if (!m_undoStack->isClean()) {
			QString nextVersion = bumpedVersion(gateRoot.firstChildElement("version").text().trimmed());

			QList<HistoryEntry> history = readHistory(gateRoot);
			QString today = QDate::currentDate().toString(Qt::ISODate);
			int todayRow = -1;
			for (int i = history.count() - 1; i >= 0; --i) {
				if (history.at(i).date == today) { todayRow = i; break; }
			}
			HistoryEntry seed;
			if (todayRow >= 0) {
				seed = history.at(todayRow);     // amend today's entry rather than add a duplicate
			} else {
				seed.date = today;
				seed.author = gateRoot.firstChildElement("author").text().trimmed();
				seed.mode = "optional";
			}

			PEHistoryEntryDialog gate(seed, false, nextVersion, this);
			gate.setWindowTitle(tr("Save part"));
			gate.setHeaderText(tr("You haven't recorded what changed in this revision."));
			gate.setSaveGateButtons();
			if (gate.exec() != QDialog::Accepted) return false;   // Cancel: abort the save

			bool domChanged = false;
			if (!gate.skipped() && gate.hasText()) {
				QList<HistoryEntry> newHistory = history;
				if (todayRow >= 0) newHistory[todayRow] = gate.entry();
				else newHistory.append(gate.entry());
				writeHistory(gateRoot, newHistory);    // also mirrors the newest entry into <author>/<date>
				domChanged = true;
			}
			if (gate.bumpVersion()) {
				QDomElement v = gateRoot.firstChildElement("version");
				if (v.isNull()) { v = m_fzpDocument.createElement("version"); gateRoot.appendChild(v); }
				TextUtils::replaceChildText(v, nextVersion);   // preserves the replacedby attribute
				domChanged = true;
			}
			if (domChanged) m_metadataView->initMetadata(m_fzpDocument);
		}
	}

	if (!overWrite) {
		bool ok = false;
		QString prefix = QInputDialog::getText(
		                     this,
		                     tr("Filename prefix"),
		                     tr("<p>Please enter a prefix to help you identify the part files.<br/>"
		                        "The file names will have the form 'PREFIX_%1'.<br/>"
		                        "(It is not necessary to change the proposed prefix, since a unique suffix is always added.)</p>").arg(m_guid),
		                     QLineEdit::Normal,
		                     m_prefix,
		                     &ok
		                 );
		if (!ok || prefix.isEmpty()) return false;
		m_prefix = prefix;
	}

	QDomElement fzpRoot = m_fzpDocument.documentElement();

	QList<MainWindow *> affectedWindows;
	QList<MainWindow *> allWindows;
	if (overWrite) {
		Q_FOREACH (QWidget *widget, QApplication::topLevelWidgets()) {
			auto *mainWindow = qobject_cast<MainWindow *>(widget);
			if (mainWindow == nullptr) continue;

			if (qobject_cast<PEMainWindow *>(mainWindow) != nullptr) continue;

			allWindows.append(mainWindow);
			if (mainWindow->usesPart(m_originalModuleID)) {
				affectedWindows.append(mainWindow);
			}
		}

		if (affectedWindows.count() > 0 && !m_gaveSaveWarning) {
			QMessageBox messageBox(this);
			messageBox.setWindowTitle(tr("Sketch Change Warning", "dialog title"));
			QString message;
			if (affectedWindows.count() == 1) {
				message = tr("The open sketch '%1' uses the part you are editing. ").arg(affectedWindows.first()->windowTitle());
				message += tr("Saving this part will make a change to the sketch that cannot be undone.");
			}
			else {
				QStringList sketchNames;
				for (int i = 0; i < affectedWindows.count(); i++) {
					sketchNames << QString("'%1'").arg(affectedWindows.at(i)->windowTitle());
				}
				QString sketchList;
				if (sketchNames.count() == 2) {
					sketchList = tr("%1 and %2").arg(sketchNames.at(0), sketchNames.at(1));
				} else {
					QString last = sketchNames.takeLast();
					sketchList = sketchNames.join(", ") + tr(", and %1").arg(last);
				}
				message = tr("The open sketches %1 use the part you are editing. "
				             "Saving this part will make a change to these sketches that cannot be undone.").arg(sketchList);

			}
			message += tr("\n\nGo ahead and save?");

			messageBox.setText(message);
			messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Cancel);
			messageBox.setDefaultButton(QMessageBox::Cancel);
			messageBox.setIcon(QMessageBox::Warning);
			messageBox.setWindowModality(Qt::WindowModal);
			auto answer = (QMessageBox::StandardButton) messageBox.exec();

			if (answer != QMessageBox::Save) {
				return false;
			}

			m_gaveSaveWarning = true;
		}
	}

	QDomElement views = fzpRoot.firstChildElement("views");

	// Determine the moduleID for the save destination subfolder
	QString targetModuleID;
	if (overWrite) {
		targetModuleID = m_originalModuleID;
	} else {
		targetModuleID = QString("%1_%2_%3").arg(m_prefix).arg(m_guid).arg(m_fileIndex);
	}
	targetModuleID = FolderUtils::sanitizeForFolder(targetModuleID);

	// Co-locate SVGs with FZP in the same moduleID subfolder
	QString fzpBasePath = m_userPartsFolderPath + targetModuleID + "/";
	QString svgBasePath = fzpBasePath;

	QHash<ViewLayer::ViewID, QString> svgPaths;

	Q_FOREACH (ViewLayer::ViewID viewID, m_viewThings.keys()) {
		ViewThing * viewThing = m_viewThings.value(viewID);
		QDomElement view = views.firstChildElement(ViewLayer::viewIDXmlName(viewID));
		QDomElement layers = view.firstChildElement("layers");

		QString currentSvgPath = layers.attribute("image");
		svgPaths.insert(viewID, currentSvgPath);

		QMultiHash<ViewLayer::ViewLayerID, QString> extraSvg;
		QDomDocument writeDoc = viewThing->document->cloneNode(true).toDocument();
		QDomElement svgRoot = writeDoc.documentElement();
		double svgWidth, svgHeight, vbWidth, vbHeight;
		bool svgOK = TextUtils::getSvgSizes(writeDoc, svgWidth, svgHeight, vbWidth, vbHeight);
		if (svgOK) {
			QHash<QString, QString> svgHash;
			Q_FOREACH (QGraphicsItem * item, viewThing->sketchWidget->scene()->items()) {
				auto * itemBase = dynamic_cast<ItemBase *>(item);
				if (itemBase == nullptr) continue;

				if (itemBase->layerKinChief() == viewThing->itemBase) continue;

				double factor;
				QString string = itemBase->layerKinChief()->retrieveSvg(itemBase->viewLayerID(), svgHash, false, vbWidth / svgWidth, factor);
				if (!string.isEmpty()) {
					QPointF d = itemBase->pos() - viewThing->itemBase->pos();
					double dx = d.x() * (vbWidth / svgWidth) / GraphicsUtils::SVGDPI;
					double dy = d.y() * (vbWidth / svgWidth) / GraphicsUtils::SVGDPI;
					string = QString("<g transform='translate(%1,%2)'>\n%3\n</g>\n").arg(dx).arg(dy).arg(string);
					extraSvg.insert(itemBase->viewLayerID(), string);
				}
			}
		}


		// deal with copper1 and copper0:
		//      find innermost
		//      don't save twice
		QDomElement copperChild;
		LayerList sketchLayers = viewThing->sketchWidget->viewLayers().keys();
		if (sketchLayers.contains(ViewLayer::Copper0) && sketchLayers.contains(ViewLayer::Copper1)) {
			QDomElement copper0 = TextUtils::findElementWithAttribute(svgRoot, "id", "copper0");
			QDomElement copper1 = TextUtils::findElementWithAttribute(svgRoot, "id", "copper1");
			if (!copper0.isNull() && !copper1.isNull()) {
				QDomElement parent0 = copper0.parentNode().toElement();
				QDomElement parent1 = copper1.parentNode().toElement();
				if (parent0.attribute("id") == "copper1") {
					copperChild = copper0;
				}
				else if (parent1.attribute("id") == "copper0") {
					copperChild = copper1;
				}
			}
			if (!copperChild.isNull()) {
				if (extraSvg.uniqueKeys().contains(ViewLayer::Copper0)) {
					extraSvg.remove(ViewLayer::Copper1);
				}
			}
		}

		Q_FOREACH (ViewLayer::ViewLayerID viewLayerID, extraSvg.uniqueKeys()) {
			QStringList svgList = extraSvg.values(viewLayerID);
			QDomElement svgViewElement = (viewLayerID == ViewLayer::Copper0 && !copperChild.isNull())
			                             ? copperChild
			                             : TextUtils::findElementWithAttribute(svgRoot, "id", ViewLayer::viewLayerXmlNameFromID(viewLayerID));
			if (svgViewElement.isNull()) {
				svgViewElement = svgRoot;
			}

			Q_FOREACH (QString svg, svgList) {
				QDomDocument doc;
				doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing);
				QDomElement root = doc.documentElement();
				Q_FOREACH (ViewLayer::ViewLayerID vlid, sketchLayers) {
					removeID(root, ViewLayer::viewLayerXmlNameFromID(vlid));
				}
				svgViewElement.appendChild(doc.documentElement());
			}
		}

		QString svg = writeDoc.toString();
		insertDesc(viewThing->referenceFile, svg);

		QString svgPath = makeSvgPath2(viewThing->sketchWidget);
		setImageAttribute(layers, svgPath);

		QString actualPath = svgBasePath + svgPath;
		FolderUtils::ensureDirectoryExists(actualPath);
		peAlienFiles << actualPath;
		// DebugDialog::debug(QString("peAlienFiles %1").arg(peAlienFiles));
		bool result = writeXml(actualPath, removeGorn(svg), false);
		if (!result) {
			// TODO: warn user
		}
	}

	QDir dir(fzpBasePath);
	FolderUtils::ensureDirectoryExists(dir.absoluteFilePath("dummy"));

	QString fzpPath;
	if (overWrite) {
		// Keep original filename, save to moduleID subfolder
		fzpPath = dir.absoluteFilePath(QFileInfo(m_originalFzpPath).fileName());
		peAlienFiles << fzpPath;
	} else {
		QString suffix = QString("%1_%2_%3").arg(m_prefix).arg(m_guid).arg(m_fileIndex++);
		fzpPath = dir.absoluteFilePath(QString("%1.fzp").arg(suffix));
		peAlienFiles << fzpPath;
		fzpRoot.setAttribute("moduleId", suffix);
		QString family = m_metadataView->family();
		QString variant = m_metadataView->variant();
		QMultiHash<QString, QString> variants = m_referenceModel->allPropValues(family, "variant");
		QStringList values = variants.values(variant);
		if (values.count() > 0) {
			QString newVariant = makeNewVariant(family);
			QDomElement properties = fzpRoot.firstChildElement("properties");
			replaceProperty("variant", newVariant, properties);
			m_metadataView->resetProperty("variant", newVariant);
		}
	}

	bool result = writeXml(fzpPath, m_fzpDocument.toString(), false);

	// Track old path for migration cleanup (defer deletion until after all updates)
	QString oldFzpPath;
	if (overWrite && fzpPath != m_originalFzpPath) {
		oldFzpPath = m_originalFzpPath;
		m_originalFzpPath = fzpPath;
	}

	if (!overWrite) {
		m_originalFzpPath = fzpPath;
		m_canSave = true;
		m_originalModuleID = fzpRoot.attribute("moduleId");
	}

	// restore the set of working svg files
	Q_FOREACH (ViewLayer::ViewID viewID, m_viewThings.keys()) {
		QString svgPath = svgPaths.value(viewID);
		if (svgPath.isEmpty()) continue;

		QDomElement view = views.firstChildElement(ViewLayer::viewIDXmlName(viewID));
		QDomElement layers = view.firstChildElement("layers");
		setImageAttribute(layers, svgPath);
	}

	ModelPart * modelPart = m_referenceModel->retrieveModelPart(m_originalModuleID);
	if (modelPart == nullptr) {
		modelPart = m_referenceModel->loadPart(fzpPath, true);
		if (modelPart != nullptr) {
			modelPart->setAlien(true);
			Q_EMIT addToMyPartsSignal(modelPart, peAlienFiles);
		} else {
			QMessageBox::critical(nullptr, tr("Parts Editor Error", "dialog title"), tr("The file %2 with prefix %1 was not saved.").arg(m_prefix).arg(fzpPath));
		}
	}
	else {
		m_referenceModel->reloadPart(fzpPath, m_originalModuleID);
		WaitPushUndoStack undoStack;
		auto * parentCommand = new QUndoCommand;
		Q_FOREACH (MainWindow * mainWindow, affectedWindows) {
			mainWindow->updateParts(m_originalModuleID, parentCommand);
		}
		Q_FOREACH (MainWindow * mainWindow, allWindows) {
			mainWindow->updatePartsBin(m_originalModuleID);
		}
		undoStack.push(parentCommand);
	}

	m_autosaveNeeded = false;

	// Flush any pending delayed commands before marking the stack clean.
	// PE commands use waitPush() with a 100ms delay; if the user saves
	// before the timer fires, the command would be pushed after setClean(),
	// causing a false modification state.
	m_undoStack->waitForTimers();
	m_undoStack->setClean();

	// Delete old file after all updates are complete
	if (!oldFzpPath.isEmpty()) {
		QFile::remove(oldFzpPath);
	}

	setWindowModified(false);

	return result;
}


void PEMainWindow::updateChangeCount(SketchWidget * sketchWidget, int changeDirection) {
	ViewThing * viewThing = m_viewThings.value(sketchWidget->viewID());
	viewThing->svgChangeCount += changeDirection;
}

PEGraphicsItem * PEMainWindow::findConnectorItem()
{
	if (m_currentGraphicsView == nullptr) return nullptr;

	QDomElement connector = m_connectorList.at(m_peToolView->currentConnectorIndex());
	if (connector.isNull()) return nullptr;

	QString svgID, terminalID;
	bool ok = ViewLayer::getConnectorSvgIDs(connector, m_currentGraphicsView->viewID(), svgID, terminalID);
	if (!ok) return nullptr;

	QList<PEGraphicsItem *> pegiList = getPegiList(m_currentGraphicsView);
	Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
		if (pegi->element().attribute("id") == svgID) return pegi;
	}

	return nullptr;
}

void PEMainWindow::terminalPointChanged(const QString & how) {
	PEGraphicsItem * pegi = findConnectorItem();
	if (pegi == nullptr) return;

	QRectF r = pegi->rect();
	QPointF p = r.center();
	if (how == "center") {
	}
	else if (how == "N") {
		p.setY(0);
	}
	else if (how == "E") {
		p.setX(r.width());
	}
	else if (how == "S") {
		p.setY(r.height());
	}
	else if (how == "W") {
		p.setX(0);
	}
	terminalPointChangedAux(pegi, pegi->terminalPoint(), p);

	// TODO: UndoCommand which changes fzp xml and svg xml
}

void PEMainWindow::terminalPointChanged(const QString & coord, double value)
{
	PEGraphicsItem * pegi = findConnectorItem();
	if (pegi == nullptr) return;

	QPointF p = pegi->terminalPoint();
	if (coord == "x") {
		p.setX(qMax(0.0, qMin(value, pegi->rect().width())));
	}
	else {
		p.setY(qMax(0.0, qMin(value, pegi->rect().height())));
	}

	terminalPointChangedAux(pegi, pegi->terminalPoint(), p);
}

void PEMainWindow::terminalPointChangedAux(PEGraphicsItem * pegi, QPointF before, QPointF after)
{
	if (pegi->pendingTerminalPoint() == after) {
		return;
	}

	pegi->setPendingTerminalPoint(after);

	QDomElement currentConnectorElement = m_connectorList.at(m_peToolView->currentConnectorIndex());

	auto * mtpc = new MoveTerminalPointCommand(this, this->m_currentGraphicsView, currentConnectorElement.attribute("id"), pegi->rect().size(), before, after, nullptr);
	mtpc->setText(tr("Move terminal point"));
	m_undoStack->waitPush(mtpc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::getSpinAmount(double & amount) {
	double zoom = m_currentGraphicsView->currentZoom() / 100;
	if (zoom == 0) {
		amount = 1;
		return;
	}

	amount = qMin(1.0, 1.0 / zoom);
	if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
		amount *= 10;
	}
}

void PEMainWindow::moveTerminalPoint(SketchWidget * sketchWidget, const QString & connectorID, QSizeF size, QPointF p, int changeDirection)
{
	// TODO: assumes these IDs are unique--need to check if they are not

	QPointF center(size.width() / 2, size.height() / 2);
	bool centered = qAbs(center.x() - p.x()) < .05 && qAbs(center.y() - p.y()) < .05;

	QDomElement fzpRoot = m_fzpDocument.documentElement();
	QDomElement connectorElement = TextUtils::findElementWithAttribute(fzpRoot, "id", connectorID);
	if (connectorElement.isNull()) {
		DebugDialog::debug(QString("missing connector %1").arg(connectorID));
		return;
	}

	QDomElement pElement = ViewLayer::getConnectorPElement(connectorElement, sketchWidget->viewID());
	QString svgID = pElement.attribute("svgId");
	if (svgID.isEmpty()) {
		DebugDialog::debug(QString("Can't find svgId for connector %1").arg(connectorID));
		return;
	}

	PEGraphicsItem * connectorPegi = nullptr;
	QList<PEGraphicsItem *> pegiList = getPegiList(sketchWidget);
	Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
		QDomElement pegiElement = pegi->element();
		if (pegiElement.attribute("id").compare(svgID) == 0) {
			connectorPegi = pegi;
		}
	}
	if (connectorPegi == nullptr) return;

	if (centered) {
		pElement.removeAttribute("terminalId");
		// no need to change SVG in this case
	}
	else {
		ViewThing * viewThing = m_viewThings.value(sketchWidget->viewID());
		QDomDocument * svgDoc = viewThing->document;
		QDomElement svgRoot = svgDoc->documentElement();
		QDomElement svgConnectorElement = TextUtils::findElementWithAttribute(svgRoot, "id", svgID);
		if (svgConnectorElement.isNull()) {
			DebugDialog::debug(QString("Unable to find svg connector element %1").arg(svgID));
			return;
		}

		QString terminalID = pElement.attribute("terminalId");
		if (terminalID.isEmpty()) {
			terminalID = svgID;
			if (terminalID.endsWith("pin") || terminalID.endsWith("pad")) {
				terminalID.chop(3);
			}
			terminalID += "terminal";
			pElement.setAttribute("terminalId", terminalID);
		}

		FSvgRenderer renderer;
		renderer.loadSvg(svgDoc->toByteArray(), "", false);
		QRectF svgBounds = renderer.boundsOnElement(svgID);
		double cx = p.x () * svgBounds.width() / size.width();
		double cy = p.y() * svgBounds.height() / size.height();
		double dx = svgBounds.width() / 1000;
		double dy = svgBounds.height() / 1000;

		QDomElement terminalElement = TextUtils::findElementWithAttribute(svgRoot, "id", terminalID);
		if (terminalElement.isNull()) {
			terminalElement = svgDoc->createElement("rect");
		}
		else if (terminalElement.tagName() != "rect" || terminalElement.attribute("fill") != "none" || terminalElement.attribute("stroke") != "none") {
			terminalElement.setAttribute("id", "");
			terminalElement = svgDoc->createElement("rect");
		}
		terminalElement.setAttribute("id", terminalID);
		terminalElement.setAttribute("oldid", terminalID);
		terminalElement.setAttribute("stroke", "none");
		terminalElement.setAttribute("fill", "none");
		terminalElement.setAttribute("stroke-width", "0");
		terminalElement.setAttribute("x", QString::number(svgBounds.left() + cx - dx));
		terminalElement.setAttribute("y", QString::number(svgBounds.top() + cy - dy));
		terminalElement.setAttribute("width", QString::number(dx * 2));
		terminalElement.setAttribute("height", QString::number(dy * 2));
		if (terminalElement.attribute("gorn").isEmpty()) {
			QString gorn = svgConnectorElement.attribute("gorn");
			int ix = gorn.lastIndexOf(".");
			if (ix > 0) {
				gorn.truncate(ix);
			}
			gorn = QString("%1.gen%2").arg(gorn).arg(FakeGornSiblingNumber++);
			terminalElement.setAttribute("gorn", gorn);
		}

		svgConnectorElement.parentNode().insertAfter(terminalElement, svgConnectorElement);

		double oldZ = connectorPegi->zValue() + 1;
		Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
			QDomElement pegiElement = pegi->element();
			if (pegiElement.attribute("id").compare(terminalID) == 0) {
				DebugDialog::debug("old pegi location", pegi->pos());
				oldZ = pegi->zValue();
				pegiList.removeOne(pegi);
				delete pegi;
				break;
			}
		}

		// update svg in case there is a subsequent call to reload
		QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(sketchWidget);
		FolderUtils::ensureDirectoryExists(newPath);
		QString svg = TextUtils::svgNSOnly(svgDoc->toString());
		writeXml(newPath, removeGorn(svg), true);
		setImageAttribute(fzpRoot, newPath, sketchWidget->viewID());

		double invdx = dx * size.width() / svgBounds.width();
		double invdy = dy * size.height() / svgBounds.height();
		QPointF topLeft = connectorPegi->offset() + p - QPointF(invdx, invdy);
		PEGraphicsItem * pegi = makePegi(QSizeF(invdx * 2, invdy * 2), topLeft, viewThing->itemBase, terminalElement, oldZ);
		if (pegi != nullptr) {
			DebugDialog::debug("new pegi location", pegi->pos());
		}
		updateChangeCount(sketchWidget, changeDirection);
	}

	connectorPegi->setTerminalPoint(p);
	connectorPegi->update();
	m_peToolView->setTerminalPointCoords(p);
}

void PEMainWindow::editPathAnchor(SketchWidget * sketchWidget, const QString & svgID, int segmentIndex, QPointF anchor, int changeDirection)
{
	if (svgID.isEmpty()) return;
	ViewThing * viewThing = m_viewThings.value(sketchWidget->viewID());
	if (viewThing == nullptr || viewThing->document == nullptr) return;
	QDomDocument * svgDoc = viewThing->document;
	QDomElement svgRoot = svgDoc->documentElement();
	QDomElement pathElement = TextUtils::findElementWithAttribute(svgRoot, "id", svgID);
	if (pathElement.isNull()) {
		DebugDialog::debug(QString("Unable to find svg path element %1").arg(svgID));
		return;
	}

	PEElement pe;
	pe.kind = PEElementKind::Path;
	pe.element = pathElement;
	QString newD = pe.editPathAnchor(segmentIndex, anchor);
	if (newD.isEmpty()) return;
	pathElement.setAttribute("d", newD);

	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(sketchWidget);
	FolderUtils::ensureDirectoryExists(newPath);
	QString svg = TextUtils::svgNSOnly(svgDoc->toString());
	writeXml(newPath, removeGorn(svg), true);
	QDomElement fzpRoot = m_fzpDocument.documentElement();
	setImageAttribute(fzpRoot, newPath, sketchWidget->viewID());

	updateChangeCount(sketchWidget, changeDirection);
}

void PEMainWindow::addPrimitive(SketchWidget * sketchWidget, const QString & parentSvgID, const QString & elementXml, int changeDirection)
{
	if (elementXml.isEmpty()) return;
	ViewThing * viewThing = m_viewThings.value(sketchWidget->viewID());
	if (viewThing == nullptr || viewThing->document == nullptr) return;
	QDomDocument * svgDoc = viewThing->document;

	QDomDocument frag;
	if (!frag.setContent(elementXml)) return;
	QDomElement newElement = frag.documentElement();
	if (newElement.isNull()) return;
	// importNode so it belongs to the target document
	QDomNode imported = svgDoc->importNode(newElement, true);

	QDomElement svgRoot = svgDoc->documentElement();
	QDomElement parent;
	if (!parentSvgID.isEmpty()) {
		parent = TextUtils::findElementWithAttribute(svgRoot, "id", parentSvgID);
	} else {
		// Recovery rather than refusal. Refusing outright turned "Add graphic" into a silent no-op for
		// any part whose layer structure the map does not recognise, which is strictly worse than the
		// pre-M1 behaviour (the shape landed at the svg root and was at least visible). Re-inspect first,
		// since the live map can lag a document reload; failing that, append at the <svg> root as this
		// code always did. Root-append is imperfect -- the shape sits outside the layer groups -- but it
		// is visible and undoable, and the log line still records that routing did not happen.
		m_viewLayerMap.inspect(svgRoot);
		parent = m_viewLayerMap.resolveParent(svgRoot, sketchWidget->viewID());
		// PE_TRACE: routing has now failed twice (once on the live map, once after re-inspecting this
		// very document). Dump the live tree so the difference between "the map is wrong" and "the
		// in-memory document is not the part svg on disk" is settled by data instead of more reading.
		{
			QString kids;
			for (QDomElement c = svgRoot.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
				kids += QString("%1(id='%2') ").arg(c.tagName()).arg(c.attribute("id"));
			DebugDialog::debug(QString("PE_TRACE addPrimitive live root tag=%1 id='%2' children: %3")
			                       .arg(svgRoot.tagName()).arg(svgRoot.attribute("id")).arg(kids));
		}
		if (parent.isNull()) {
			DebugDialog::debug(QString("addPrimitive: no known layer group for view %1"
			                           " [mapEmpty=%2 mapHasView=%3 rootId='%4' rootChildren=%5];"
			                           " falling back to svg root")
			                           .arg(ViewLayer::viewIDNaturalName(sketchWidget->viewID()))
			                           .arg(m_viewLayerMap.isEmpty())
			                           .arg(m_viewLayerMap.contains(sketchWidget->viewID()))
			                           .arg(svgRoot.attribute("id"))
			                           .arg(svgRoot.childNodes().count()));
			parent = svgRoot;
		}
	}
	if (parent.isNull()) {
		DebugDialog::debug(QString("addPrimitive: requested parent '%1' not found -- refusing to add graphic").arg(parentSvgID));
		return;
	}
	parent.appendChild(imported);

	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(sketchWidget);
	FolderUtils::ensureDirectoryExists(newPath);
	QString svg = TextUtils::svgNSOnly(svgDoc->toString());
	writeXml(newPath, removeGorn(svg), true);
	QDomElement fzpRoot = m_fzpDocument.documentElement();
	setImageAttribute(fzpRoot, newPath, sketchWidget->viewID());

	// Re-init the overlay items so the new element gets a PEGraphicsItem.
	//
	// ROOT CAUSE of "click Add circle and nothing happens" (and the same for moveGraphic,
	// resizeGraphic, editPathAnchor and removePrimitive): the edited document was written to a NEW temp
	// path (makeSvgPath2 embeds m_fileIndex, which createPrimitiveRequested bumps per creation), but
	// itemBase kept pointing at the ORIGINAL svg file -- and initSvgTree() re-reads itemBase->filename()
	// and rebuilds its own QDomDocument from that file, discarding the in-memory document we just
	// edited. So every edit was correctly persisted to disk and then never displayed: the canvas kept
	// showing the pre-edit file. setFilename() redirects the item (and the fzp image attribute already
	// updated above) at the file we actually wrote, so the re-read picks up the edit.
	viewThing->itemBase->setFilename(newPath);
	// The QGraphicsSvgItem backdrop (itemBase) renders from a QSvgRenderer that setFilename does
	// NOT reload -- it only stores the path. Without an explicit resetRenderer(), the backdrop keeps
	// painting the PRE-EDIT part svg forever, so a moved/resized shape appears BOTH in its old spot
	// (stale backdrop) and its new spot (live overlay item) -- the reported "artifacts / hidden under
	// the part / doubling". resetRenderer reloads the shared renderer from the string we just wrote
	// (it, not reloadRenderer, also handles a size change). Done BEFORE initSvgTree so the rebuilt
	// overlay items sit over a current backdrop.
	reloadBackdropForView(viewThing, removeGorn(svg), newPath);
	initSvgTree(sketchWidget, viewThing->itemBase, *svgDoc);

	updateChangeCount(sketchWidget, changeDirection);
}

void PEMainWindow::removePrimitive(SketchWidget * sketchWidget, const QString & elementXml, int changeDirection)
{
	DebugDialog::debug(QString("PE_TRACE removePrimitive xml=%1").arg(elementXml.simplified().left(80)));
	if (elementXml.isEmpty()) return;
	ViewThing * viewThing = m_viewThings.value(sketchWidget->viewID());
	if (viewThing == nullptr || viewThing->document == nullptr) return;
	QDomDocument * svgDoc = viewThing->document;

	QDomDocument frag;
	if (!frag.setContent(elementXml)) return;
	QString id = frag.documentElement().attribute("id");
	if (id.isEmpty()) return;
	QDomElement svgRoot = svgDoc->documentElement();
	QDomElement existing = TextUtils::findElementWithAttribute(svgRoot, "id", id);
	if (existing.isNull()) return;
	QDomNode parent = existing.parentNode();
	parent.removeChild(existing);

	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(sketchWidget);
	FolderUtils::ensureDirectoryExists(newPath);
	QString svg = TextUtils::svgNSOnly(svgDoc->toString());
	writeXml(newPath, removeGorn(svg), true);
	QDomElement fzpRoot = m_fzpDocument.documentElement();
	setImageAttribute(fzpRoot, newPath, sketchWidget->viewID());

	// Point the item at the file we actually wrote before re-reading (see addPrimitive).
	viewThing->itemBase->setFilename(newPath);
	// The QGraphicsSvgItem backdrop (itemBase) renders from a QSvgRenderer that setFilename does
	// NOT reload -- it only stores the path. Without an explicit resetRenderer(), the backdrop keeps
	// painting the PRE-EDIT part svg forever, so a moved/resized shape appears BOTH in its old spot
	// (stale backdrop) and its new spot (live overlay item) -- the reported "artifacts / hidden under
	// the part / doubling". resetRenderer reloads the shared renderer from the string we just wrote
	// (it, not reloadRenderer, also handles a size change). Done BEFORE initSvgTree so the rebuilt
	// overlay items sit over a current backdrop.
	reloadBackdropForView(viewThing, removeGorn(svg), newPath);
	initSvgTree(sketchWidget, viewThing->itemBase, *svgDoc);

	updateChangeCount(sketchWidget, changeDirection);
}

// P7 live wiring: move/resize a graphic element (by svg id) -> rewrite its geometry attrs, then
// re-render the view. Mirrors editPathAnchor (find the element by id, mutate, rewrite svg + fzp).
// Colourise a part svg the SAME way the rest of Fritzing does, then hand it to the single backdrop
// renderer. PROVEN root cause (docs/internal/pe-findings.md): the parts editor repainted the part after
// every edit from the RAW svg, so copper pads lost their copper colour (headless proof: raw render -> gold=0)
// -- the washed "old SVG over our new stuff". The real app colourises in FSvgRenderer::loadAux via
// SvgFileSplitter::fixColorRecurse(group, <layer colour>, {black,#000000}) where <layer colour> is keyed on
// the layer (setUpImage: copper1->Copper1Color, copper0->Copper0Color, silkscreen->Silkscreen*Color). We
// apply the identical transform per recognised layer group so the one QGraphicsSvgItem paints a live,
// correctly-coloured part -- no second copy, no raw paint, no reload(). Black (#000000) is preserved (it is
// the silkscreen/technical ink the app keeps), matching loadAux's exception list.
QString PEMainWindow::colourisePartSvg(const QString & svg) {
	QDomDocument doc;
	if (!doc.setContent(svg, QDomDocument::ParseOption::UseNamespaceProcessing)) {
		return svg;
	}
	QDomElement root = doc.documentElement();
	if (root.isNull()) return svg;

	QStringList exceptions;
	exceptions << "black" << "#000000";

	// layer xml id -> the colour Fritzing paints that layer. Mirrors ItemBase::setUpImage's PCB switch.
	auto colorise = [&](const char * layerId, const QString & color) {
		QDomElement g = TextUtils::findElementWithAttribute(root, "id", QString::fromLatin1(layerId));
		if (!g.isNull()) SvgFileSplitter::fixColorRecurse(g, color, exceptions);
	};
	colorise("copper1",     ViewLayer::Copper1Color);
	colorise("copper0",     ViewLayer::Copper0Color);
	colorise("silkscreen",  ViewLayer::Silkscreen1Color);
	colorise("silkscreen0", ViewLayer::Silkscreen0Color);

	return doc.toString();
}

void PEMainWindow::reloadBackdropForView(ViewThing * viewThing, const QString & fullSvg, const QString & newPath) {
	if (viewThing == nullptr || viewThing->itemBase == nullptr) return;
	ItemBase * chief = viewThing->itemBase;

	// PROVEN from the live scene dump (docs/internal/pe-findings.md): the PCB part is NOT one renderer.
	// The SCENE-SVG trace showed view=3 holding THREE ItemBases -- layer 20 (Copper0), 23 (Copper1) and 25
	// (Silkscreen1): the chief is a PaletteItem and Copper0 / Silkscreen1 are its layerKin siblings, EACH
	// with its own QSvgRenderer. setUpImage (verified: paletteitembase.cpp:453) repaints ONLY the item it is
	// called on -- it does NOT recurse into layerKin. So every earlier fix that touched just the chief left
	// Copper0 and Silkscreen1 painting the PRE-EDIT svg: those stale siblings are the "old SVG over our new
	// stuff" and the giant black silkscreen circles. Refresh EVERY member of the family. setUpImage reads
	// bytes off disk via the modelPart imageFileName, so point the chief's modelPart at the file we just
	// wrote, unprocess connectors per member (setUpImage re-processes them for that view/layer), then
	// setUpImage each member with its OWN layer attributes. Reuses the existing items -> zoom/pan + overlay
	// survive (unlike reload()), and reproduces the exact colourised per-layer render of the initial load.
	PaletteItem * pi = dynamic_cast<PaletteItem *>(chief);
	if (pi != nullptr && pi->modelPart() != nullptr && !newPath.isEmpty() && viewThing->sketchWidget != nullptr) {
		ModelPart * mp = pi->modelPart();
		const ViewLayer::ViewID viewID = chief->viewID();
		mp->setImageFileName(viewID, newPath);

		QList<ItemBase *> family;
		family << chief;
		Q_FOREACH (ItemBase * lk, pi->layerKin()) {
			if (lk != nullptr && lk != chief) family << lk;
		}

		bool allRefreshed = true;
		Q_FOREACH (ItemBase * member, family) {
			PaletteItemBase * pib = dynamic_cast<PaletteItemBase *>(member);
			if (pib == nullptr) { allRefreshed = false; continue; }
			Q_FOREACH (Connector * connector, mp->connectors()) {
				connector->unprocess(viewID, member->viewLayerID());
			}
			LayerAttributes layerAttributes;
			member->initLayerAttributes(layerAttributes, viewID, member->viewLayerID(),
			                            member->viewLayerPlacement(), true, !member->selectionShape().isEmpty());
			if (!pib->setUpImage(mp, viewThing->sketchWidget->viewLayers(), layerAttributes)) {
				allRefreshed = false;
			}
		}

		// If any member could not go through the colourising path, at least repaint every member from the
		// edited svg with the layer colours applied, so nothing is left showing the pre-edit part.
		if (!allRefreshed) {
			const QString colourised = colourisePartSvg(fullSvg);
			Q_FOREACH (ItemBase * member, family) {
				member->resetRenderer(colourised);
			}
		}
		return;
	}

	// Non-PaletteItem fallback (single renderer): colourised raw paint beats a stale backdrop.
	chief->resetRenderer(colourisePartSvg(fullSvg));
}

void PEMainWindow::moveGraphic(SketchWidget * sketchWidget, const QString & svgID, QPointF delta, int changeDirection)
{
	DebugDialog::debug(QString("PE_TRACE moveGraphic svgID=%1").arg(svgID));
	if (svgID.isEmpty()) return;
	ViewThing * viewThing = m_viewThings.value(sketchWidget->viewID());
	if (viewThing == nullptr || viewThing->document == nullptr) return;
	QDomDocument * svgDoc = viewThing->document;
	QDomElement svgRoot = svgDoc->documentElement();
	QDomElement target = TextUtils::findElementWithAttribute(svgRoot, "id", svgID);
	if (target.isNull()) {
		DebugDialog::debug(QString("Unable to find svg graphic element %1").arg(svgID));
		return;
	}

	PEElement pe;
	pe.kind = PEPrimitive::kindForElement(target);
	pe.element = target;
	if (!pe.moveBy(delta)) return;

	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(sketchWidget);
	FolderUtils::ensureDirectoryExists(newPath);
	QString svg = TextUtils::svgNSOnly(svgDoc->toString());
	writeXml(newPath, removeGorn(svg), true);
	QDomElement fzpRoot = m_fzpDocument.documentElement();
	setImageAttribute(fzpRoot, newPath, sketchWidget->viewID());

	// Point the item at the file we actually wrote before re-reading: initSvgTree() rebuilds from
	// itemBase->filename(), so without this the view reloads the stale pre-edit svg and the move --
	// though correctly persisted -- is never displayed. (See addPrimitive for the full analysis.)
	viewThing->itemBase->setFilename(newPath);
	// The QGraphicsSvgItem backdrop (itemBase) renders from a QSvgRenderer that setFilename does
	// NOT reload -- it only stores the path. Without an explicit resetRenderer(), the backdrop keeps
	// painting the PRE-EDIT part svg forever, so a moved/resized shape appears BOTH in its old spot
	// (stale backdrop) and its new spot (live overlay item) -- the reported "artifacts / hidden under
	// the part / doubling". resetRenderer reloads the shared renderer from the string we just wrote
	// (it, not reloadRenderer, also handles a size change). Done BEFORE initSvgTree so the rebuilt
	// overlay items sit over a current backdrop.
	reloadBackdropForView(viewThing, removeGorn(svg), newPath);
	initSvgTree(sketchWidget, viewThing->itemBase, *svgDoc);

	updateChangeCount(sketchWidget, changeDirection);
}

void PEMainWindow::resizeGraphic(SketchWidget * sketchWidget, const QString & svgID, QRectF to, int changeDirection)
{
	DebugDialog::debug(QString("PE_TRACE resizeGraphic svgID=%1").arg(svgID));
	if (svgID.isEmpty()) return;
	ViewThing * viewThing = m_viewThings.value(sketchWidget->viewID());
	if (viewThing == nullptr || viewThing->document == nullptr) return;
	QDomDocument * svgDoc = viewThing->document;
	QDomElement svgRoot = svgDoc->documentElement();
	QDomElement target = TextUtils::findElementWithAttribute(svgRoot, "id", svgID);
	if (target.isNull()) {
		DebugDialog::debug(QString("Unable to find svg graphic element %1").arg(svgID));
		return;
	}

	PEElement pe;
	// Derive the kind from the element instead of asserting Rect. Hardcoding Rect meant resizeRect's
	// kind check rejected every circle/line/ellipse before it could look at the attrs, so dragging a
	// resize grip on those shapes silently did nothing.
	pe.kind = PEPrimitive::kindForElement(target);
	pe.element = target;
	pe.bounds = getPixelBounds(target);   // polyline/polygon resize scales from the current box
	if (!pe.resizeRect(to)) {
		DebugDialog::debug(QString("resizeGraphic: tag %1 (kind %2) does not support resize -- element %3")
		                       .arg(target.tagName()).arg(static_cast<int>(pe.kind)).arg(svgID));
		return;
	}

	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(sketchWidget);
	FolderUtils::ensureDirectoryExists(newPath);
	QString svg = TextUtils::svgNSOnly(svgDoc->toString());
	writeXml(newPath, removeGorn(svg), true);
	QDomElement fzpRoot = m_fzpDocument.documentElement();
	setImageAttribute(fzpRoot, newPath, sketchWidget->viewID());

	// Point the item at the file we actually wrote before re-reading (see addPrimitive).
	viewThing->itemBase->setFilename(newPath);
	// The QGraphicsSvgItem backdrop (itemBase) renders from a QSvgRenderer that setFilename does
	// NOT reload -- it only stores the path. Without an explicit resetRenderer(), the backdrop keeps
	// painting the PRE-EDIT part svg forever, so a moved/resized shape appears BOTH in its old spot
	// (stale backdrop) and its new spot (live overlay item) -- the reported "artifacts / hidden under
	// the part / doubling". resetRenderer reloads the shared renderer from the string we just wrote
	// (it, not reloadRenderer, also handles a size change). Done BEFORE initSvgTree so the rebuilt
	// overlay items sit over a current backdrop.
	reloadBackdropForView(viewThing, removeGorn(svg), newPath);
	initSvgTree(sketchWidget, viewThing->itemBase, *svgDoc);

	updateChangeCount(sketchWidget, changeDirection);
}

void PEMainWindow::pegiPathAnchorMoved(PEGraphicsItem * pegi, int segmentIndex, QPointF anchor)
{
	// live drag feedback: re-render the item so the grip tracks the cursor (no undo yet)
	Q_UNUSED(segmentIndex);
	Q_UNUSED(anchor);
	if (pegi != nullptr) pegi->update();
}

void PEMainWindow::pegiPathAnchorChanged(PEGraphicsItem * pegi, int segmentIndex, QPointF before, QPointF after)
{
	pathAnchorChangedAux(pegi, segmentIndex, before, after);
}

void PEMainWindow::createPrimitiveRequested(const QString & kind)
{
    DebugDialog::debug(QString("PE_TRACE createPrimitiveRequested kind=%1 currentView=%2")
                       .arg(kind).arg(m_currentGraphicsView == nullptr ? "NULL" : "set"));
    if (m_currentGraphicsView == nullptr) {
        // No graphic canvas is active (e.g. still on the Welcome/landing tab). Say so instead of
        // silently doing nothing -- a silent return made the tool buttons feel dead/non-functional.
        DebugDialog::debug("PEMainWindow::createPrimitiveRequested: no active graphic view; open a part's graphic tab first");
        return;
    }
    // Map the requested kind string onto the typed element kind.
    PEElementKind k = PEElementKind::Unknown;
    if (kind == "rect") k = PEElementKind::Rect;
    else if (kind == "circle") k = PEElementKind::Circle;
    else if (kind == "ellipse") k = PEElementKind::Ellipse;
    else if (kind == "line") k = PEElementKind::Line;
    else if (kind == "path") k = PEElementKind::Path;
    else if (kind == "polyline") k = PEElementKind::Polyline;
    else if (kind == "polygon") k = PEElementKind::Polygon;
    else if (kind == "text") k = PEElementKind::Text;
    if (k == PEElementKind::Unknown) return;

    // Place the new graphic inside the part, centred on what the user is looking at.
    //
    // The previous version took mapToScene(viewportRect()).boundingRect().center() and ran it through
    // PEUtils::convertUnits(), i.e. px->mm at 90 dpi. Two things were wrong with that, and they are
    // what put a shape half off a 15x13mm board (a saved file showed cy=7.05 with y1=-2.01):
    //   * The scene px are the px of a VIEW THAT IS ZOOMED AND PANNED. mapToScene of the widget rect
    //     is the currently-visible region, so the "centre" moved with zoom and pan, and its distance
    //     from the part origin scaled with the zoom level.
    //   * convertUnits assumes 90-dpi svg px. The overlay lives in the item's own px space, which is
    //     already a scaled rendering of the viewBox, so dividing by 3.5277 again double-scaled it.
    // The correct mapping is the one Qt already owns: ask the svg item to map the view centre into ITS
    // coordinate system, which is the same px space getPixelBounds()/PEGraphicsItem use, then convert
    // px->units exactly once. Clamp into the part's own bounding box so a shape can never be created
    // off-canvas where the user cannot click it.
    ViewThing * viewThing = m_viewThings.value(m_currentGraphicsView->viewID());
    if (viewThing == nullptr || viewThing->document == nullptr || viewThing->itemBase == nullptr) return;

    const QPointF viewCentrePx =
        viewThing->itemBase->mapFromScene(m_currentGraphicsView->mapToScene(m_currentGraphicsView->viewport()->rect().center()));
    QPointF center = PEUtils::convertUnits(viewCentrePx);

    // Keep the whole box on the board: a shape placed partly off the viewBox is invisible or only
    // half clickable, which is exactly the "cannot click the shape" complaint. The board is the part's
    // viewBox in user units -- NOT getPixelBounds(root), which reads geometry attrs and returns null
    // for an <svg> element.
    // Read the extent off a renderer where possible: QSvgRenderer already resolves viewBox vs
    // width/height vs preserveAspectRatio the same way the on-screen item does, so the clamp cannot
    // disagree with what the user sees. Fall back to parsing the raw attribute, and if that fails too,
    // do NOT clamp -- inventing a 100x100 box would silently park shapes near the origin instead.
    QRectF board;
    {
        FSvgRenderer vbRenderer;
        if (vbRenderer.load(viewThing->originalSvgPath)) board = vbRenderer.viewBoxF();
    }
    if (!board.isValid() || board.width() <= 0 || board.height() <= 0) {
        board = PEPrimitive::viewBoxFromAttribute(viewThing->document->documentElement());
    }
    const bool canClamp = board.isValid() && board.width() > 0 && board.height() > 0;

    const double half = 5.0;   // default 10x10 box in user units, centred on `center`
    double boxHalf = half;
    if (canClamp && (board.width() < 2 * boxHalf || board.height() < 2 * boxHalf)) {
        // Board smaller than the default box: shrink to fit rather than clamp the centre into a
        // corner, which would park every shape at the edge of a small part.
        boxHalf = qMax(0.5, qMin(board.width(), board.height()) / 4.0);
        center = board.center();
    } else if (canClamp) {
        center.setX(qBound(board.left() + boxHalf, center.x(), board.right() - boxHalf));
        center.setY(qBound(board.top() + boxHalf, center.y(), board.bottom() - boxHalf));
    }
    DebugDialog::debug(QString("PE_TRACE place centre=(%1,%2) boxHalf=%3 board=(%4,%5 %6x%7) clamped=%8")
                           .arg(center.x()).arg(center.y()).arg(boxHalf)
                           .arg(board.x()).arg(board.y()).arg(board.width()).arg(board.height())
                           .arg(canClamp));
    const QRectF rect(center.x() - boxHalf, center.y() - boxHalf, 2 * boxHalf, 2 * boxHalf);
    const QPointF a(center.x() - boxHalf, center.y() - boxHalf);
    const QPointF b(center.x() + boxHalf, center.y() + boxHalf);

    // Unique graphic id: module id + the (per-part) graphic counter, which we bump below on success.
    QString id = QString("%1.%2").arg(m_originalModuleID).arg(m_fileIndex);

    // Match the stroke width the part already draws with. PEPrimitive::make used to hardcode 1, which
    // is 1 USER UNIT -- on this 15 x 13 mm connector whose own tracks are 0.2-0.44 wide, that is 3-5x
    // too fat, and a 10-unit circle with a 1-unit black stroke reads as a solid blob once zoomed out.
    const double strokeWidth = PEPrimitive::typicalStrokeWidth(viewThing->document->documentElement());
    QDomElement built = PEPrimitive::make(*viewThing->document,
                                          k, id, QString(), rect, a, b,
                                          center, 5.0, center, 5.0, 5.0,
                                          QList<QPointF>() << a << b,
                                          QString("M %1 %2 L %3 %4").arg(QString::number(a.x()), QString::number(a.y()), QString::number(b.x()), QString::number(b.y())),
                                          center, QString(), strokeWidth);
    if (built.isNull()) return;

    QDomDocument frag;
    frag.appendChild(frag.importNode(built, true));
    QString xml = frag.toString();
    DebugDialog::debug(QString("PE_TRACE created element id=%1 tag=%2 center=(%3,%4) strokeWidth=%5 viewBox='%6'")
                       .arg(id).arg(built.tagName()).arg(center.x()).arg(center.y()).arg(strokeWidth)
                       .arg(viewThing->document->documentElement().attribute("viewBox")));
    auto * command = new AddPrimitiveCommand(this, m_currentGraphicsView, QString(), xml, nullptr);
    command->setText(tr("Add %1").arg(kind));
    // Bump the counter now so the next created graphic gets a fresh id (was: every graphic reused
    // the same id, so only the first ever showed / updated).
    m_fileIndex++;
    m_undoStack->waitPush(command, SketchWidget::PropChangeDelay);
}

void PEMainWindow::addConnectorRequested()
{
	if (m_currentGraphicsView == nullptr) return;
	// Bind a new connector to the first graphic element of the current view (the graphic the user
	// most plausibly just created); the terminal handle then rides on that graphic via P2 node-edit.
	ViewThing * viewThing = m_viewThings.value(m_currentGraphicsView->viewID());
	if (viewThing == nullptr || viewThing->document == nullptr) return;
	QDomElement svgRoot = viewThing->document->documentElement();
	// pick the first child graphic of the svg root as the connector's graphic target.
	QDomElement target;
	for (QDomElement e = svgRoot.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
		if (e.tagName() != "g") { target = e; break; }
	}
	if (target.isNull() || target.attribute("id").isEmpty()) {
		DebugDialog::debug("PEMainWindow::addConnectorRequested: no graphic element with an id to bind");
		return;
	}

	QString connectorID = QString("connector%1").arg(m_fileIndex);
	QDomElement fzpRoot = m_fzpDocument.documentElement();
	PEConnector::authorConnector(m_fzpDocument, fzpRoot, connectorID, QString(),
	                            ViewLayer::viewIDXmlName(m_currentGraphicsView->viewID()),
	                            target.attribute("id"));
	m_connectorList.clear();
	initConnectors(true);
	updateAssignedConnectors();
	updateChangeCount(m_currentGraphicsView, 1);
}

void PEMainWindow::createBlankPart()
{
	// P6 create-from-blank: stand up a brand-new, empty part in place of the current base. Build the
	// blank module into m_fzpDocument with a fresh moduleId, write an empty graphic group per view into
	// the user parts folder, point each view's <layers image> at its file, then reload the canvases.
	m_guid = PEBlankPart::newModuleID();
	m_originalModuleID = m_guid;
	m_prefix = "prefix0000";
	m_fileIndex = 0;
	m_userPartsFolderSvgPath = m_userPartsFolderPath + m_guid + "/";
	FolderUtils::ensureDirectoryExists(m_userPartsFolderSvgPath);

	// A default 0.5in x 0.5in blank box for the empty graphic group's viewBox.
	QRectF box(0, 0, 0.5, 0.5);
	QDomDocument blank;
	QDomElement module = PEBlankPart::buildModule(blank, m_guid, tr("New Part"));
	// Rewrite each view's <layers image> to the real svg we write for that view, and give the svg a
	// single empty graphic group named by the view's base layer (the <g id> convention the editor keys on).
	QDomElement views = module.firstChildElement("views");
	Q_FOREACH (ViewLayer::ViewID viewID, m_viewThings.keys()) {
		const QList<ViewLayer::ViewLayerID> layers = ViewLayer::layersForView(viewID);
		if (layers.isEmpty()) continue;
		QString layerName = ViewLayer::viewLayerXmlNameFromID(layers.first());
		QString svg = PEBlankPart::blankViewSvg(layerName, box);
		QString actualPath = m_userPartsFolderSvgPath + layerName + "/" + m_guid + ".svg";
		FolderUtils::ensureDirectoryExists(QFileInfo(actualPath).absolutePath());
		writeXml(actualPath, svg, false);
		QDomElement view = views.firstChildElement(ViewLayer::viewIDXmlName(viewID));
		if (!view.isNull())
			setImageAttribute(module, actualPath, viewID);
	}

	m_fzpDocument = blank;
	m_originalFzpPath = m_userPartsFolderSvgPath + m_guid + ".fzp";
	m_canSave = true;
	reload(true);
	setTitle();
}

void PEMainWindow::importSvgFile()
{
	if (m_currentGraphicsView == nullptr) return;
	QString path = QFileDialog::getOpenFileName(this, tr("Import SVG"), QString(),
	                                           tr("SVG (*.svg);;All Files (*)"));
	if (path.isEmpty()) return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		QMessageBox::warning(this, tr("Import SVG"), tr("Unable to open %1").arg(path));
		return;
	}
	QString foreign = QString::fromUtf8(file.readAll());
	file.close();

	// Normalize the foreign svg into a single canonical graphic group keyed by the current view's base layer.
	const QList<ViewLayer::ViewLayerID> layers = ViewLayer::layersForView(m_currentGraphicsView->viewID());
	if (layers.isEmpty()) return;
	QString layerName = ViewLayer::viewLayerXmlNameFromID(layers.first());
	QString normalized = PESvgImport::normalizeToString(foreign, layerName);

	// Land the imported graphic in the current view's svg file and point the fzp at it.
	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(m_currentGraphicsView);
	FolderUtils::ensureDirectoryExists(QFileInfo(newPath).absolutePath());
	if (!writeXml(newPath, normalized, false)) {
		QMessageBox::critical(this, tr("Import SVG"), tr("Unable to write svg to %1").arg(newPath));
		return;
	}
	QDomElement fzpRoot = m_fzpDocument.documentElement();
	setImageAttribute(fzpRoot, newPath, m_currentGraphicsView->viewID());
	m_canSave = true;
	reload(true);
	setTitle();
}

void PEMainWindow::pathAnchorChangedAux(PEGraphicsItem * pegi, int segmentIndex, QPointF before, QPointF after)
{
	if (pegi == nullptr) return;
	if (m_currentGraphicsView == nullptr) return;
	QDomElement element = pegi->element();
	QString svgID = element.attribute("id");
	if (svgID.isEmpty()) return;
	SketchWidget * sketchWidget = m_currentGraphicsView;

	auto * command = new EditPathAnchorCommand(this, sketchWidget, svgID, segmentIndex, before, after, nullptr);
	command->setText(tr("Move path anchor"));
	m_undoStack->waitPush(command, SketchWidget::PropChangeDelay);
}

void PEMainWindow::showInOS(QWidget *parent, const QString &pathIn)
{
	Q_UNUSED(parent);
	FolderUtils::showInFolder(pathIn);
	QClipboard *clipboard = QApplication::clipboard();
	if (clipboard != nullptr) {
		clipboard->setText(pathIn);
	}
}

void PEMainWindow::showInOS() {
	if (m_currentGraphicsView == nullptr) return;

	ViewThing * viewThing = m_viewThings.value(m_currentGraphicsView->viewID());
	showInOS(this, viewThing->itemBase->filename());
}

PEGraphicsItem * PEMainWindow::makePegi(QSizeF size, QPointF topLeft, ItemBase * itemBase, QDomElement & element, double z, bool freelyEditable)
{
	// M0.4/M0.5 guard: every live item must be backed by a real modeled graphic. An item built over a
	// null QDomElement renders nothing (shapeFromElement has no attrs), cannot be edited
	// (pegiGraphicMoveDelta/MoveGraphicCommand resolve the target by element id), and is still movable,
	// selectable and returned by getPegiList() -- so it silently eats clicks and corrupts every pass over
	// that list. Reject it loudly instead of shipping an inert interactive phantom.
	if (itemBase == nullptr || itemBase->scene() == nullptr || element.isNull()) {
		DebugDialog::debug(QString("makePegi: refused to create a live graphic item (null element or scene) at %1,%2")
		                       .arg(topLeft.x()).arg(topLeft.y()));
		return nullptr;
	}

	// Fully-live slice: the graphic is now a standalone top-level object (no parent), movable +
	// selectable, so the user can click a single shape to select it and drag it (or its nodes) directly.
	// (It used to be a child overlay of the whole-view svg item.)
	auto * pegiItem = new PEGraphicsItem(0, 0, size.width(), size.height(), itemBase);
	pegiItem->showTerminalPoint(false);
	pegiItem->setPos(itemBase->pos() + topLeft);
	pegiItem->setZValue(z);
	itemBase->scene()->addItem(pegiItem);
	pegiItem->setElement(element);
	pegiItem->setOffset(topLeft);
	pegiItem->setDocBounds(QRectF(topLeft, size));   // element bounds in user units (px<->doc mapping for grips/drag)
	// A connector pad / nonconnector is part of the part's electrical geometry, not a free graphic.
	// It stays selectable (the connector workflow needs to click it) but must NOT get move/resize grips:
	// when every pad was independently draggable the user dragging "the part" actually detached single
	// pads out of the copper layer, which is the reported "layering is wrong / part looks broken".
	if (freelyEditable) {
		pegiItem->setInteractive(true);
	} else {
		pegiItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
		pegiItem->setFlag(QGraphicsItem::ItemIsFocusable, true);
	}
	connect(pegiItem, SIGNAL(highlightSignal(PEGraphicsItem *)), this, SLOT(highlightSlot(PEGraphicsItem *)));
	connect(pegiItem, SIGNAL(mouseReleasedSignal(PEGraphicsItem *)), this, SLOT(pegiMouseReleased(PEGraphicsItem *)));
	connect(pegiItem, SIGNAL(mousePressedSignal(PEGraphicsItem *, bool &)), this, SLOT(pegiMousePressed(PEGraphicsItem *, bool &)), Qt::DirectConnection);
	connect(pegiItem, SIGNAL(terminalPointMoved(PEGraphicsItem *, QPointF)), this, SLOT(pegiTerminalPointMoved(PEGraphicsItem *, QPointF)));
	connect(pegiItem, SIGNAL(terminalPointChanged(PEGraphicsItem *, QPointF, QPointF)), this, SLOT(pegiTerminalPointChanged(PEGraphicsItem *, QPointF, QPointF)));
	connect(pegiItem, SIGNAL(pathAnchorMoved(PEGraphicsItem *, int, QPointF)), this, SLOT(pegiPathAnchorMoved(PEGraphicsItem *, int, QPointF)));
	connect(pegiItem, SIGNAL(pathAnchorChanged(PEGraphicsItem *, int, QPointF, QPointF)), this, SLOT(pegiPathAnchorChanged(PEGraphicsItem *, int, QPointF, QPointF)));
	connect(pegiItem, SIGNAL(graphicMoveDelta(PEGraphicsItem *, QPointF)), this, SLOT(pegiGraphicMoveDelta(PEGraphicsItem *, QPointF)));
	connect(pegiItem, SIGNAL(graphicResizeTo(PEGraphicsItem *, QRectF)), this, SLOT(pegiGraphicResizeTo(PEGraphicsItem *, QRectF)));
	return pegiItem;
}

// Fully-live slice: a live graphic was body-dragged; apply the user-unit delta to its geometry attrs
// through the existing moveGraphic path (translates every kind, incl. <path>), undoable.
void PEMainWindow::pegiGraphicMoveDelta(PEGraphicsItem * pegi, QPointF deltaUnits)
{
	if (m_currentGraphicsView == nullptr || pegi == nullptr) return;
	QString svgID = pegi->element().attribute("id");
	if (svgID.isEmpty()) return;
	auto * command = new MoveGraphicCommand(this, m_currentGraphicsView, svgID, deltaUnits, nullptr);
	command->setText(tr("Move graphic"));
	m_undoStack->waitPush(command, SketchWidget::PropChangeDelay);
}

void PEMainWindow::pegiGraphicResizeTo(PEGraphicsItem * pegi, QRectF to)
{
	if (m_currentGraphicsView == nullptr || pegi == nullptr) return;
	QString svgID = pegi->element().attribute("id");
	if (svgID.isEmpty()) return;
	// 'before' is taken from the element being resized so undo restores the exact prior box.
	QRectF before = getPixelBounds(pegi->element());
	auto * command = new ResizeGraphicCommand(this, m_currentGraphicsView, svgID, before, to, nullptr);
	command->setText(tr("Resize graphic"));
	m_undoStack->waitPush(command, SketchWidget::PropChangeDelay);
}

QRectF PEMainWindow::getPixelBounds(QDomElement & element)
{
	// P9: the element has been flattened (PETransform::flatten in initSvgTree), so its own geometry
	// attrs already hold absolute user-unit coordinates -- no renderer bounds / viewBox→px scaling is
	// needed (and the renderer's baked id-addressing no longer applies to the rewritten attrs). Read the
	// attrs directly per kind, matching PEElement::moveBy's per-kind coordinate set. For kinds that store
	// no box (line/circle/path/text), return an axis-aligned box spanning their (absolute) points.
	const QString tag = element.tagName();
	if (tag == QLatin1String("rect") || tag == QLatin1String("image")) {
		bool ok = false;
		const double x = element.attribute(QStringLiteral("x")).toDouble(&ok);
		if (!ok) return QRectF();
		const double y = element.attribute(QStringLiteral("y")).toDouble(&ok);
		if (!ok) return QRectF();
		const double w = element.attribute(QStringLiteral("width")).toDouble(&ok);
		if (!ok) return QRectF();
		const double h = element.attribute(QStringLiteral("height")).toDouble(&ok);
		if (!ok) return QRectF();
		return QRectF(x, y, w, h);
	}
	if (tag == QLatin1String("line")) {
		bool ok = false;
		const double x1 = element.attribute(QStringLiteral("x1")).toDouble(&ok); if (!ok) return QRectF();
		const double y1 = element.attribute(QStringLiteral("y1")).toDouble(&ok); if (!ok) return QRectF();
		const double x2 = element.attribute(QStringLiteral("x2")).toDouble(&ok); if (!ok) return QRectF();
		const double y2 = element.attribute(QStringLiteral("y2")).toDouble(&ok); if (!ok) return QRectF();
		return QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized();
	}
	if (tag == QLatin1String("circle")) {
		bool ok = false;
		const double cx = element.attribute(QStringLiteral("cx")).toDouble(&ok); if (!ok) return QRectF();
		const double cy = element.attribute(QStringLiteral("cy")).toDouble(&ok); if (!ok) return QRectF();
		const double r = element.attribute(QStringLiteral("r")).toDouble(&ok); if (!ok) return QRectF();
		return QRectF(cx - r, cy - r, 2 * r, 2 * r);
	}
	if (tag == QLatin1String("ellipse")) {
		bool ok = false;
		const double cx = element.attribute(QStringLiteral("cx")).toDouble(&ok); if (!ok) return QRectF();
		const double cy = element.attribute(QStringLiteral("cy")).toDouble(&ok); if (!ok) return QRectF();
		const double rx = element.attribute(QStringLiteral("rx")).toDouble(&ok); if (!ok) return QRectF();
		const double ry = element.attribute(QStringLiteral("ry")).toDouble(&ok); if (!ok) return QRectF();
		return QRectF(cx - rx, cy - ry, 2 * rx, 2 * ry);
	}
	// M1: path / text / polyline / polygon store no axis-aligned box in their attributes, so the
	// attribute-level read above cannot bound them. This used to return a null rect, and because
	// initSvgTree() only creates a live item when width>0 && height>0, those four element kinds got
	// appended to the SVG but never displayed -- which is exactly why the "Add graphic" path / polyline /
	// polygon buttons looked dead (and why created text was invisible). Decode the actual geometry
	// instead: P10's shapeFromElement already produces the QPainterPath the item self-renders from, so
	// its bounding rect is the one honest bound and it agrees with what gets painted.
	QPainterPath shape;
	if (PETransform::shapeFromElement(element, shape)) {
		const QRectF r = shape.boundingRect();
		if (r.width() > 0 && r.height() > 0) return r;
		// A straight horizontal / vertical line (or a text run) legitimately has zero height or width.
		// It is real, visible geometry, so pad it to a hairline box rather than dropping it.
		if (!r.isNull()) return r.adjusted(-0.01, -0.01, 0.01, 0.01);
	}

	return QRectF();
}

bool PEMainWindow::canSave() {

	return m_canSave;
}

void PEMainWindow::tabWidget_currentChanged(int index) {
	MainWindow::tabWidget_currentChanged(index);

	if (m_peToolView == nullptr) return;

	switchedConnector(m_peToolView->currentConnectorIndex());

	bool enabled = index < IconViewIndex;
	m_peSvgView->setChildrenVisible(enabled);
	m_peToolView->setChildrenVisible(enabled);

	if (m_currentGraphicsView == nullptr) {
		// update title when switching to connector and metadata view
		setTitle();
	}
	else {
	}

	updateAssignedConnectors();
	updateActiveLayerButtons();
}

void PEMainWindow::backupSketch()
{
}

void PEMainWindow::removedConnector(const QDomElement & element)
{
	QList<QDomElement> connectors;
	connectors.append(element);
	removedConnectorsAux(connectors);
}

void PEMainWindow::removedConnectors(QList<ConnectorMetadata *> & cmdList)
{
	QList<QDomElement> connectors;

	Q_FOREACH (ConnectorMetadata * cmd, cmdList) {
		int index;
		QDomElement connector = findConnector(cmd->connectorID, index);
		if (connector.isNull()) return;

		cmd->index = index;
		connectors.append(connector);
	}

	removedConnectorsAux(connectors);
}

void PEMainWindow::removedConnectorsAux(QList<QDomElement> & connectors)
{
	QString originalPath = saveFzp();

	Q_FOREACH (QDomElement connector, connectors) {
		if (m_removedConnector.isEmpty()) {
			QTextStream stream(&m_removedConnector);
			connector.save(stream, 0);
		}
		connector.parentNode().removeChild(connector);
	}

	QString newPath = saveFzp();

	auto * cfc = new ChangeFzpCommand(this, originalPath, newPath, nullptr);
	QString message;
	if (connectors.count() == 1) {
		message = tr("Remove connector");
	}
	else {
		message = tr("Remove %1 connectors").arg(connectors.count());
	}
	cfc->setText(message);
	m_undoStack->waitPush(cfc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::restoreFzp(const QString & fzpPath)
{
	if (!loadFzp(fzpPath)) return;

	reload(false);
}

void PEMainWindow::setBeforeClosingText(const QString & filename, QMessageBox & messageBox)
{
	Q_UNUSED(filename);

	QString partTitle = getPartTitle();
	messageBox.setWindowTitle(tr("Save \"%1\"", "dialog title").arg(partTitle));
	messageBox.setText(tr("Do you want to save the changes you made in the part \"%1\"?").arg(partTitle));
	messageBox.setInformativeText(tr("Your changes will be lost if you don't save them."));
}

QString PEMainWindow::getPartTitle() {
	QString partTitle = tr("untitled part");
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement title = root.firstChildElement("title");
	QString candidate = title.text();
	if (!candidate.isEmpty()) return candidate;

	if (m_viewThings.count() > 0) {
		ViewThing * viewThing = m_viewThings.values().at(0);
		if (viewThing->itemBase != nullptr) {
			candidate = viewThing->itemBase->title();
			if (!candidate.isEmpty()) return candidate;
		}
	}

	return partTitle;
}

void PEMainWindow::killPegi() {
	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		if (viewThing->sketchWidget == nullptr) continue;

		Q_FOREACH (QGraphicsItem * item, viewThing->sketchWidget->scene()->items()) {
			auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
			if (pegi != nullptr) delete pegi;
		}
	}
}

bool PEMainWindow::loadFzp(const QString & path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		DebugDialog::debug(QString("Unable to open :%1").arg(path));
	}
	QDomDocument::ParseResult parseResult = m_fzpDocument.setContent(&file);
	if (!parseResult) {
		QMessageBox::critical(nullptr, tr("Parts Editor", "dialog title"), tr("Unable to load fzp from %1").arg(path));
		return false;
	}

	return true;
}

void PEMainWindow::connectorCountChanged(int newCount) {
	QList<QDomElement> connectorList;
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		connectorList.append(connector);
		connector = connector.nextSiblingElement();
	}

	if (newCount == connectorList.count()) return;

	if (newCount < connectorList.count()) {
		std::sort(connectorList.begin(), connectorList.end(), byID);
		QList<QDomElement> toDelete;
		for (int i = newCount; i < connectorList.count(); i++) {
			toDelete.append(connectorList.at(i));
		}

		removedConnectorsAux(toDelete);
		return;
	}

	// add connectors
	int id = 0;
	Q_FOREACH (QDomElement connector, connectorList) {
		QRegularExpressionMatch match;
		int ix = connector.attribute("id").indexOf(IntegerFinder, 0, &match);
		if (ix >= 0) {
			int candidate = match.captured(0).toInt();
			if (candidate > id) id = candidate;
		}
		// sometimes id = 0 but name = 1, and we are now using name = id
		match = QRegularExpressionMatch();
		ix = connector.attribute("name").indexOf(IntegerFinder, 0, &match);
		if (ix >= 0) {
			int candidate = match.captured(0).toInt();
			if (candidate > id) id = candidate;
		}
	}

	QString originalPath = saveFzp();

	QDomElement connectorModel;
	QDomDocument tempDoc;
	if (connectorList.count() > 0) connectorModel = connectorList.at(0);
	else {
		tempDoc.setContent(m_removedConnector);
		connectorModel = tempDoc.documentElement();
		if (connectorModel.isNull()) {
			QMessageBox::critical(nullptr, tr("Parts Editor", "dialog title"), tr("Unable to create new connector--you may have to start over."));
			return;
		}
	}
	for (int i = connectorList.count(); i < newCount; i++) {
		id++;
		QDomElement element = connectorModel.cloneNode(true).toElement();
		connectors.appendChild(element);
		QString newName = QString("pin %1").arg(id);
		element.setAttribute("name", newName);
		QString cid = QString("connector%1").arg(id);
		element.setAttribute("id", cid);
		QString svgid = cid + "pin";
		QDomNodeList nodeList = element.elementsByTagName("p");
		for (int n = 0; n < nodeList.count(); n++) {
			QDomElement p = nodeList.at(n).toElement();
			p.removeAttribute("terminalId");
			p.removeAttribute("legId");
			p.setAttribute("svgId", svgid);
		}
		QDomElement description = element.firstChildElement("description");
		if (description.isNull()) {
			description = m_fzpDocument.createElement("description");
			element.appendChild(description);
		}
		TextUtils::replaceChildText(description, newName);
	}

	QString newPath = saveFzp();

	auto * cfc = new ChangeFzpCommand(this, originalPath, newPath, nullptr);
	QString message;
	if (newCount - connectorList.count() == 1) {
		message = tr("Add connector");
	}
	else {
		message = tr("Add %1 connectors").arg(newCount - connectorList.count());
	}
	cfc->setText(message);
	m_undoStack->waitPush(cfc, SketchWidget::PropChangeDelay);
}

bool PEMainWindow::editsModuleID(const QString & moduleID) {
	// only to detect whether a user tries to open the parts editor on the same part twice
	return (m_originalModuleID.compare(moduleID) == 0);
}

/*
QString PEMainWindow::getFzpReferenceFile() {
    QString referenceFile = m_fzpDocument.documentElement().attribute(ReferenceFileString);
    if (!referenceFile.isEmpty()) referenceFile += "_";
    return referenceFile;
}
*/

QString PEMainWindow::getSvgReferenceFile(const QString & filename) {
	QFileInfo info(filename);
	QString referenceFile = info.fileName();

	QFile file(filename);
	if (!file.open(QFile::ReadOnly)) return referenceFile;

	QString svg = file.readAll();
	if (!svg.contains(ReferenceFileString)) return referenceFile;

	QXmlStreamReader xml(svg.toUtf8());
	xml.setNamespaceProcessing(false);
	bool inDesc = false;
	while (!xml.atEnd()) {
		switch (xml.readNext()) {
		case QXmlStreamReader::StartElement:
		{
			QString name = xml.name().toString();
			if (inDesc && name.compare(ReferenceFileString) == 0) {
				QString candidate = xml.readElementText().trimmed();
				if (candidate.isEmpty()) return referenceFile;
				return candidate;
			}
			if (name.compare("desc") == 0) {
				inDesc = true;
			}
		}
		break;
		case QXmlStreamReader::EndElement:
		{
			QString name = xml.name().toString();
			if (name.compare("desc") == 0) {
				inDesc = false;
			}
		}
		break;

		default:
			break;
		}
	}

	return referenceFile;
}

QString PEMainWindow::makeDesc(const QString & referenceFile)
{
	return QString("\n<desc><%2>%1</%2></desc>\n").arg(referenceFile).arg(ReferenceFileString);
}

void PEMainWindow::updateWindowMenu() {
}

void PEMainWindow::updateRaiseWindowAction() {
	QString title = tr("Fritzing (New) Parts Editor");
	QString partTitle = getPartTitle();
	QString actionText = QString("%1: %2").arg(title).arg(partTitle);
	m_raiseWindowAct->setText(actionText);
	m_raiseWindowAct->setToolTip(actionText);
	m_raiseWindowAct->setStatusTip("raise \""+actionText+"\" window");
}

bool PEMainWindow::writeXml(const QString & path, const QString & xml, bool temp)
{
	bool result = TextUtils::writeUtf8(path, TextUtils::svgNSOnly(xml));
	if (result) {
		if (temp) m_filesToDelete.append(path);
		else m_filesToDelete.removeAll(path);
	}

	return result;
}

void PEMainWindow::displayBuses() {
	deleteBuses();

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement buses = root.firstChildElement("buses");
	QDomElement bus = buses.firstChildElement("bus");
	while (!bus.isNull()) {
		QDomElement nodeMember = bus.firstChildElement("nodeMember");
		QSet<QString> connectorIDs;
		while (!nodeMember.isNull()) {
			QString connectorID = nodeMember.attribute("connectorId");
			if (!connectorID.isEmpty()) {
				connectorIDs.insert(connectorID);
			}
			nodeMember = nodeMember.nextSiblingElement("nodeMember");
		}

		Q_FOREACH (ViewLayer::ViewID viewID, m_viewThings.keys()) {
			ViewThing * viewThing = m_viewThings.value(viewID);
			QList<ConnectorItem *> connectorItems;
			Q_FOREACH (QString connectorID, connectorIDs) {
				ConnectorItem * connectorItem = viewThing->itemBase->findConnectorItemWithSharedID(connectorID, viewThing->itemBase->viewLayerPlacement());
				if (connectorItem != nullptr) connectorItems.append(connectorItem);
			}
			for (int i = 0; i < connectorItems.count() - 1; i++) {
				ConnectorItem * c1 = connectorItems.at(i);
				ConnectorItem * c2 = connectorItems.at(i + 1);
				Wire * wire = viewThing->sketchWidget->makeOneRatsnestWire(c1, c2, false, QColor(0, 255, 0), true);
				wire->setZValue(RatZ);
			}
		}

		bus = bus.nextSiblingElement("bus");
	}
}

void PEMainWindow::updateWireMenu() {
	// assumes update wire menu is only called when right-clicking a wire
	// and that wire is cached by the menu in Wire::mousePressEvent

	Wire * wire = m_activeWire;
	m_activeWire = nullptr;

	m_deleteBusConnectionAct->setWire(wire);
	m_deleteBusConnectionAct->setEnabled(true);
}

void PEMainWindow::deleteBusConnection() {
	auto * wireAction = qobject_cast<WireAction *>(sender());
	if (wireAction == nullptr) return;

	Wire * wire = wireAction->wire();
	if (wire == nullptr) return;

	QList<Wire *> wires;
	QList<ConnectorItem *> ends;
	wire->collectChained(wires, ends);
	if (ends.count() != 2) return;

	QString id0 = ends.at(0)->connectorSharedID();
	QString id1 = ends.at(1)->connectorSharedID();

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement buses = root.firstChildElement("buses");
	QDomElement bus = buses.firstChildElement("bus");
	QDomElement nodeMember0;
	QDomElement nodeMember1;
	while (!bus.isNull()) {
		nodeMember0 = TextUtils::findElementWithAttribute(bus, "connectorId", id0);
		nodeMember1 = TextUtils::findElementWithAttribute(bus, "connectorId", id1);
		if (!nodeMember0.isNull() && !nodeMember1.isNull()) break;

		bus = bus.nextSiblingElement("bus");
	}

	QString busID = bus.attribute("id");

	if (nodeMember0.isNull() || nodeMember1.isNull()) {
		QMessageBox::critical(nullptr, tr("Parts Editor", "dialog title"), tr("Internal connections are very messed up."));
		return;
	}

	auto * parentCommand = new QUndoCommand();
	QStringList names;
	names << ends.at(0)->connectorSharedName() << ends.at(1)->connectorSharedName() ;
	new RemoveBusConnectorCommand(this, busID, id0, false, parentCommand);
	new RemoveBusConnectorCommand(this, busID, id1, false, parentCommand);
	if (ends.at(0)->connectedToItems().count() > 1) {
		// restore it
		names.removeAt(0);
		new RemoveBusConnectorCommand(this, busID, id0, true, parentCommand);
	}
	if (ends.at(1)->connectedToItems().count() > 1) {
		// restore it
		new RemoveBusConnectorCommand(this, busID, id1, true, parentCommand);
	}

	parentCommand->setText(tr("Remove internal connection from '%1'").arg(names.at(0)));
	m_undoStack->waitPush(parentCommand, SketchWidget::PropChangeDelay);
}

void PEMainWindow::newWireSlot(Wire * wire) {
	wire->setDisplayBendpointCursor(false);
	disconnect(wire, nullptr, m_viewThings.value(wire->viewID())->sketchWidget, nullptr);
	connect(wire, SIGNAL(wireChangedSignal(Wire*, const QLineF &, const QLineF &, QPointF, QPointF, ConnectorItem *, ConnectorItem *)	),
	        this, SLOT(wireChangedSlot(Wire*, const QLineF &, const QLineF &, QPointF, QPointF, ConnectorItem *, ConnectorItem *)),
	        Qt::DirectConnection);		// DirectConnection means call the slot directly like a subroutine, without waiting for a thread or queue
}

void PEMainWindow::wireChangedSlot(Wire* wire, const QLineF &, const QLineF &, QPointF, QPointF, ConnectorItem * fromOnWire, ConnectorItem * to) {
	wire->deleteLater();

	if (to == nullptr) return;

	ConnectorItem * from = wire->otherConnector(fromOnWire)->firstConnectedToIsh();
	if (from == nullptr) return;

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement buses = root.firstChildElement("buses");
	if (buses.isNull()) {
		buses = m_fzpDocument.createElement("buses");
		root.appendChild(buses);
	}


	QDomElement fromBus = findNodeMemberBus(buses, from->connectorSharedID());
	QString fromBusID = fromBus.attribute("id");
	QDomElement toBus = findNodeMemberBus(buses, to->connectorSharedID());
	QString toBusID = toBus.attribute("id");

	QString busID = fromBusID.isEmpty() ? toBusID : fromBusID;
	if (busID.isEmpty()) {
		int theMax = std::numeric_limits<int>::max();
		for (int ix = 1; ix < theMax; ix++) {
			QString candidate = QString("internal%1").arg(ix);
			QDomElement busElement = findBus(buses, candidate);
			if (busElement.isNull()) {
				busID = candidate;
				break;
			}
		}
	}

	auto * parentCommand = new QUndoCommand(tr("Add internal connection from '%1' to '%2'").arg(from->connectorSharedName()).arg(to->connectorSharedName()));
	if (!fromBusID.isEmpty()) {
		// changing the bus for this nodeMember
		new RemoveBusConnectorCommand(this, fromBusID, from->connectorSharedID(), false, parentCommand);
	}
	if (!toBusID.isEmpty()) {
		// changing the bus for this nodeMember
		new RemoveBusConnectorCommand(this, toBusID, to->connectorSharedID(), false, parentCommand);
	}
	new RemoveBusConnectorCommand(this, busID, from->connectorSharedID(), true, parentCommand);
	new RemoveBusConnectorCommand(this, busID, to->connectorSharedID(), true, parentCommand);
	m_undoStack->waitPush(parentCommand, SketchWidget::PropChangeDelay);
}

QDomElement PEMainWindow::findBus(const QDomElement & buses, const QString & id)
{
	QDomElement busElement = buses.firstChildElement("bus");
	while (!busElement.isNull()) {
		if (busElement.attribute("id").compare(id) == 0) {
			return busElement;
		}
		busElement = busElement.nextSiblingElement("bus");
	}

	return QDomElement();
}

QDomElement PEMainWindow::findNodeMemberBus(const QDomElement & buses, const QString & connectorID)
{
	QDomElement bus = buses.firstChildElement("bus");
	while (!bus.isNull()) {
		QDomElement nodeMember = bus.firstChildElement("nodeMember");
		while (!nodeMember.isNull()) {
			if (nodeMember.attribute("connectorId").compare(connectorID) == 0) {
				return bus;
			}
			nodeMember = nodeMember.nextSiblingElement("nodeMember");
		}
		bus = bus.nextSiblingElement("bus");
	}

	return QDomElement();
}

void PEMainWindow::addBusConnector(const QString & busID, const QString & connectorID)
{
	// called from command object
	removeBusConnector(busID, connectorID, false);			// keep the dom very clean

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement buses = root.firstChildElement("buses");
	if (buses.isNull()) {
		m_fzpDocument.createElement("buses");
		root.appendChild(buses);
	}

	QDomElement theBusElement = findBus(buses, busID);
	if (theBusElement.isNull()) {
		theBusElement = m_fzpDocument.createElement("bus");
		theBusElement.setAttribute("id", busID);
		buses.appendChild(theBusElement);
	}

	QDomElement nodeMember = m_fzpDocument.createElement("nodeMember");
	nodeMember.setAttribute("connectorId", connectorID);
	theBusElement.appendChild(nodeMember);
	displayBuses();
}

void PEMainWindow::removeBusConnector(const QString & busID, const QString & connectorID, bool display)
{
	// called from command object
	// for the sake of cleaning, deletes all matching nodeMembers so be careful about the order of deletion and addition within the same parentCommand
	Q_UNUSED(busID);

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement buses = root.firstChildElement("buses");
	QDomElement bus = buses.firstChildElement("bus");
	QList<QDomElement> toDelete;
	while (!bus.isNull()) {
		QDomElement nodeMember = bus.firstChildElement("nodeMember");
		while (!nodeMember.isNull()) {
			if (nodeMember.attribute("connectorId").compare(connectorID) == 0) {
				toDelete.append(nodeMember);
			}
			nodeMember = nodeMember.nextSiblingElement("nodeMember");
		}
		bus = bus.nextSiblingElement("bus");
	}

	Q_FOREACH (QDomElement element, toDelete) {
		element.parentNode().removeChild(element);
	}

	if (display) displayBuses();
}

void PEMainWindow::replaceProperty(const QString & key, const QString & value, QDomElement & properties)
{
	QDomElement prop = properties.firstChildElement("property");
	while (!prop.isNull()) {
		QString name = prop.attribute("name");
		if (name.compare(key, Qt::CaseInsensitive) == 0) {
			TextUtils::replaceChildText(prop, value);
			return;
		}

		prop = prop.nextSiblingElement("property");
	}

	prop = m_fzpDocument.createElement("property");
	properties.appendChild(prop);
	prop.setAttribute("name", key);
	TextUtils::replaceChildText(prop, value);
}

QWidget * PEMainWindow::createTabWidget() {
	auto * tabWidget = new QTabWidget(this);
	tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	tabWidget->setObjectName("pe_tabs");
	return tabWidget;
}

void PEMainWindow::addTab(QWidget * widget, const QString & label) {
	qobject_cast<QTabWidget *>(m_tabWidget)->addTab(widget, label);
	this -> setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

int PEMainWindow::currentTabIndex() {
	return qobject_cast<QTabWidget *>(m_tabWidget)->currentIndex();
}

void PEMainWindow::setCurrentTabIndex(int index) {
	qobject_cast<QTabWidget *>(m_tabWidget)->setCurrentIndex(index);
}

QWidget * PEMainWindow::currentTabWidget() {
	return qobject_cast<QTabWidget *>(m_tabWidget)->currentWidget();
}

bool PEMainWindow::event(QEvent * e) {
	if (e->type() == QEvent::Close) {
		//qDebug() << "event close";
		//if (m_inFocusWidgets.count() > 0) {
		//	e->ignore();
		//	qDebug() << "bail in focus";
		//	return true;
		//}
	}

	return MainWindow::event(e);
}

bool PEMainWindow::eventFilter(QObject *object, QEvent *event)
{
	if (m_inPickMode) {
		switch (event->type()) {
		case QEvent::MouseButtonPress:
			clearPickMode();
			{
				auto * mouseEvent = static_cast<QMouseEvent *>(event);
				m_useNextPick = (mouseEvent->button() == Qt::LeftButton);
			}
			QTimer::singleShot(1, this, SLOT(resetNextPick()));
			break;

		case QEvent::ApplicationActivate:
		case QEvent::ApplicationDeactivate:
		case QEvent::WindowActivate:
		case QEvent::WindowDeactivate:
		case QEvent::NonClientAreaMouseButtonPress:
			clearPickMode();
			break;

		case QEvent::KeyPress:
		{
			auto * kevent = static_cast<QKeyEvent *>(event);
			if (kevent->key() == Qt::Key_Escape) {
				clearPickMode();
				return true;
			}
		}

		default:
			break;
		}

		return false;
	}

	//qDebug() << "event" << event->type();
	if (event->type() == QEvent::FocusIn) {
		auto * lineEdit = qobject_cast<QLineEdit *>(object);
		if (lineEdit != nullptr) {
			if (lineEdit->window() == this) {
				qDebug() << "inc focus";
				m_inFocusWidgets << lineEdit;
			}
		}
		else {
			auto * textEdit = qobject_cast<QTextEdit *>(object);
			if (textEdit != nullptr && textEdit->window() == this) {
				qDebug() << "inc focus";
				m_inFocusWidgets << textEdit;
			}
		}
	}
	if (event->type() == QEvent::FocusOut) {
		auto * lineEdit = qobject_cast<QLineEdit *>(object);
		if (lineEdit != nullptr) {
			if (lineEdit->window() == this) {
				qDebug() << "dec focus";
				m_inFocusWidgets.removeOne(lineEdit);
			}
		}
		else {
			auto * textEdit = qobject_cast<QTextEdit *>(object);
			if (textEdit != nullptr && textEdit->window() == this) {
				qDebug() << "inc focus";
				m_inFocusWidgets.removeOne(textEdit);
			}
		}
	}
	return false;
}

void PEMainWindow::closeLater()
{
	close();
}

void PEMainWindow::resetNextPick() {
	m_useNextPick = false;
}

void PEMainWindow::clearPickMode() {
	qApp->removeEventFilter(this);
	m_useNextPick = m_inPickMode = false;
	QApplication::restoreOverrideCursor();
	if (m_currentGraphicsView != nullptr) {
		Q_FOREACH (QGraphicsItem * item, m_currentGraphicsView->scene()->items()) {
			auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
			if (pegi != nullptr) pegi->setPickAppearance(false);
		}
	}
}

QList<PEGraphicsItem *> PEMainWindow::getPegiList(SketchWidget * sketchWidget) {
	// DebugDialog::debug("-----------------------------");

	QList<PEGraphicsItem *> pegiList;
	Q_FOREACH (QGraphicsItem * item, sketchWidget->scene()->items()) {
		auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
		if (pegi == nullptr) continue;

		pegiList.append(pegi);
		/*
		if (pegi->showingTerminalPoint() || pegi->showingMarquee() || !pegi->element().attribute("id").isEmpty()) {
		    DebugDialog::debug(QString("pegi m:%1 t:%2 %3")
		        .arg(pegi->showingMarquee())
		        .arg(pegi->showingTerminalPoint())
		        .arg(pegi->element().attribute("id")));
		}
		*/
	}

	//DebugDialog::debug("-----------------------------");

	return pegiList;
}

void PEMainWindow::deleteBuses() {
	QList<Wire *> toDelete;
	Q_FOREACH (ViewThing * viewThing, m_viewThings.values()) {
		Q_FOREACH (QGraphicsItem * item, viewThing->sketchWidget->scene()->items()) {
			Wire * wire = dynamic_cast<Wire *>(item);
			if (wire == nullptr) continue;

			toDelete << wire;
		}
	}

	Q_FOREACH (Wire * wire, toDelete) {
		delete wire;
	}
}

void PEMainWindow::connectorsTypeChanged(Connector::ConnectorType ct)
{
	QUndoCommand * parentCommand = nullptr;

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		ConnectorMetadata oldCmd;
		fillInMetadata(connector, oldCmd);
		if (oldCmd.connectorType != ct) {
			if (parentCommand == nullptr) {
				parentCommand = new QUndoCommand(tr("Change all connectors to %1").arg(Connector::connectorNameFromType(ct)));
			}
			ConnectorMetadata cmd = oldCmd;
			cmd.connectorType = ct;
			new ChangeConnectorMetadataCommand(this, &oldCmd, &cmd, parentCommand);
		}

		connector = connector.nextSiblingElement("connector");
	}

	if (parentCommand != nullptr) {
		m_undoStack->waitPush(parentCommand, SketchWidget::PropChangeDelay);
	}
}

void PEMainWindow::fillInMetadata(const QDomElement & connector, ConnectorMetadata & cmd)
{
	cmd.connectorID = connector.attribute("id");
	cmd.connectorType = Connector::connectorTypeFromName(connector.attribute("type"));
	cmd.connectorName = connector.attribute("name");
	QDomElement description = connector.firstChildElement("description");
	cmd.connectorDescription = description.text();
}

void PEMainWindow::smdChanged(const QString & after) {
	QString before;
	bool toSMD = true;
	if (after.compare("smd", Qt::CaseInsensitive) != 0) {
		before = "smd";
		toSMD = false;
	}

	ViewThing * viewThing = m_viewThings.value(ViewLayer::PCBView);
	ItemBase * itemBase = viewThing->itemBase;
	if (itemBase == nullptr) return;

	QFile file(itemBase->filename());
	if (!file.open(QIODevice::ReadOnly)) {
		DebugDialog::debug(QString("Unable to open :%1").arg(itemBase->filename()));
	}
	QDomDocument svgDoc;
	svgDoc.setContent(&file);
	QDomElement svgRoot = svgDoc.documentElement();
	QDomElement svgCopper0 = TextUtils::findElementWithAttribute(svgRoot, "id", "copper0");
	QDomElement svgCopper1 = TextUtils::findElementWithAttribute(svgRoot, "id", "copper1");
	if (svgCopper0.isNull() && svgCopper1.isNull()) {
		QMessageBox::critical(nullptr, tr("Parts Editor", "dialog title"), tr("Unable to parse '%1'").arg(itemBase->filename()));
		return;
	}

	if (toSMD) {
		if (svgCopper0.isNull() && !svgCopper1.isNull()) {
			// everything is fine
		}
		else if (svgCopper1.isNull()) {
			// just needs a swap
			svgCopper0.setAttribute("id", "copper1");
		}
		else {
			svgCopper0.removeAttribute("id");
		}
	}
	else {
		if (!svgCopper0.isNull() && !svgCopper1.isNull()) {
			// everything is fine
		}
		else if (svgCopper1.isNull()) {
			svgCopper1 = m_pcbDocument.createElement("g");
			svgCopper1.setAttribute("id", "copper1");
			svgRoot.appendChild(svgCopper1);
			QDomElement child = svgCopper0.firstChildElement();
			while (!child.isNull()) {
				QDomElement next = child.nextSiblingElement();
				svgCopper1.appendChild(child);
				child = next;
			}
			svgCopper0.appendChild(svgCopper1);
		}
		else {
			svgCopper0 = m_pcbDocument.createElement("g");
			svgCopper0.setAttribute("id", "copper0");
			svgRoot.appendChild(svgCopper0);
			QDomElement child = svgCopper1.firstChildElement();
			while (!child.isNull()) {
				QDomElement next = child.nextSiblingElement();
				svgCopper0.appendChild(child);
				child = next;
			}
			svgCopper1.appendChild(svgCopper0);
		}
	}

	QString newPath = m_userPartsFolderSvgPath + makeSvgPath2(m_pcbGraphicsView);
	FolderUtils::ensureDirectoryExists(newPath);
	QString svg = TextUtils::svgNSOnly(svgDoc.toString());
	writeXml(newPath, removeGorn(svg), true);

	auto * csc = new ChangeSMDCommand(this, before, after, itemBase->filename(), newPath, nullptr);
	csc->setText(tr("Change to %1").arg(after));
	m_undoStack->waitPush(csc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::changeSMD(const QString & after, const QString & filename, int changeDirection)
{
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement views = root.firstChildElement("views");
	QDomElement pcbView = views.firstChildElement("pcbView");
	QDomElement layers = pcbView.firstChildElement("layers");
	QDomElement copper0 = TextUtils::findElementWithAttribute(layers, "layerId", "copper0");
	QDomElement copper1 = TextUtils::findElementWithAttribute(layers, "layerId", "copper1");
	bool toSMD = true;
	if (after.compare("smd", Qt::CaseInsensitive) == 0) {
		if (!copper0.isNull()) {
			copper0.parentNode().removeChild(copper0);
		}
	}
	else {
		toSMD = false;
		if (copper0.isNull()) {
			copper0 = m_fzpDocument.createElement("layer");
			copper0.setAttribute("layerId", "copper0");
			layers.appendChild(copper0);
		}
	}
	if (copper1.isNull()) {
		copper1 = m_fzpDocument.createElement("layer");
		copper1.setAttribute("layerId", "copper1");
		layers.appendChild(copper1);
	}

	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		setConnectorSMD(toSMD, connector);
		connector = connector.nextSiblingElement("connector");
	}

	changeSvg(m_pcbGraphicsView, filename, changeDirection);
}

void PEMainWindow::setConnectorSMD(bool toSMD, QDomElement & connector) {
	QDomElement views = connector.firstChildElement("views");
	QDomElement pcbView = views.firstChildElement("pcbView");
	QDomElement copper0 = TextUtils::findElementWithAttribute(pcbView, "layer", "copper0");
	QDomElement copper1 = TextUtils::findElementWithAttribute(pcbView, "layer", "copper1");
	if (copper0.isNull() && copper1.isNull()) {
		// SVG is seriously messed up
		DebugDialog::debug("setting SMD is very broken");
		return;
	}

	if (toSMD) {
		if (copper0.isNull() && !copper1.isNull()) {
			// already correct
			return;
		}
		if (copper1.isNull()) {
			// swap it to copper1
			copper0.setAttribute("layer", "copper1");
			return;
		}
		// remove the extra layer
		copper0.parentNode().removeChild(copper0);
		return;
	}

	if (!copper0.isNull() && !copper1.isNull()) {
		// already correct
		return;
	}

	if (copper1.isNull()) {
		copper1 = copper0.cloneNode(true).toElement();
		copper0.parentNode().appendChild(copper1);
		copper1.setAttribute("layer", "copper1");
		return;
	}

	copper0 = copper1.cloneNode(true).toElement();
	copper1.parentNode().appendChild(copper0);
	copper0.setAttribute("layer", "copper0");
}


void PEMainWindow::reuseBreadboard()
{
	reuseImage(ViewLayer::BreadboardView);
}

void PEMainWindow::reuseSchematic()
{
	reuseImage(ViewLayer::SchematicView);
}

void PEMainWindow::reusePCB()
{
	reuseImage(ViewLayer::PCBView);
}

void PEMainWindow::reuseImage(ViewLayer::ViewID viewID) {
	if (m_currentGraphicsView == nullptr) return;

	ViewThing * afterViewThing = m_viewThings.value(viewID);
	if (afterViewThing->itemBase == nullptr) return;

	QString afterFilename = afterViewThing->itemBase->filename();

	ViewThing * beforeViewThing = m_viewThings.value(m_currentGraphicsView->viewID());

	auto * csc = new ChangeSvgCommand(this, m_currentGraphicsView, beforeViewThing->itemBase->filename(), afterFilename, nullptr);
	QFileInfo info(afterFilename);
	csc->setText(QString("Load '%1'").arg(info.fileName()));
	m_undoStack->waitPush(csc, SketchWidget::PropChangeDelay);
}

void PEMainWindow::updateFileMenu() {
	MainWindow::updateFileMenu();

	m_saveAct->setEnabled(canSave());

	/*
	QHash<ViewLayer::ViewID, bool> enabled;
	enabled.insert(ViewLayer::BreadboardView, true);
	enabled.insert(ViewLayer::SchematicView, true);
	enabled.insert(ViewLayer::PCBView, true);
	bool enableAll = true;
	if (m_currentGraphicsView == nullptr) {
		enableAll = false;
	}
	else {
		ViewLayer::ViewID viewID = m_currentGraphicsView->viewID();
		enabled.insert(viewID, false);
	}

	m_reuseBreadboardAct->setEnabled(enableAll && enabled.value(ViewLayer::BreadboardView));
	m_reuseSchematicAct->setEnabled(enableAll && enabled.value(ViewLayer::SchematicView));
	m_reusePCBAct->setEnabled(enableAll && enabled.value(ViewLayer::PCBView));
	*/

	bool enabled = m_currentGraphicsView != nullptr && m_currentGraphicsView->viewID() == ViewLayer::IconView;
	m_reuseBreadboardAct->setEnabled(enabled);
	m_reuseSchematicAct->setEnabled(enabled);
	m_reusePCBAct->setEnabled(enabled);
	// P6: create-from-blank is always available; svg-import needs an active view to land the graphic in.
	if (m_importSvgAct != nullptr) m_importSvgAct->setEnabled(m_currentGraphicsView != nullptr);
}

void PEMainWindow::setImageAttribute(QDomElement & fzpRoot, const QString & svgPath, ViewLayer::ViewID viewID)
{
	QDomElement views = fzpRoot.firstChildElement("views");
	QDomElement view = views.firstChildElement(ViewLayer::viewIDXmlName(viewID));
	QDomElement layers = view.firstChildElement("layers");
	QFileInfo info(svgPath);
	QDir dir = info.absoluteDir();
	QString shortName = dir.dirName() + "/" + info.fileName();
	setImageAttribute(layers, shortName);
}

void PEMainWindow::setImageAttribute(QDomElement & layers, const QString & svgPath)
{
	layers.setAttribute("image", svgPath);
	QDomElement layer = layers.firstChildElement("layer");
	if (!layer.isNull()) return;

	layer = m_fzpDocument.createElement("layer");
	layers.appendChild(layer);
	layer.setAttribute("layerId", ViewLayer::viewLayerXmlNameFromID(ViewLayer::UnknownLayer));
}

void PEMainWindow::updateLayerMenu(bool resetLayout) {
	MainWindow::updateLayerMenu(resetLayout);

	bool enabled = false;
	if (m_currentGraphicsView != nullptr) {
		switch (m_currentGraphicsView->viewID()) {
		case ViewLayer::BreadboardView:
		case ViewLayer::SchematicView:
		case ViewLayer::PCBView:
			enabled = true;
		default:
			break;
		}
	}

	m_hideOtherViewsAct->setEnabled(enabled);
}

void PEMainWindow::hideOtherViews() {
	if (m_currentGraphicsView == nullptr) return;

	ViewLayer::ViewID afterViewID = m_currentGraphicsView->viewID();
	ItemBase * afterItemBase = m_viewThings.value(afterViewID)->itemBase;
	if (afterItemBase == nullptr) return;
	QString afterFilename = afterItemBase->filename();

	QList<ViewLayer::ViewID> viewIDList;
	viewIDList << ViewLayer::BreadboardView << ViewLayer::SchematicView << ViewLayer::PCBView;
	viewIDList.removeOne(afterViewID);

	QString originalPath = saveFzp();

	QString afterViewName = ViewLayer::viewIDXmlName(afterViewID);
	QStringList beforeViewNames;
	Q_FOREACH (ViewLayer::ViewID viewID, viewIDList) {
		beforeViewNames << ViewLayer::viewIDXmlName(viewID);
	}

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		QDomElement views = connector.firstChildElement("views");
		QDomElement afterView = views.firstChildElement(afterViewName);

		Q_FOREACH (QString name, beforeViewNames) {
			QDomElement toRemove = views.firstChildElement(name);
			if (!toRemove.isNull()) {
				toRemove.parentNode().removeChild(toRemove);
			}
			QDomElement toReplace = afterView.cloneNode(true).toElement();
			toReplace.setTagName(name);
			views.appendChild(toReplace);
		}

		connector = connector.nextSiblingElement("connector");
	}

	QDomElement views = root.firstChildElement("views");
	QDomElement afterView = views.firstChildElement(afterViewName);
	Q_FOREACH (QString name, beforeViewNames) {
		QDomElement toRemove = views.firstChildElement(name);
		if (!toRemove.isNull()) {
			toRemove.parentNode().removeChild(toRemove);
		}
		QDomElement toReplace = afterView.cloneNode(true).toElement();
		toReplace.setTagName(name);
		views.appendChild(toReplace);
	}

	QString newPath = saveFzp();
	auto * cfc = new ChangeFzpCommand(this, originalPath, newPath, nullptr);
	cfc->setText(tr("Make only %1 view visible").arg(m_currentGraphicsView->viewName()));
	m_undoStack->waitPush(cfc, SketchWidget::PropChangeDelay);
}

QString PEMainWindow::makeNewVariant(const QString & family)
{
	QStringList variants = m_referenceModel->propValues(family, "variant", true);
	int theMax = std::numeric_limits<int>::max();
	QString candidate;
	for (int i = 1; i < theMax; i++) {
		candidate = QString("variant %1").arg(i);
		if (!variants.contains(candidate, Qt::CaseInsensitive)) break;
	}

	return candidate;
}

void PEMainWindow::updateAssignedConnectors() {
	if (m_currentGraphicsView == nullptr) return;

	QDomDocument * doc = m_viewThings.value(m_currentGraphicsView->viewID())->document;
	if (doc != nullptr) m_peToolView->showAssignedConnectors(doc, m_currentGraphicsView->viewID());
}

void PEMainWindow::connectorWarning() {
	QHash<ViewLayer::ViewID, int> unassigned;
	Q_FOREACH (ViewLayer::ViewID viewID, m_viewThings.keys()) {
		unassigned.insert(viewID, 0);
	}
	int unassignedTotal = 0;

	QDomElement fzpRoot = m_fzpDocument.documentElement();
	QDomElement connectors = fzpRoot.firstChildElement("connectors");
	Q_FOREACH (ViewLayer::ViewID viewID, m_viewThings.keys()) {
		if (viewID == ViewLayer::IconView) continue;

		QDomDocument * svgDoc = m_viewThings.value(viewID)->document;
		QDomElement svgRoot = svgDoc->documentElement();
		QDomElement connector = connectors.firstChildElement("connector");
		while (!connector.isNull()) {
			QString svgID, terminalID;
			if (ViewLayer::getConnectorSvgIDs(connector, viewID, svgID, terminalID)) {
				QDomElement element = TextUtils::findElementWithAttribute(svgRoot, "id", svgID);
				if (element.isNull()) {
					unassigned.insert(viewID, 1 + unassigned.value(viewID));
					unassignedTotal++;
				}
			}
			else {
				unassigned.insert(viewID, 1 + unassigned.value(viewID));
				unassignedTotal++;
			}

			connector = connector.nextSiblingElement("connector");
		}
	}

	if (unassignedTotal > 0) {
		int viewCount = 0;
		Q_FOREACH (ViewLayer::ViewID viewID, unassigned.keys()) {
			if (unassigned.value(viewID) > 0) viewCount++;
		}
		QMessageBox::warning(nullptr, tr("Parts Editor", "dialog title"),
		                     tr("This part has %n unassigned connector(s). ", "", unassignedTotal) +
		                     tr("This affects %n view(s). ", "", viewCount) +
		                     tr("Until all connectors are assigned to SVG elements, the part will not work correctly. "
		                        "Exiting the Parts Editor now is fine, as long as you remember to finish the assignments later.")
		                    );
	}

}

void PEMainWindow::showing(SketchWidget * sketchWidget) {
	ViewThing * viewThing = m_viewThings.value(sketchWidget->viewID());
	if (viewThing->firstTime) {
		viewThing->firstTime = false;
		QPointF offset = viewThing->sketchWidget->alignOneToGrid(viewThing->itemBase);
		if (offset.x() != 0 || offset.y() != 0) {
			QList<PEGraphicsItem *> pegiList = getPegiList(sketchWidget);
			Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
				pegi->setPos(pegi->pos() + offset);
				pegi->setOffset(pegi->offset() + offset);
			}
		}
	}
}

bool PEMainWindow::anyMarquee() {
	if (m_currentGraphicsView == nullptr) return false;

	QList<PEGraphicsItem *> pegiList = getPegiList(m_currentGraphicsView);
	Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
		if (pegi->showingMarquee()) {
			return true;
		}
	}

	return false;
}

bool PEMainWindow::anyVisible() {
	if (m_currentGraphicsView == nullptr) return false;

	Q_FOREACH (QGraphicsItem * item, m_currentGraphicsView->scene()->items()) {
		auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
		if (pegi == nullptr) continue;

		return pegi->isVisible();
	}

	return false;
}

void PEMainWindow::changeReferenceFile(ViewLayer::ViewID viewID, const QString referenceFile)
{
	ViewThing * viewThing = m_viewThings.value(viewID);
	if (viewThing == nullptr) {
		// shouldn't happen
		DebugDialog::debug(QString("missing view thing for %1").arg(viewID));
		return;
	}

	viewThing->referenceFile = referenceFile;
}

void PEMainWindow::insertDesc(const QString & referenceFile, QString & svg) {
	if (svg.contains(ReferenceFileString)) return;

	int ix = svg.indexOf("<svg");
	if (ix >= 0) {
		int jx = svg.indexOf(">", ix);
		if (jx > ix) {
			svg.insert(jx + 1, makeDesc(referenceFile));
		}
	}
}

void PEMainWindow::itemAddedSlot(ModelPart *, ItemBase * itemBase, ViewLayer::ViewLayerPlacement, const ViewGeometry &, long id, SketchWidget *) {
	Q_UNUSED(id);

	if (itemBase == nullptr) return;
	if (m_currentGraphicsView == nullptr) return;
	if (itemBase->viewID() != m_currentGraphicsView->viewID()) return;

	// M0.4: this used to call makePegi() with a default-constructed (null) QDomElement. The result was a
	// live, interactive PEGraphicsItem with no backing graphic: it rendered nothing (shapeFromElement has
	// no attrs to read), could not be moved (pegiGraphicMoveDelta bails on an empty id), and yet it was
	// movable/selectable and returned by getPegiList() -- so it swallowed clicks and skewed every
	// "highest z" / visibility / connector-lookup pass over that list.
	//
	// The graphics items are derived from the part's SVG document in initSvgTree(), which is the single
	// place items are created; an ItemBase added to the scene is not itself a modeled graphic, so there
	// is nothing to add here. Resync from the document instead, which is idempotent and keeps the scene
	// consistent with the source of truth.
	if (ViewThing * viewThing = m_viewThings.value(itemBase->viewID())) {
		if (viewThing->document != nullptr && viewThing->itemBase == itemBase) {
			initSvgTree(viewThing->sketchWidget, viewThing->itemBase, *viewThing->document);
		}
	}
}

void PEMainWindow::itemMovedSlot(ItemBase * itemBase) {
	if (itemBase == nullptr) return;
	if (itemBase->viewID() != m_currentGraphicsView->viewID()) return;

	// Graphic moves are owned by PEGraphicsItem::graphicMoveDelta -> pegiGraphicMoveDelta, which knows
	// exactly which element was dragged and carries a user-unit delta. This slot cannot recover either:
	// every interactive graphic in a view shares one itemBase, so "the first pegi whose itemBase()
	// matches" is an arbitrary element, and itemBase->pos() is the PART's scene position, not the drag
	// delta. Treating it as one re-issued a bogus moveGraphic for whichever element came first on every
	// drop -- the second, competing edit behind "something moves around". Reposition the overlays so
	// they follow the part, and stop there.
	QList<PEGraphicsItem *> pegiList = getPegiList(m_currentGraphicsView);
	Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
		if (pegi->itemBase() != itemBase) continue;
		// keep the overlay grip on top of the moved graphic
		pegi->setPos(itemBase->pos() + pegi->offset());
	}
}

void PEMainWindow::resizedSlot(ItemBase * itemBase) {
	if (itemBase == nullptr) return;
	if (itemBase->viewID() != m_currentGraphicsView->viewID()) return;

	QList<PEGraphicsItem *> pegiList = getPegiList(m_currentGraphicsView);
	QDomElement element;
	Q_FOREACH (PEGraphicsItem * pegi, pegiList) {
		if (pegi->itemBase() != itemBase) continue;
		pegi->setPos(itemBase->pos() + pegi->offset());
		QRectF bounds = itemBase->boundingRect();
		pegi->setRect(bounds);
		element = pegi->element();
	}
	if (element.isNull()) return;
	QString svgID = element.attribute("id");
	if (svgID.isEmpty()) return;
	SketchWidget * sketchWidget = m_currentGraphicsView;
	QRectF before = element.attribute("x").isEmpty() ? QRectF()
	                           : QRectF(element.attribute("x").toDouble(), element.attribute("y").toDouble(),
	                                    element.attribute("width").toDouble(), element.attribute("height").toDouble());
	QRectF to = itemBase->boundingRect();
	auto * command = new ResizeGraphicCommand(this, sketchWidget, svgID, before, to, nullptr);
	command->setText(tr("Resize graphic"));
	m_undoStack->waitPush(command, SketchWidget::PropChangeDelay);
}

void PEMainWindow::clickedItemCandidateSlot(QGraphicsItem * item, bool & ok) {
	auto * pegi = dynamic_cast<PEGraphicsItem *>(item);
	if (pegi == nullptr) {
		ok = true;
		return;
	}

	ok = pegi->showingMarquee();
}

void PEMainWindow::initProgrammingWidget() {
}

void PEMainWindow::initWelcomeView() {
}

void PEMainWindow::setInitialView() {
	// do this the first time, since the current_changed signal wasn't sent
	int tab = 0;
	tabWidget_currentChanged(tab+1);
	tabWidget_currentChanged(tab);
}

void PEMainWindow::updateExportMenu() {
	bool enabled = (m_currentGraphicsView != nullptr) || currentTabIndex() == IconViewIndex;
	Q_FOREACH (QAction * action, m_exportMenu->actions()) {
		action->setEnabled(enabled);
	}
}

void PEMainWindow::convertToTenth() {
	if (m_currentGraphicsView == nullptr) return;
	if (m_currentGraphicsView->viewID() != ViewLayer::SchematicView) return;

	QString originalFzpPath = saveFzp();
	QString newFzpPath = saveFzp();

	ViewThing * viewThing = m_viewThings.value(m_currentGraphicsView->viewID());
	QString originalSvgPath = viewThing->itemBase->filename();
	QString newSvgPath = m_userPartsFolderSvgPath + makeSvgPath2(m_currentGraphicsView);
	FolderUtils::ensureDirectoryExists(newSvgPath);
	QFile::copy(originalSvgPath, newSvgPath);

	S2S s2s(false);
	connect(&s2s, SIGNAL(messageSignal(const QString &)), this, SLOT(s2sMessageSlot(const QString &)));
	bool result = s2s.onefzp(newFzpPath, newSvgPath);

	if (!result) return;          // if conversion fails

	auto * parentCommand = new QUndoCommand("Convert Schematic");
	new ChangeFzpCommand(this, originalFzpPath, newFzpPath, parentCommand);
	new ChangeSvgCommand(this, m_currentGraphicsView, originalSvgPath, newSvgPath, parentCommand);
	m_undoStack->waitPush(parentCommand, SketchWidget::PropChangeDelay);
}

void PEMainWindow::s2sMessageSlot(const QString & message) {
	QMessageBox::information(this, "Schematic Conversion", message);
}

void PEMainWindow::updateEditMenu() {
	MainWindow::updateEditMenu();
	m_convertToTenthAct->setEnabled(m_currentGraphicsView != nullptr && m_currentGraphicsView->viewID() == ViewLayer::SchematicView);
}
