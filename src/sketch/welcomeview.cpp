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

#include "welcomeview.h"
#include "sketchmeta.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSettings>
#include <QSpacerItem>
#include <QSizePolicy>

WelcomeView::WelcomeView(QWidget * parent) : QFrame(parent)
{
	this->setObjectName("welcomeView");
	initLayout();
	connect(m_recentListWidget, &QListWidget::itemSelectionChanged,
	        this, &WelcomeView::onRecentSelected);
	updateRecent();
}

void WelcomeView::initLayout()
{
	auto * mainLayout = new QGridLayout();
	mainLayout->setContentsMargins(12, 12, 12, 12);
	mainLayout->setSpacing(12);
	// Give the preview (right) column more horizontal room than the recents list.
	mainLayout->setColumnStretch(0, 1);
	mainLayout->setColumnStretch(1, 2);

	// ---- Left: recent sketches ------------------------------------------
	auto * leftFrame = new QFrame();
	auto * leftLayout = new QVBoxLayout();
	leftLayout->setContentsMargins(0, 0, 0, 0);

	auto * recentTitle = new QLabel(tr("Recent Sketches"));
	recentTitle->setObjectName("recentTitle");
	QFont tf = recentTitle->font(); tf.setPointSize(13); tf.setBold(true);
	recentTitle->setFont(tf);
	leftLayout->addWidget(recentTitle);

	m_recentListWidget = new QListWidget();
	m_recentListWidget->setObjectName("recentList");
	m_recentListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	leftLayout->addWidget(m_recentListWidget);

	auto * newBtn = new QPushButton(tr("New Sketch"));
	connect(newBtn, &QPushButton::clicked, this, [this]() { Q_EMIT newSketch(); });
	leftLayout->addWidget(newBtn);

	leftFrame->setLayout(leftLayout);
	mainLayout->addWidget(leftFrame, 0, 0, 1, 1);

	// ---- Right: interactive preview -------------------------------------
	auto * rightFrame = new QFrame();
	auto * rightLayout = new QVBoxLayout();
	rightLayout->setContentsMargins(0, 0, 0, 0);

	auto * previewTitle = new QLabel(tr("Preview"));
	previewTitle->setObjectName("previewTitle");
	previewTitle->setFont(tf);
	rightLayout->addWidget(previewTitle);

	m_previewName = new QLabel(tr("Select a sketch to preview"));
	m_previewName->setWordWrap(true);
	QFont nf = m_previewName->font(); nf.setPointSize(11); nf.setBold(true);
	m_previewName->setFont(nf);
	rightLayout->addWidget(m_previewName);

	m_previewStatus = new QLabel(QString());
	m_previewStatus->setWordWrap(true);
	m_previewStatus->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	rightLayout->addWidget(m_previewStatus);

	m_previewImage = new QLabel();
	m_previewImage->setAlignment(Qt::AlignCenter);
	m_previewImage->setMinimumSize(520, 420);
	m_previewImage->setStyleSheet("border: 1px solid #ccc; background:#fafafa;");
	m_previewImage->setText(tr("(no preview)"));
	rightLayout->addWidget(m_previewImage, 1);

	auto * btnRow = new QHBoxLayout();
	m_openButton = new QPushButton(tr("Open"));
	m_openButton->setEnabled(false);
	connect(m_openButton, &QPushButton::clicked, this, &WelcomeView::onOpenClicked);
	btnRow->addWidget(m_openButton);

	m_openGerberButton = new QPushButton(tr("Open in Gerber Viewer"));
	m_openGerberButton->setEnabled(false);
	connect(m_openGerberButton, &QPushButton::clicked, this, &WelcomeView::onOpenGerberClicked);
	btnRow->addWidget(m_openGerberButton);

	m_refreshButton = new QPushButton(tr("Refresh preview"));
	m_refreshButton->setEnabled(false);
	connect(m_refreshButton, &QPushButton::clicked, this, &WelcomeView::onRefreshClicked);
	btnRow->addWidget(m_refreshButton);

	// Housekeeping: drop the selected sketch from the recent list (and its preview). Purely a
	// recents/settings cleanup - the saved .fzz file itself is never touched.
	m_removeButton = new QPushButton(tr("Remove from recents"));
	m_removeButton->setEnabled(false);
	m_removeButton->setStatusTip(tr("Remove the selected sketch from the recent list (does not delete the file)"));
	connect(m_removeButton, &QPushButton::clicked, this, &WelcomeView::onRemoveFromRecentClicked);
	btnRow->addWidget(m_removeButton);

	// Clear the entire recent list in one click (silently; no confirm dialog).
	m_purgeButton = new QPushButton(tr("Purge all recents"));
	m_purgeButton->setStatusTip(tr("Clear the entire recent-sketches list (does not delete any files)"));
	connect(m_purgeButton, &QPushButton::clicked, this, &WelcomeView::onPurgeRecentsClicked);
	btnRow->addWidget(m_purgeButton);
	rightLayout->addLayout(btnRow);

	rightFrame->setLayout(rightLayout);
	mainLayout->addWidget(rightFrame, 0, 1, 1, 2);

	setLayout(mainLayout);
}

void WelcomeView::updateRecent() {
	if (!m_recentListWidget) return;

	QSettings settings;
	auto files = settings.value("recentFileList").toStringList();
	m_recentListWidget->clear();
	for (const QString & f : files) {
		if (!QFileInfo(f).exists()) continue;
		auto * item = new QListWidgetItem(QFileInfo(f).fileName(), m_recentListWidget);
		item->setData(Qt::UserRole, f);
	}
	if (m_recentListWidget->count() == 0) {
		new QListWidgetItem(tr("No recent sketches"), m_recentListWidget);
	}
	// If the sketch shown in the preview is no longer in the list (it was removed, or the list was
	// purged), drop its stale preview - unless it's the sketch loaded in this window's editor, whose
	// content is still live and should stay previewable.
	if (!m_selectedPath.isEmpty()) {
		const QString want = QFileInfo(m_selectedPath).absoluteFilePath();
		const bool isLoaded = !m_loadedPath.isEmpty()
		                   && want == QFileInfo(m_loadedPath).absoluteFilePath();
		bool stillListed = false;
		for (int i = 0; i < m_recentListWidget->count(); ++i) {
			QListWidgetItem * it = m_recentListWidget->item(i);
			if (it && QFileInfo(it->data(Qt::UserRole).toString()).absoluteFilePath() == want) {
				stillListed = true;
				break;
			}
		}
		if (!isLoaded && !stillListed) clearPreview();
	}
}

void WelcomeView::onRecentSelected() {
	auto items = m_recentListWidget->selectedItems();
	if (items.isEmpty()) return;
	const QString path = items.first()->data(Qt::UserRole).toString();
	if (path.isEmpty()) return;
	showPreviewFor(path);
}

void WelcomeView::setLoadedFile(const QString & fzzPath) {
	m_loadedPath = fzzPath;
	if (fzzPath.isEmpty()) return;

	// A board can only be re-rendered ("Refresh preview") in the window that actually has
	// it loaded. So when this window loads a sketch, automatically select its item in the
	// recent list and show its preview, which also enables the refresh button for it. This
	// is what makes refresh work without the user having to (or being able to) click it.
	if (m_recentListWidget) {
		const QString want = QFileInfo(fzzPath).absoluteFilePath();
		for (int i = 0; i < m_recentListWidget->count(); ++i) {
			QListWidgetItem * it = m_recentListWidget->item(i);
			if (it && QFileInfo(it->data(Qt::UserRole).toString()).absoluteFilePath() == want) {
				m_recentListWidget->setCurrentItem(it);   // triggers itemSelectionChanged -> showPreviewFor
				return;
			}
		}
	}
	// Not on the (visible) recent list: still show it and enable refresh directly.
	showPreviewFor(fzzPath);
}

void WelcomeView::showPreviewFor(const QString & fzzPath) {
	m_selectedPath = fzzPath;

	SketchMeta meta = SketchMeta::load(fzzPath);

	if (m_previewName) m_previewName->setText(QFileInfo(fzzPath).fileName());

	// --- board preview (gerber-independent) --------------------------------
	QPixmap thumb = meta.thumbnail();
	if (m_previewImage) {
		if (!thumb.isNull()) {
			// thumb carries its render DPR (set at capture time); QPixmap::scaled works in
			// logical coordinates and preserves that DPR, so it stays crisp on HiDPI here.
			m_previewImage->setPixmap(thumb.scaled(m_previewImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
		} else {
			m_previewImage->clear();
			m_previewImage->setText(tr("(no preview)"));
		}
	}
	// "Refresh preview" re-renders the board live, which is only possible for the sketch
	// actually loaded in this window's editor. For any other recent item it stays disabled
	// (open it first, and its preview is generated on open), so a refresh can never blank a
	// good preview by rendering an unrelated/hidden board.
	if (m_refreshButton) {
		const bool isLoaded = !m_loadedPath.isEmpty()
		                      && QFileInfo(fzzPath).absoluteFilePath() == QFileInfo(m_loadedPath).absoluteFilePath();
		m_refreshButton->setEnabled(isLoaded);
	}

	// --- gerber production status (separate concern) -----------------------
	if (!meta.isValid()) {
		if (m_previewStatus) m_previewStatus->setText(tr("No gerbers generated for this sketch."));
		if (m_openGerberButton) m_openGerberButton->setEnabled(false);
	} else {
		const bool upToDate = SketchMeta::isUpToDate(fzzPath);
		if (m_previewStatus) {
			if (!upToDate)
				m_previewStatus->setText(tr("<span style='color:#b00020;'><b>Gerbers out of date &mdash; do not use for production.</b></span><br>The sketch changed since these gerbers were generated. Re-export before fabricating."));
			else
				m_previewStatus->setText(tr("<span style='color:#00802b;'><b>Gerbers up to date &mdash; safe for production.</b></span>"));
		}
		if (m_openGerberButton) m_openGerberButton->setEnabled(true);
	}
	if (m_openButton) m_openButton->setEnabled(true);
	// A real sketch is selected, so it can now be removed from the recent list.
	if (m_removeButton) m_removeButton->setEnabled(true);
}

void WelcomeView::clearPreview() {
	m_selectedPath.clear();
	if (m_previewImage) {
		m_previewImage->clear();
		m_previewImage->setText(tr("(no preview)"));
	}
	if (m_previewName) m_previewName->setText(tr("Select a sketch to preview"));
	if (m_previewStatus) m_previewStatus->clear();
	if (m_openButton) m_openButton->setEnabled(false);
	if (m_openGerberButton) m_openGerberButton->setEnabled(false);
	if (m_refreshButton) m_refreshButton->setEnabled(false);
	if (m_removeButton) m_removeButton->setEnabled(false);
}

void WelcomeView::onOpenClicked() {
	if (!m_selectedPath.isEmpty())
		Q_EMIT recentSketch(m_selectedPath, m_selectedPath);
}

void WelcomeView::onOpenGerberClicked() {
	if (m_selectedPath.isEmpty()) return;
	SketchMeta meta = SketchMeta::load(m_selectedPath);
	if (!meta.isValid()) return;
	Q_EMIT openInGerberViewer(meta.gerberDir(), QFileInfo(m_selectedPath).completeBaseName());
}

void WelcomeView::onRefreshClicked() {
	if (m_selectedPath.isEmpty()) return;
	// Ask MainWindow to regenerate the board preview for the currently-loaded board.
	// It re-shows the preview (via showPreviewFor) once the deferred render completes.
	Q_EMIT refreshRequested(m_selectedPath);
}

void WelcomeView::onRemoveFromRecentClicked() {
	if (m_selectedPath.isEmpty()) return;
	Q_EMIT removeFromRecent(m_selectedPath);
}

void WelcomeView::onPurgeRecentsClicked() {
	Q_EMIT purgeRecents();
}
