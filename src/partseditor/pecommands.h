/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2019 Fritzing

Fritzing is free software: you can redistribute it and/or modify\
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

#ifndef PECOMMANDS_H
#define PECOMMANDS_H

#include "../commands.h"
#include "peutils.h"
#include "../model/modelpartshared.h"   // HistoryEntry


/////////////////////////////////////////////

class PEBaseCommand : public BaseCommand
{

public:
	PEBaseCommand(class PEMainWindow *, QUndoCommand* parent);
	~PEBaseCommand();

	QString getParamString() const;

protected:
	class PEMainWindow * m_peMainWindow;
};

/////////////////////////////////////////////

class ChangeMetadataCommand : public PEBaseCommand
{
public:
	ChangeMetadataCommand(class PEMainWindow *, const QString & name, const QString  & oldValue, const QString & newValue, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QString m_name;
	QString m_oldValue;
	QString m_newValue;
};

/////////////////////////////////////////////

class ChangeConnectorMetadataCommand : public PEBaseCommand
{
public:
	ChangeConnectorMetadataCommand(class PEMainWindow *, ConnectorMetadata * oldcm, ConnectorMetadata * newcm, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	ConnectorMetadata m_oldcm;
	ConnectorMetadata m_newcm;
};

/////////////////////////////////////////////

class ChangeFzpCommand : public PEBaseCommand
{
public:
	ChangeFzpCommand(class PEMainWindow *, const QString & oldFzpFile, const QString & newFzpFile, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QString m_oldFzpFile;
	QString m_newFzpFile;
};

/////////////////////////////////////////////

class ChangeTagsCommand : public PEBaseCommand
{
public:
	ChangeTagsCommand(class PEMainWindow *, const QStringList & oldTabs, const QStringList  & newTags, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QStringList m_old;
	QStringList m_new;
};

/////////////////////////////////////////////

class ChangeHistoryCommand : public PEBaseCommand
{
public:
	ChangeHistoryCommand(class PEMainWindow *, const QList<HistoryEntry> & oldHistory, const QList<HistoryEntry>  & newHistory, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QList<HistoryEntry> m_old;
	QList<HistoryEntry> m_new;
};

/////////////////////////////////////////////

class ChangePropertiesCommand : public PEBaseCommand
{
public:
	ChangePropertiesCommand(class PEMainWindow *, const QHash<QString, QString> & oldProps, const QHash<QString, QString> & newProps, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QHash<QString, QString> m_old;
	QHash<QString, QString> m_new;
};

/////////////////////////////////////////////

class ChangeSvgCommand : public PEBaseCommand
{
public:
	ChangeSvgCommand(class PEMainWindow *, SketchWidget *, const QString  & oldFilename, const QString & newFilename, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QString m_oldFilename;
	QString m_newFilename;
};

/////////////////////////////////////////////

class RelocateConnectorSvgCommand : public PEBaseCommand
{
public:
	RelocateConnectorSvgCommand(class PEMainWindow *, SketchWidget *, const QString  & id, const QString & terminalID, const QString  & oldGorn, const QString & oldGornTerminal, const QString  & newGorn, const QString & newGornTerminal, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QString m_id;
	QString m_terminalID;
	QString m_oldGorn;
	QString m_oldGornTerminal;
	QString m_newGorn;
	QString m_newGornTerminal;
};

/////////////////////////////////////////////

class MoveTerminalPointCommand : public PEBaseCommand
{
public:
	MoveTerminalPointCommand(class PEMainWindow *, SketchWidget *, const QString  & id, QSizeF size, QPointF oldLocation, QPointF newLocation, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QString m_id;
	QSizeF m_size;
	QPointF m_oldLocation;
	QPointF m_newLocation;
};

/////////////////////////////////////////////

// P3: node-edit on a <path>. Carries the svg element id, the segment index, and the
// before/after anchor in document user units; redo/undo call PEMainWindow::editPathAnchor.
class EditPathAnchorCommand : public PEBaseCommand
{
public:
	EditPathAnchorCommand(class PEMainWindow *, SketchWidget *, const QString & svgID, int segmentIndex, QPointF before, QPointF after, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	SketchWidget * m_sketchWidget;
	QString m_svgID;
	int m_segmentIndex;
	QPointF m_before;
	QPointF m_after;
};

/////////////////////////////////////////////

// P3: add (redo) / remove (undo) a primitive element to a view's svg. Carries the
// serialized element fragment and the parent element's svg id (document order append).
class AddPrimitiveCommand : public PEBaseCommand
{
public:
	AddPrimitiveCommand(class PEMainWindow *, SketchWidget *, const QString & parentSvgID, const QString & elementXml, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	SketchWidget * m_sketchWidget;
	QString m_parentSvgID;
	QString m_elementXml;
};

/////////////////////////////////////////////

// P3: remove (redo) / restore (undo) the primitive element identified by svg id.
class DeletePrimitiveCommand : public PEBaseCommand
{
public:
	DeletePrimitiveCommand(class PEMainWindow *, SketchWidget *, const QString & svgID, const QString & elementXml, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	SketchWidget * m_sketchWidget;
	QString m_svgID;
	QString m_elementXml;
};

/////////////////////////////////////////////

class RemoveBusConnectorCommand : public PEBaseCommand
{
public:
	RemoveBusConnectorCommand(class PEMainWindow *, const QString  & busID, const QString & connectorID, bool inverted, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QString m_busID;
	QString m_connectorID;
	bool m_inverted;
};

/////////////////////////////////////////////

class ChangeSMDCommand : public PEBaseCommand
{
public:
	ChangeSMDCommand(class PEMainWindow *, const QString  & before, const QString & after, const QString  & oldFilename, const QString & newFilename, QUndoCommand *parent);
	void undo();
	void redo();

protected:
	QString getParamString() const;

protected:
	QString m_before;
	QString m_after;
	QString m_oldFilename;
	QString m_newFilename;
};

/////////////////////////////////////////////


#endif // PECOMMANDS_H
