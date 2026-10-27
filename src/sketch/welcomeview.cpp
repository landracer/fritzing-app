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
#include "debugdialog.h"

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
	mainLayout->addWidget(leftFrame, 0, 0);

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
	m_previewImage->setMinimumSize(320, 240);
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
	rightLayout->addLayout(btnRow);

	rightFrame->setLayout(rightLayout);
	mainLayout->addWidget(rightFrame, 0, 1);

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
}

void WelcomeView::onRecentSelected() {
	auto items = m_recentListWidget->selectedItems();
	if (items.isEmpty()) return;
	const QString path = items.first()->data(Qt::UserRole).toString();
	if (path.isEmpty()) return;
	showPreviewFor(path);
}

void WelcomeView::showPreviewFor(const QString & fzzPath) {
	m_selectedPath = fzzPath;

	SketchMeta meta = SketchMeta::load(fzzPath);

	if (m_previewName) m_previewName->setText(QFileInfo(fzzPath).fileName());

	if (!meta.isValid()) {
		if (m_previewImage) { m_previewImage->clear(); m_previewImage->setText(tr("(no gerber preview)")); }
		if (m_previewStatus) m_previewStatus->setText(tr("No gerbers generated for this sketch yet."));
		if (m_openGerberButton) m_openGerberButton->setEnabled(false);
	} else {
		QPixmap thumb = meta.thumbnail();
		if (m_previewImage) {
			if (!thumb.isNull())
				m_previewImage->setPixmap(thumb.scaled(m_previewImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
			else {
				m_previewImage->clear();
				m_previewImage->setText(tr("(no thumbnail)"));
			}
		}
		if (m_openGerberButton) m_openGerberButton->setEnabled(true);
		const bool upToDate = SketchMeta::isUpToDate(fzzPath);
		if (m_previewStatus) {
			if (!upToDate)
				m_previewStatus->setText(tr("<span style='color:#b00020;'><b>Out of date &mdash; do not use for production.</b></span><br>The sketch changed since these gerbers were generated. Re-export before fabricating."));
			else
				m_previewStatus->setText(tr("<span style='color:#00802b;'><b>Up to date &mdash; safe for production.</b></span>"));
		}
	}
	if (m_openButton) m_openButton->setEnabled(true);
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
