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


#endif // PECOMMANDS_H
