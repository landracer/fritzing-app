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

#include "pemainwindow.h"

#include "pemetadataview.h"
#include "peconnectorsview.h"
#include "pecommands.h"
#include "pehistoryentrydialog.h"
#include "petoolview.h"
#include "pesvgpartloader.h"
#include "peviewwriter.h"

#include "../debugdialog.h"
#include "../referencemodel/referencemodel.h"
#include "../utils/textutils.h"
#include "../utils/folderutils.h"
#include "../items/paletteitem.h"
#include "../model/modelpart.h"

#include <QtDebug>
#include <QInputDialog>
#include <QApplication>
#include <QTabWidget>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDate>
#include <QRegularExpression>

#include <algorithm>
#include <limits>

static const QString ReferenceFileString("referenceFile");

// Change-metadata edits are coalesced onto the undo stack after this many ms (was SketchWidget::PropChangeDelay).
static constexpr int PropChangeDelay = 100;

// Sort connector <connector> elements by the integer embedded in their id/name (ascending).
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
	match = QRegularExpressionMatch();
	ix = c2.attribute("name").indexOf(IntegerFinder, 0, &match);
	if (ix >= 0) c2id = qMax(c2id, match.captured(0).toInt());

	return c1id < c2id;
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

////////////////////////////////////////////////////
// Construction / central tab host

PEMainWindow::PEMainWindow(ReferenceModel * referenceModel, QWidget * parent)
	: MainWindow(referenceModel, parent),
	m_guid(TextUtils::getRandText()),
	m_prefix("prefix0000"),
	m_fileIndex(0),
	m_currentViewId(ViewLayer::BreadboardView),
	m_canSave(false),
	m_gaveSaveWarning(false)
{
	m_viewOrder << ViewLayer::BreadboardView << ViewLayer::SchematicView
	            << ViewLayer::PCBView << ViewLayer::IconView;

	m_userPartsFolderPath = FolderUtils::getLocalPartsPath() + "/user/";
	m_userPartsFolderSvgPath = m_userPartsFolderPath;
}

PEMainWindow::~PEMainWindow()
{
	Q_FOREACH (QString string, m_filesToDelete) {
		QFile::remove(string);
	}
	QDir dir = QDir::temp();
	dir.rmdir(makeDirName());
}

// Minimal PE setup. Unlike the base MainWindow::init (which wires the legacy SketchWidget/graphics-view/
// welcome/programming/layer-palette/bin-manager spine that this sole-renderer path deliberately has none of),
// the parts editor only needs a central tab host (filled by setInitialView -> setInitialItem) plus an active
// undo group wired to this window's stack. Everything else in the base init dereferences null legacy members.
void PEMainWindow::init(ReferenceModel * referenceModel, bool)
{
	m_referenceModel = referenceModel;

	m_tabWidget = createTabWidget();
	setCentralWidget(m_tabWidget);

	m_undoView = new QUndoView();
	m_undoGroup = new QUndoGroup(this);
	m_undoView->setGroup(m_undoGroup);
	m_undoGroup->setActiveStack(m_undoStack);

	setInitialView();
}


void PEMainWindow::closeEvent(QCloseEvent * event)
{
	QMainWindow::closeEvent(event);
}

QWidget * PEMainWindow::createTabWidget() {
	auto * tabWidget = new QTabWidget(this);
	tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	tabWidget->setObjectName("pe_tabs");
	connect(tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabWidget_currentChanged(int)));
	return tabWidget;
}

void PEMainWindow::addTab(QWidget * widget, const QString & label) {
	qobject_cast<QTabWidget *>(m_tabWidget)->addTab(widget, label);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

void PEMainWindow::initSketchWidgets(bool) {
	setCentralWidget(createTabWidget());

	m_editor = new PESvgEditor(this);
	addTab(m_editor, tr("Graphic"));

	m_metadataView = new PEMetadataView(m_referenceModel, this);
	addTab(m_metadataView, tr("Metadata"));
	connect(m_metadataView, SIGNAL(metadataChanged(const QString &, const QString &)), this, SLOT(metadataChanged(const QString &, const QString &)), Qt::DirectConnection);
	connect(m_metadataView, SIGNAL(tagsChanged(const QStringList &)), this, SLOT(tagsChanged(const QStringList &)), Qt::DirectConnection);
	connect(m_metadataView, SIGNAL(propertiesChanged(const QHash<QString, QString> &)), this, SLOT(propertiesChanged(const QHash<QString, QString> &)), Qt::DirectConnection);
	connect(m_metadataView, SIGNAL(historyChanged(const QList<HistoryEntry> &)), this, SLOT(historyChanged(const QList<HistoryEntry> &)), Qt::DirectConnection);

	m_connectorsView = new PEConnectorsView(this);
	addTab(m_connectorsView, tr("Connectors"));
	connect(m_connectorsView, SIGNAL(connectorMetadataChanged(ConnectorMetadata *)), this, SLOT(connectorMetadataChanged(ConnectorMetadata *)), Qt::DirectConnection);
	connect(m_connectorsView, SIGNAL(connectorsTypeChanged(Connector::ConnectorType)), this, SLOT(connectorsTypeChanged(Connector::ConnectorType)), Qt::DirectConnection);
	connect(m_connectorsView, SIGNAL(removedConnectors(QList<ConnectorMetadata *> &)), this, SLOT(removedConnectors(QList<ConnectorMetadata *> &)), Qt::DirectConnection);
	connect(m_connectorsView, SIGNAL(connectorCountChanged(int)), this, SLOT(connectorCountChanged(int)), Qt::DirectConnection);
}

void PEMainWindow::setInitialView() {
	setCurrentTabIndex(0);
}

bool PEMainWindow::canSave() {
	return m_canSave;
}

////////////////////////////////////////////////////
// Sole-renderer load path

QDomDocument & PEMainWindow::viewDocument(ViewLayer::ViewID viewID) {
	switch (viewID) {
	case ViewLayer::BreadboardView: return m_breadboardDocument;
	case ViewLayer::SchematicView:  return m_schematicDocument;
	case ViewLayer::PCBView:        return m_pcbDocument;
	case ViewLayer::IconView:       return m_iconDocument;
	default:                        return m_breadboardDocument;
	}
}

bool PEMainWindow::loadViewDocument(ViewLayer::ViewID viewID) {
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement views = root.firstChildElement("views");
	QDomElement view = views.firstChildElement(ViewLayer::viewIDXmlName(viewID));
	if (view.isNull()) return false;
	QDomElement layers = view.firstChildElement("layers");
	QString shortPath = layers.attribute("image");
	if (shortPath.isEmpty()) return false;

	QFileInfo fzpInfo(m_originalFzpPath);
	QDir fzpDir = fzpInfo.absoluteDir();
	QString canonicalViewPath = fzpDir.absoluteFilePath(shortPath);

	QFile file(canonicalViewPath);
	if (!file.open(QIODevice::ReadOnly)) {
		DebugDialog::debug(QString("Unable to open :%1").arg(canonicalViewPath));
		return false;
	}
	QDomDocument::ParseResult result = viewDocument(viewID).setContent(&file);
	file.close();
	if (!result) {
		DebugDialog::debug(QString("Unable to parse svg :%1").arg(canonicalViewPath));
		return false;
	}

	if (m_editor) m_editor->setDocument(viewDocument(viewID), ViewLayer::viewIDXmlName(viewID));
	return true;
}

void PEMainWindow::setCurrentViewId(ViewLayer::ViewID viewID) {
	m_currentViewId = viewID;
	if (m_editor) {
		m_editor->setDocument(viewDocument(viewID), ViewLayer::viewIDXmlName(viewID));
		m_editor->fitToWindow();
	}
}

void PEMainWindow::setTitle() {
	setWindowTitle(getPartTitle());
}


bool PEMainWindow::setInitialItem(PaletteItem * paletteItem) {
	ModelPart * originalModelPart = nullptr;
	if (paletteItem == nullptr) {
		originalModelPart = m_referenceModel->retrieveModelPart("generic_ic_dip_8_300mil");
	} else {
		originalModelPart = paletteItem->modelPart();
	}
	if (originalModelPart == nullptr) return false;

	m_originalFzpPath = originalModelPart->path();
	m_originalModuleID = originalModelPart->moduleID();

	QString sanitizedModuleID = FolderUtils::sanitizeForFolder(m_originalModuleID);
	if (!sanitizedModuleID.isEmpty()) {
		m_userPartsFolderSvgPath = m_userPartsFolderPath + sanitizedModuleID + "/";
	}

	QFileInfo info(originalModelPart->path());
	m_canSave = info.absoluteFilePath().contains(m_userPartsFolderPath);

	if (!loadFzp(originalModelPart->path())) return false;

	QDomElement fzpRoot = m_fzpDocument.documentElement();
	if (fzpRoot.attribute(ReferenceFileString).isEmpty()) {
		fzpRoot.setAttribute(ReferenceFileString, info.fileName());
	}

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
	QHash<QString, QString> props = originalModelPart->properties();
	Q_FOREACH (QString key, props.keys()) {
		replaceProperty(key, props.value(key), properties);
	}

	QDomElement connectors = fzpRoot.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		QString localName = originalModelPart->connectorLocalName(connector.attribute("id"));
		if (!localName.isEmpty()) {
			connector.setAttribute("name", localName);
		}
		connector = connector.nextSiblingElement("connector");
	}

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
		replaceProperty("variant", makeNewVariant(family.text()), properties);
	}

	Q_FOREACH (ViewLayer::ViewID viewID, m_viewOrder) {
		loadViewDocument(viewID);
	}
	setCurrentViewId(ViewLayer::BreadboardView);

	initConnectors(true);
	setTitle();
	return true;
}

////////////////////////////////////////////////////
// Metadata DOM handlers

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
	m_undoStack->waitPush(cpc, PropChangeDelay);
}

void PEMainWindow::metadataChanged(const QString & name, const QString & value)
{
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

	QDomElement root = m_fzpDocument.documentElement();
	QDomElement element = root.firstChildElement(name);
	QString oldValue = element.text();
	if (oldValue == value) return;

	auto * cmc = new ChangeMetadataCommand(this, name, oldValue, value, nullptr);
	cmc->setText(menuText);
	cmc->setSkipFirstRedo();
	changeMetadata(name, value, false);
	m_undoStack->waitPush(cmc, PropChangeDelay);
}

void PEMainWindow::tagsChanged(const QStringList & newTags)
{
	QDomElement tags = m_fzpDocument.documentElement().firstChildElement("tags");
	QDomElement tag = tags.isNull() ? QDomElement() : tags.firstChildElement("tag");
	QStringList oldTags;
	while (!tag.isNull()) {
		oldTags << tag.text();
		tag = tag.nextSiblingElement("tag");
	}

	auto * ctc = new ChangeTagsCommand(this, oldTags, newTags, nullptr);
	ctc->setText(tr("Change tags"));
	ctc->setSkipFirstRedo();
	changeTags(newTags, false);
	m_undoStack->waitPush(ctc, PropChangeDelay);
}

void PEMainWindow::changeTags(const QStringList & newTags, bool updateDisplay)
{
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement tags = root.firstChildElement("tags");
	if (tags.isNull()) {
		tags = m_fzpDocument.createElement("tags");
		root.appendChild(tags);
	}
	QDomElement tag = tags.firstChildElement("tag");
	while (!tag.isNull()) {
		QDomElement next = tag.nextSiblingElement("tag");
		tags.removeChild(tag);
		tag = next;
	}

	Q_FOREACH (QString newTag, newTags) {
		QDomElement t = m_fzpDocument.createElement("tag");
		tags.appendChild(t);
		TextUtils::replaceChildText(t, newTag);
	}

	if (updateDisplay) {
		m_metadataView->initMetadata(m_fzpDocument);
	}
}

void PEMainWindow::historyChanged(const QList<HistoryEntry> & newHistory)
{
	QDomElement root = m_fzpDocument.documentElement();
	QList<HistoryEntry> oldHistory = readHistory(root);

	auto * chc = new ChangeHistoryCommand(this, oldHistory, newHistory, nullptr);
	chc->setText(tr("Change revision history"));
	chc->setSkipFirstRedo();
	changeHistory(newHistory, true);
	m_undoStack->waitPush(chc, PropChangeDelay);
}

void PEMainWindow::changeHistory(const QList<HistoryEntry> & history, bool updateDisplay)
{
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
		entry.mode = h.attribute("mode", "optional");
		entry.description = h.text().trimmed();
		history.append(entry);
	}

	if (history.isEmpty()) {
		QString d = root.firstChildElement("date").text().trimmed();
		QString a = root.firstChildElement("author").text().trimmed();
		if (!d.isEmpty() || !a.isEmpty()) {
			HistoryEntry entry;
			entry.date = d;
			entry.author = a;
			entry.mode = "optional";
			history.append(entry);
		}
	}

	std::stable_sort(history.begin(), history.end(), [](const HistoryEntry & a, const HistoryEntry & b) {
		return a.parsedDate() < b.parsedDate();
	});
	return history;
}

void PEMainWindow::writeHistory(QDomElement & root, const QList<HistoryEntry> & history)
{
	QDomDocument doc = root.ownerDocument();

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

	if (!sorted.isEmpty()) {
		const HistoryEntry & newest = sorted.last();
		TextUtils::replaceElementChildText(root, "author", newest.author);
		TextUtils::replaceElementChildText(root, "date", newest.date);
	}
}

void PEMainWindow::propertiesChanged(const QHash<QString, QString> & newProperties)
{
	QHash<QString, QString> oldProperties = getOldProperties();
	auto * cpc = new ChangePropertiesCommand(this, oldProperties, newProperties, nullptr);
	cpc->setText(tr("Change properties"));
	cpc->setSkipFirstRedo();
	changeProperties(newProperties, false);
	m_undoStack->waitPush(cpc, PropChangeDelay);
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
		QDomElement p = m_fzpDocument.createElement("property");
		properties.appendChild(p);
		p.setAttribute("name", name);
		TextUtils::replaceChildText(p, newProperties.value(name));
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
		oldProperties.insert(prop.attribute("name"), prop.text());
		prop = prop.nextSiblingElement("property");
	}

	return oldProperties;
}

void PEMainWindow::replaceProperty(const QString & key, const QString & value, QDomElement & properties)
{
	QDomElement prop = properties.firstChildElement("property");
	while (!prop.isNull()) {
		if (prop.attribute("name").compare(key, Qt::CaseInsensitive) == 0) {
			TextUtils::replaceChildText(prop, value);
			return;
		}
		prop = prop.nextSiblingElement("property");
	}

	QDomElement newProp = m_fzpDocument.createElement("property");
	properties.appendChild(newProp);
	newProp.setAttribute("name", key);
	TextUtils::replaceChildText(newProp, value);
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

void PEMainWindow::tabWidget_currentChanged(int index) {
	MainWindow::tabWidget_currentChanged(index);
	if (index == 0 && m_editor) m_editor->fitToWindow();
}

////////////////////////////////////////////////////
// Connector DOM handlers

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

	if (updateConnectorsView && m_connectorsView) {
		m_connectorsView->initConnectors(&m_connectorList);
	}
	if (m_peToolView) m_peToolView->initConnectors(&m_connectorList);
	updateAssignedConnectors();
}

void PEMainWindow::updateAssignedConnectors() {
	// Assigned-connector layer highlighting on the sole renderer returns in a later graphic-edit slice.
}

void PEMainWindow::switchedConnector(int ix) {
	Q_UNUSED(ix);
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
		if (!connector.isNull()) connectors.append(connector);
	}
	removedConnectorsAux(connectors);
}

void PEMainWindow::removedConnectorsAux(QList<QDomElement> & connectors)
{
	if (connectors.isEmpty()) return;

	QString originalPath = saveFzp();
	QString message;
	Q_FOREACH (QDomElement connector, connectors) {
		m_removedConnector = connector.ownerDocument().toString();
		connector.parentNode().removeChild(connector);
		if (message.isEmpty()) {
			message = tr("Remove connector");
		}
	}
	QString newPath = saveFzp();

	auto * cfc = new ChangeFzpCommand(this, originalPath, newPath, nullptr);
	cfc->setText(message);
	m_undoStack->waitPush(cfc, PropChangeDelay);
	initConnectors(true);
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
	m_undoStack->waitPush(ccmc, PropChangeDelay);
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
	}
}

void PEMainWindow::changeConnectorElement(QDomElement & connector, ConnectorMetadata * cmd)
{
	connector.setAttribute("name", cmd->connectorName);
	connector.setAttribute("id", cmd->connectorID);
	connector.setAttribute("type", Connector::connectorNameFromType(cmd->connectorType));
	TextUtils::replaceElementChildText(connector, "description", cmd->connectorDescription);
}

void PEMainWindow::fillInMetadata(const QDomElement & connector, ConnectorMetadata & cmd) {
	cmd.connectorID = connector.attribute("id");
	cmd.connectorType = Connector::connectorTypeFromName(connector.attribute("type"));
	cmd.connectorName = connector.attribute("name");
	QDomElement description = connector.firstChildElement("description");
	cmd.connectorDescription = description.text();
}

void PEMainWindow::setConnectorSMD(bool toSMD, QDomElement & connector) {
	QDomElement views = connector.firstChildElement("views");
	QDomElement pcbView = views.firstChildElement("pcbView");
	QDomElement copper0 = TextUtils::findElementWithAttribute(pcbView, "layer", "copper0");
	QDomElement copper1 = TextUtils::findElementWithAttribute(pcbView, "layer", "copper1");
	if (copper0.isNull() && copper1.isNull()) return;

	if (toSMD) {
		if (copper0.isNull() && !copper1.isNull()) return;   // already correct
		if (copper1.isNull()) { copper0.setAttribute("layer", "copper1"); return; }
		copper0.parentNode().removeChild(copper0);            // remove the extra layer
		return;
	}

	if (!copper0.isNull() && !copper1.isNull()) return;       // already correct

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

void PEMainWindow::connectorCountChanged(int newCount) {
	QList<QDomElement> connectorList;
	QDomElement root = m_fzpDocument.documentElement();
	QDomElement connectors = root.firstChildElement("connectors");
	QDomElement connector = connectors.firstChildElement("connector");
	while (!connector.isNull()) {
		connectorList.append(connector);
		connector = connector.nextSiblingElement("connector");
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

	int id = 0;
	Q_FOREACH (QDomElement existing, connectorList) {
		QRegularExpressionMatch match;
		int ix = existing.attribute("id").indexOf(IntegerFinder, 0, &match);
		if (ix >= 0) id = qMax(id, match.captured(0).toInt());
		match = QRegularExpressionMatch();
		ix = existing.attribute("name").indexOf(IntegerFinder, 0, &match);
		if (ix >= 0) id = qMax(id, match.captured(0).toInt());
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
	QString message = (newCount - connectorList.count() == 1)
	                  ? tr("Add connector")
	                  : tr("Add %1 connectors").arg(newCount - connectorList.count());
	cfc->setText(message);
	m_undoStack->waitPush(cfc, PropChangeDelay);

	initConnectors(true);
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
		initConnectors(true);
		m_undoStack->waitPush(parentCommand, PropChangeDelay);
	}
}



////////////////////////////////////////////////////
// Save path

QString PEMainWindow::makeDirName() {
	return "fritzing_pe_" + m_guid;
}

QString PEMainWindow::saveFzp() {
	QDir dir = QDir::temp();
	QString dirName = makeDirName();
	dir.mkdir(dirName);
	dir.cd(dirName);

	QString fname = QString("temp%1.fzp").arg(m_fileIndex);
	QFile file(dir.filePath(fname));
	if (!file.open(QFile::WriteOnly | QFile::Truncate)) return QString();

	QTextStream stream(&file);
	m_fzpDocument.save(stream, 2);

	file.close();

	return file.fileName();
}

QString PEMainWindow::getSvgReferenceFile() {
	QString referenceFile = m_fzpDocument.documentElement().attribute(ReferenceFileString);
	if (!referenceFile.isEmpty()) referenceFile += "_";
	return referenceFile;
}

QString PEMainWindow::makeSvgFileName(const QDomElement & moduleIDElement) {
	return getSvgReferenceFile() + moduleIDElement.text() + ".svg";
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

QString PEMainWindow::getPartTitle() {
	QDomElement root = m_fzpDocument.documentElement();
	QString candidate = root.firstChildElement("title").text();
	if (!candidate.isEmpty()) return candidate;
	return tr("untitled part");
}

void PEMainWindow::setImageAttribute(QDomElement & fzpRoot, const QString & svgPath, ViewLayer::ViewID viewID)
{
	QDomElement views = fzpRoot.firstChildElement("views");
	QDomElement view = views.firstChildElement(ViewLayer::viewIDXmlName(viewID));
	QDomElement layers = view.firstChildElement("layers");
	QFileInfo info(svgPath);
	QDir dir = info.absoluteDir();
	setImageAttribute(layers, dir.dirName() + "/" + info.fileName());
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

bool PEMainWindow::loadFzp(const QString & path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		DebugDialog::debug(QString("Unable to open :%1").arg(path));
		return false;
	}
	QDomDocument::ParseResult parseResult = m_fzpDocument.setContent(&file);
	file.close();
	if (!parseResult) {
		QMessageBox::critical(nullptr, tr("Parts Editor", "dialog title"), tr("Unable to load fzp from %1").arg(path));
		return false;
	}

	return true;
}

void PEMainWindow::restoreFzp(const QString & fzpPath)
{
	if (!loadFzp(fzpPath)) return;

	if (m_metadataView) m_metadataView->initMetadata(m_fzpDocument);
	initConnectors(true);
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

bool PEMainWindow::saveAs(bool overWrite)
{
	// Save gate: when the part has unsaved changes, offer to record what changed in this revision
	// (and optionally bump the version) before the file is written. Cancel aborts the save.
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
				seed = history.at(todayRow);
			} else {
				seed.date = today;
				seed.author = gateRoot.firstChildElement("author").text().trimmed();
				seed.mode = "optional";
			}

			PEHistoryEntryDialog gate(seed, false, nextVersion, this);
			gate.setWindowTitle(tr("Save part"));
			gate.setHeaderText(tr("You haven't recorded what changed in this revision."));
			gate.setSaveGateButtons();
			if (gate.exec() != QDialog::Accepted) return false;

			bool domChanged = false;
			if (!gate.skipped() && gate.hasText()) {
				QList<HistoryEntry> newHistory = history;
				if (todayRow >= 0) newHistory[todayRow] = gate.entry();
				else newHistory.append(gate.entry());
				writeHistory(gateRoot, newHistory);
				domChanged = true;
			}
			if (gate.bumpVersion()) {
				QDomElement v = gateRoot.firstChildElement("version");
				if (v.isNull()) { v = m_fzpDocument.createElement("version"); gateRoot.appendChild(v); }
				TextUtils::replaceChildText(v, nextVersion);
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
	QDomElement views = fzpRoot.firstChildElement("views");

	QString targetModuleID;
	if (overWrite) {
		targetModuleID = m_originalModuleID;
	} else {
		targetModuleID = QString("%1_%2_%3").arg(m_prefix).arg(m_guid).arg(m_fileIndex);
	}
	targetModuleID = FolderUtils::sanitizeForFolder(targetModuleID);

	QString svgBasePath = m_userPartsFolderPath + targetModuleID + "/";

	if (!QDir(svgBasePath).exists()) {
		QDir().mkdir(svgBasePath);
	}

	Q_FOREACH (ViewLayer::ViewID viewID, m_viewOrder) {
		if (viewDocument(viewID).isNull()) continue;
		QString svgFileName = makeSvgFileName(fzpRoot.firstChildElement("moduleID"));
		if (!PEViewWriter::writeViewSvg(viewDocument(viewID), svgBasePath + svgFileName)) {
			QMessageBox::critical(this, tr("Unable to write file", "dialog title"),
			                      tr("Unable to write the part's SVG view to %1.").arg(svgBasePath + svgFileName));
			return false;
		}
		QDomElement view = views.firstChildElement(ViewLayer::viewIDXmlName(viewID));
		QDomElement layers = view.isNull() ? QDomElement() : view.firstChildElement("layers");
		if (!layers.isNull()) {
			layers.setAttribute("image", targetModuleID + "/" + svgFileName);
		}
	}

	QDomElement moduleIDElement = fzpRoot.firstChildElement("moduleID");
	QString oldModuleID = moduleIDElement.text();
	moduleIDElement.firstChild().toText().setData(targetModuleID);
	QString saveToPath = m_userPartsFolderPath + targetModuleID + "/";
	if (!writeXml(saveToPath + "part.fzp", m_fzpDocument.toString(), false)) {
		QMessageBox::critical(this, tr("Unable to write file", "dialog title"),
		                      tr("Unable to write the part to %1.").arg(saveToPath + "part.fzp"));
		moduleIDElement.firstChild().toText().setData(oldModuleID);
		return false;
	}
	moduleIDElement.firstChild().toText().setData(oldModuleID);

	m_canSave = true;
	m_undoStack->setClean();
	m_prefix = "";
	setTitle();

	return true;
}

void PEMainWindow::changeMetadata(const QString & name, const QString & value, bool updateDisplay)
{
	// called from command object
	QDomElement root = m_fzpDocument.documentElement();
	TextUtils::replaceElementChildText(root, name, value);
	if (updateDisplay) {
		m_metadataView->initMetadata(m_fzpDocument);
	}
}

void PEMainWindow::updateWindowMenu() {
}

void PEMainWindow::updateEditMenu() {
}

bool PEMainWindow::editsModuleID(const QString & moduleID) {
	return (m_originalModuleID.compare(moduleID) == 0);
}


