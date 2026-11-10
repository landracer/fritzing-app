// Standalone SVG part editor (docs/design/pe-sole-renderer.md).
//
// Deliberately shares NOTHING with src/partseditor/pemainwindow.cpp or the Fritzing part-load
// pipeline (ReferenceModel / PartBase / ModelItemManager). It opens a part the way a user does --
// from a .fzp -- by resolving the fzp's <pcbView>/<breadboardView>/<schematicView><layers image=...>
// to its svg, colourising that svg exactly like the app does, and handing the result to PESvgEditor,
// which draws it through QtSvg with one QGraphicsSvgItem per top-level element. There is no ItemBase,
// no ConnectorItem, no PE reload path in this render chain at all.
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStatusBar>
#include "pesvgeditor.h"
#include "pesvgpartloader.h"

class EditorWindow : public QMainWindow {
public:
    EditorWindow() {
        m_editor = new PESvgEditor(this);
        setCentralWidget(m_editor);
        statusBar()->showMessage(tr("Open a part (.fzp) to begin."));

        auto * mFile = menuBar()->addMenu(tr("&File"));

        auto * openPartAct = new QAction(tr("&Open Part (.fzp)..."), this);
        openPartAct->setShortcut(QKeySequence::Open);
        connect(openPartAct, &QAction::triggered, this, [this]() { openPartDialog(); });
        mFile->addAction(openPartAct);

        auto * openSvgAct = new QAction(tr("Open Raw &SVG..."), this);
        connect(openSvgAct, &QAction::triggered, this, [this]() { openSvgDialog(); });
        mFile->addAction(openSvgAct);

        mFile->addSeparator();
        auto * quitAct = new QAction(tr("&Quit"), this);
        connect(quitAct, &QAction::triggered, this, &QMainWindow::close);
        mFile->addAction(quitAct);

        auto * mView = menuBar()->addMenu(tr("&View"));
        auto * viewGroup = new QActionGroup(this);
        viewGroup->setExclusive(true);

        struct ViewDef { const char * name; PESvgPartLoader::View v; } views[] = {
            { "PCB",        PESvgPartLoader::PcbView },
            { "Breadboard", PESvgPartLoader::BreadboardView },
            { "Schematic",  PESvgPartLoader::SchematicView },
        };
        for (auto & vd : views) {
            auto * a = new QAction(tr(vd.name), this);
            a->setCheckable(true);
            a->setData(static_cast<int>(vd.v));
            viewGroup->addAction(a);
            mView->addAction(a);
            if (vd.v == PESvgPartLoader::PcbView) a->setChecked(true);
        }
        connect(viewGroup, &QActionGroup::triggered, this, [this](QAction * a) {
            m_activeView = static_cast<PESvgPartLoader::View>(a->data().toInt());
            if (!m_currentFzp.isEmpty()) loadFzp(m_currentFzp, m_activeView);
        });

        // --- Edit menu: undo / redo of element moves --------------------------------------
        auto * mEdit = menuBar()->addMenu(tr("&Edit"));
        m_undoAct = new QAction(tr("&Undo Move"), this);
        m_undoAct->setShortcut(QKeySequence::Undo);
        m_undoAct->setEnabled(false);
        connect(m_undoAct, &QAction::triggered, m_editor, &PESvgEditor::undo);
        mEdit->addAction(m_undoAct);

        m_redoAct = new QAction(tr("&Redo Move"), this);
        m_redoAct->setShortcut(QKeySequence::Redo);
        m_redoAct->setEnabled(false);
        connect(m_redoAct, &QAction::triggered, m_editor, &PESvgEditor::redo);
        mEdit->addAction(m_redoAct);

        // Selection readout + dirty/undo state drive the status bar and Edit menu.
        connect(m_editor, &PESvgEditor::selectionInfo, this, [this](const QString & text) {
            if (!text.isEmpty()) statusBar()->showMessage(tr("Selected: %1").arg(text));
        });
        connect(m_editor, &PESvgEditor::undoRedoChanged, this, [this]() {
            m_undoAct->setEnabled(m_editor->canUndo());
            m_redoAct->setEnabled(m_editor->canRedo());
        });
        connect(m_editor, &PESvgEditor::dirtyChanged, this, [this](bool dirty) {
            m_dirty = dirty;
            updateTitle();
        });

        m_partsRoot  = defaultPartsRoot();
        m_svgDirRoot = m_partsRoot + "/svg/core";
        m_fzpDir     = m_partsRoot.isEmpty() ? QDir::homePath() : m_partsRoot + "/core";
        if (m_partsRoot.isEmpty())
            statusBar()->showMessage(tr("No parts library found near %1 — use File ▸ Open to locate a .fzp")
                                         .arg(QCoreApplication::applicationDirPath()));

    }

    bool openPart(const QString & fzpPath) {
        m_currentFzp = fzpPath;
        return loadFzp(fzpPath, m_activeView);
    }

    bool openSvg(const QString & svgPath) {
        QFile f(svgPath);
        if (!f.open(QIODevice::ReadOnly)) {
            statusBar()->showMessage(tr("Cannot open: %1").arg(svgPath));
            return false;
        }
        QDomDocument doc;
        if (!doc.setContent(&f)) {
            statusBar()->showMessage(tr("Cannot parse: %1").arg(svgPath));
            return false;
        }
        f.close();
        m_currentFzp.clear();
        m_editor->setFilepath(svgPath);
        m_editor->setVisibleLayers(QStringList());
        m_editor->setDocument(doc, QFileInfo(svgPath).fileName());
        m_titleBase = tr("PEditor — %1").arg(QFileInfo(svgPath).fileName());
        m_dirty = false;
        updateTitle();
        statusBar()->showMessage(tr("Loaded raw SVG %1 (no part context, no colourisation). Click a shape to select, drag to move, Ctrl+Z to undo.").arg(QFileInfo(svgPath).fileName()));
        return true;
    }

private:
    bool loadFzp(const QString & fzpPath, PESvgPartLoader::View view) {
        const auto r = PESvgPartLoader::loadPart(fzpPath, view, m_svgDirRoot);
        if (!r.ok) {
            statusBar()->showMessage(r.error);
            return false;
        }
        m_editor->setFilepath(r.svgPath);
        m_editor->setVisibleLayers(r.visibleLayers);
        m_editor->setDocument(r.document, r.svgLabel);
        const QString title = r.title.isEmpty() ? QFileInfo(fzpPath).completeBaseName() : r.title;
        m_titleBase = tr("PEditor — %1 [%2]").arg(title, viewName(view));
        m_dirty = false;
        updateTitle();
        statusBar()->showMessage(tr("%1 — %2 (%3). Click a shape to select, drag to move, Ctrl+Z to undo.")
            .arg(title, r.svgLabel, viewName(view)));
        return true;
    }

    void updateTitle() {
        setWindowTitle(m_dirty ? tr("%1 *").arg(m_titleBase) : m_titleBase);
    }


    void openPartDialog() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open Part"), m_fzpDir, tr("Fritzing parts (*.fzp)"));
        if (!path.isEmpty()) openPart(path);
    }
    void openSvgDialog() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open SVG"), m_svgDirRoot, tr("SVG files (*.svg)"));
        if (!path.isEmpty()) openSvg(path);
    }

    static QString viewName(PESvgPartLoader::View v) {
        switch (v) {
            case PESvgPartLoader::PcbView:        return QStringLiteral("PCB");
            case PESvgPartLoader::BreadboardView: return QStringLiteral("Breadboard");
            case PESvgPartLoader::SchematicView:  return QStringLiteral("Schematic");
        }
        return QStringLiteral("PCB");
    }

    // Locate the bundled parts tree by walking up from both the executable and the current
    // directory. A directory is a parts root iff it contains BOTH core/ (the .fzp files) and
    // svg/core/ (the svgs the fzps reference). Returns empty only if nothing matches, so callers
    // get a truthful "not found" instead of a bogus path that produces a confusing load error.
    static bool isPartsRoot(const QString & d) {
        return QFileInfo::exists(d + "/core") && QFileInfo::exists(d + "/svg/core");
    }
    static QString searchUp(const QString & start) {
        QDir d(start);
        for (int i = 0; i < 8 && d.exists(); ++i) {
            const QString cand = d.absolutePath();
            if (isPartsRoot(cand + "/fritzing-parts")) return cand + "/fritzing-parts";
            if (isPartsRoot(cand + "/parts")) return cand + "/parts";
            if (isPartsRoot(cand)) return cand;   // cwd already IS a parts checkout
            if (!d.cdUp()) break;
        }
        return QString();
    }
    static QString defaultPartsRoot() {
        const QString appDir = QCoreApplication::applicationDirPath();
        // searchUp first checks each ancestor + "/fritzing-parts", so an ancestor of the repo root
        // is fine; checking isPartsRoot(ancestor) itself catches "cwd is the parts dir".
        QString root = searchUp(appDir);
        if (root.isEmpty()) root = searchUp(QDir::currentPath());
        return root; // may be empty; loadFzp/openPart report it via the loader error
    }

    PESvgEditor * m_editor = nullptr;
    QAction * m_undoAct = nullptr;
    QAction * m_redoAct = nullptr;
    QString m_partsRoot;
    QString m_svgDirRoot;
    QString m_fzpDir;
    QString m_currentFzp;
    QString m_titleBase;
    bool m_dirty = false;
    PESvgPartLoader::View m_activeView = PESvgPartLoader::PcbView;
};

int main(int argc, char ** argv) {
    QApplication app(argc, argv);
    EditorWindow win;
    win.resize(1100, 800);
    win.show();
    if (argc >= 2) {
        const QString a = QString::fromUtf8(argv[1]);
        if (a.endsWith(".fzp", Qt::CaseInsensitive)) win.openPart(a);
        else win.openSvg(a);
    }
    return app.exec();
}

