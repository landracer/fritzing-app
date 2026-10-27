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

#ifndef WELCOMEVIEW_H
#define WELCOMEVIEW_H

#include <QFrame>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>

/**
 * @brief The landing page: recent sketches (left) + an interactive preview (right).
 *
 * The left column lists recent sketches (from the "recentFileList" QSettings key).
 * Selecting one loads its stored production metadata (thumbnail PNG + gerber dir +
 * up-to-date/stale status) into the right column WITHOUT opening a full editor
 * window - unlike the gerber viewer, which pops its own window. From here the user
 * can open the sketch in the editor or open its existing gerbers in the standalone
 * GerberPreviewDialog (which is why we store the gerber dir: no regeneration).
 *
 * The status badge is the correctness guard: gerbers that no longer match the board
 * are flagged "out of date - do not use for production" so a user never fabricates
 * stale artifacts.
 */
class WelcomeView : public QFrame
{
	Q_OBJECT

public:
	explicit WelcomeView(QWidget * parent = nullptr);

	void updateRecent();
	// Render/refresh the preview panel for the given sketch. Public so MainWindow
	// can refresh the currently-visible preview after regenerating a thumbnail.
	void showPreviewFor(const QString & fzzPath);
	// Tell the welcome view which sketch is actually loaded in this window's editor.
	// Only that sketch can be re-rendered ("Refresh preview") without opening it, so the
	// refresh button is enabled only when the selected item matches this path.
	void setLoadedFile(const QString & fzzPath);

	Q_SIGNALS:
	void newSketch();
	void openSketch();
	void recentSketch(const QString & filename, const QString & actionText);
	void openInGerberViewer(const QString & gerberDir, const QString & title);
	void refreshRequested(const QString & fzzPath);

protected Q_SLOTS:
	void onRecentSelected();
	void onOpenClicked();
	void onOpenGerberClicked();
	void onRefreshClicked();

protected:
	void initLayout();

	QListWidget * m_recentListWidget = nullptr;
	QLabel      * m_previewImage     = nullptr;   // thumbnail on the right
	QLabel      * m_previewName      = nullptr;   // selected sketch name
	QLabel      * m_previewStatus    = nullptr;   // gerber status badge
	QPushButton * m_openButton       = nullptr;
	QPushButton * m_openGerberButton = nullptr;
	QPushButton * m_refreshButton    = nullptr;   // regenerate the preview image
	QString       m_selectedPath;                 // currently-previewed sketch
	QString       m_loadedPath;                   // sketch actually loaded in this window's editor
};

#endif
