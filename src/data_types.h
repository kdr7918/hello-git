#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <QHash>
#include <QMetaType>
#include <QSharedPointer>
#include <QString>
#include <QVector>

struct TocEntry
{
    TocEntry()
        : id(-1), parentId(-1), level(1), byteOffset(0), byteLength(0),
          estimatedRows(0)
    {
    }

    qint64 id;
    qint64 parentId;
    int level;
    QString title;
    qint64 byteOffset;
    qint64 byteLength;
    qint64 estimatedRows;
};

struct DataRow
{
    DataRow()
        : sectionId(-1), lineNumber(0), sourceOffset(0)
    {
    }

    // sourceOffset is unique inside a file and is used to restore selections.
    qint64 sectionId;
    qint64 lineNumber;
    quint64 sourceOffset;
    QString key;
    QString value;
    QString raw;
};

struct ParsedDocument
{
    QHash<qint64, QVector<DataRow> > rowsBySection;
};

typedef QSharedPointer<ParsedDocument> ParsedDocumentPtr;

Q_DECLARE_METATYPE(TocEntry)
Q_DECLARE_METATYPE(QVector<TocEntry>)
Q_DECLARE_METATYPE(DataRow)
Q_DECLARE_METATYPE(QVector<DataRow>)
Q_DECLARE_METATYPE(ParsedDocumentPtr)

#endif // DATA_TYPES_H
