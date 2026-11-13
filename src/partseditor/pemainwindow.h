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

#ifndef PEMAINWINDOW_H_
#define PEMAINWINDOW_H_

#include <QList>
#include <QDomDocument>

#include "../mainwindow/mainwindow.h"
#include "../model/modelpartshared.h"
#include "../utils/misc.h"
#include "peconnectorsview.h"
#include "pesvgeditor.h"

class PEMetadataView;
class PaletteItem;

// The Parts Editor, rebuilt around the sole-renderer (docs/design/pe-integration-plan.md). The central view
// area is a QTabWidget: one PESvgEditor tab per view (Breadboard / Schematic / PCB / Icon), each painting its
// own QDomDocument directly, plus Metadata and Connectors tabs. There is NO SketchWidget/ItemBase/ViewThing
// spine: each per-view QDomDocument (m_breadboardDocument / m_schematicDocument / m_pcbDocument / m_iconDocument)
// is the sole model of that graphic; PEViewWriter serializes it on save. The Metadata and Connectors docks
// are DOM-in/DOM-out and are reused as-is, driven straight from m_fzpDocument.
class PEMainWindow : public MainWindow
{
	Q_OBJECT

public:
	PEMainWindow(ReferenceModel * referenceModel, QWidget * parent);
	~PEMainWindow() override;

	// Sole-renderer entry point. Loads the part's fzp into m_fzpDocument and each view's graphic into the
	// corresponding view QDomDocument (resolving the <layers image> svg), feeding each into its own PESvgEditor
	// tab, then shows the current view. Returns false if nothing could be loaded.
	bool setInitialItem(PaletteItem *);

	// Clean metadata DOM mutations (operate on m_fzpDocument, re-feed PEMetadataView).
	void changeTags(const QStringList &, bool updateDisplay);
	void changeHistory(const QList<HistoryEntry> &, bool updateDisplay);
	void changeProperties(const QHash<QString, QString> &, bool updateDisplay);
	void changeMetadata(const QString & name, const QString & value, bool updateDisplay);
	// Clean connector DOM mutation (operate on m_fzpDocument, re-feed the connector editors).
	void changeConnectorMetadata(ConnectorMetadata *, bool updateDisplay);

	// Restore m_fzpDocument from a saved temp fzp (used by ChangeFzpCommand undo/redo), refreshing the display.
	void restoreFzp(const QString & fzpPath);

	bool editsModuleID(const QString &);

	// PE-specific window setup. The base MainWindow::init wires the legacy SketchWidget/graphics-view/welcome/
	// programming/layer-palette/bin-manager spine (all null here, hence the crash on PE open). PE overrides it:
	// it only needs the central tab host (built by initSketchWidgets) and an active undo group wired to this
	// window's stack. The graphic area is one PESvgEditor tab per view plus the DOM-driven Metadata and Connectors tabs.
	void init(ReferenceModel * referenceModel, bool lockFiles) override;

public Q_SLOTS:
	void metadataChanged(const QString & name, const QString & value);
	void propertiesChanged(const QHash<QString, QString> &);
	void tagsChanged(const QStringList &);
	void historyChanged(const QList<HistoryEntry> &);
	void connectorMetadataChanged(struct ConnectorMetadata *);
	void removedConnectors(QList<ConnectorMetadata *> &);
	void switchedConnector(int);
	void removedConnector(const QDomElement &);
	void connectorCountChanged(int);
	void connectorsTypeChanged(Connector::ConnectorType);

protected:
	// Sole-renderer central area: a QTabWidget hosting the single PESvgEditor graphic page.
	void initSketchWidgets(bool withIcons) override;
	void setInitialView() override;

	void closeEvent(QCloseEvent *) override;

protected:
	bool save();
	bool saveAs();
	bool saveAs(bool overWrite);
	void tabWidget_currentChanged(int index) override;
	void updateWindowMenu() override;
	void updateEditMenu() override;

protected:
	void setTitle();
	QHash<QString, QString> getOldProperties();
	// Read/write the flat <history> children of an FZP module root. readHistory synthesizes a single oldest
	// entry from <date>/<author> for legacy parts that have no <history> yet; writeHistory rewrites the
	// <history> children sorted oldest->newest and mirrors the newest into the top-level <author>/<date>.
	static QList<HistoryEntry> readHistory(const QDomElement & root);
	static void writeHistory(QDomElement & root, const QList<HistoryEntry> & history);
	// Load a part's fzp into m_fzpDocument.
	bool loadFzp(const QString & path);
	QDomElement findConnector(const QString & id, int & index);
	void removedConnectorsAux(QList<QDomElement> & connectors);
	void changeConnectorElement(QDomElement & connector, ConnectorMetadata *);

	// Return a reference to the view QDomDocument for @p viewID (the sole model of that graphic).
	QDomDocument & viewDocument(ViewLayer::ViewID viewID);
	// The PESvgEditor tab hosting @p viewID, or nullptr if none.
	PESvgEditor * editorFor(ViewLayer::ViewID viewID);
	// The ViewID a graphic tab's PESvgEditor was created for (set via setViewId), else InvalidView.
	static ViewLayer::ViewID editorViewId(const QWidget * tab);
	// Human-readable tab/label name for a view id (Breadboard / Schematic / PCB / Icon).
	QString viewName(ViewLayer::ViewID viewID);
	// Resolve the <layers image> target for @p viewID relative to the loaded fzp and load it into both the
	// view QDomDocument and that view's PESvgEditor.
	bool loadViewDocument(ViewLayer::ViewID viewID);
	void setCurrentViewId(ViewLayer::ViewID viewID);

	QString makeSvgFileName(const QDomElement & moduleIDElement);
	QString saveFzp();
	void initConnectors(bool updateConnectorsView);
	void fillInMetadata(const QDomElement & connector, ConnectorMetadata & cmd);
	void setConnectorSMD(bool toSMD, QDomElement & connector);
	void updateAssignedConnectors();
	void setImageAttribute(QDomElement & layers, const QString & image);
	void setImageAttribute(QDomElement & fzpRoot, const QString & svgPath, ViewLayer::ViewID viewID);
	QString makeNewVariant(const QString & family);
	QString makeDirName();
	QString getPartTitle();
	QString getSvgReferenceFile();
	bool writeXml(const QString & path, const QString & text, bool temp);
	void replaceProperty(const QString & key, const QString & value, QDomElement & properties);
	QWidget * createTabWidget();
	void addTab(QWidget * widget, const QString & label);
	int currentTabIndex();
	void setCurrentTabIndex(int);
	QWidget * currentTabWidget();
	void changeSpecialProperty(const QString & name, const QString & value);
	bool canSave();


protected:
	// m_fzpDocument is the authoritative module metadata. Each view QDomDocument is the authoritative
	// graphic for that view, edited in place by the PESvgEditor and serialized by PEViewWriter on save.
	QDomDocument m_fzpDocument;
	QDomDocument m_iconDocument;
	QDomDocument m_breadboardDocument;
	QDomDocument m_schematicDocument;
	QDomDocument m_pcbDocument;

	PEMetadataView * m_metadataView = nullptr;
	PEConnectorsView * m_connectorsView = nullptr;

	// One sole-renderer surface per view (Breadboard / Schematic / PCB / Icon), each a tab, each editing
	// its own view QDomDocument. Keyed by ViewID; tab order follows m_viewOrder.
	QHash<ViewLayer::ViewID, PESvgEditor *> m_editors;

	QString m_guid;
	QString m_prefix;
	int m_fileIndex;
	// View ids in tab order (Breadboard, Schematic, PCB, Icon) -- iterated consistently across the class.
	QList<ViewLayer::ViewID> m_viewOrder;
	ViewLayer::ViewID m_currentViewId;
	QString m_userPartsFolderPath;
	QString m_userPartsFolderSvgPath;
	bool m_canSave;
	QString m_originalFzpPath;
	QString m_originalModuleID;
	bool m_gaveSaveWarning;
	QStringList m_filesToDelete;
	QList<QDomElement> m_connectorList;
	QString m_removedConnector;
};


#endif /* PEMAINWINDOW_H_ */

