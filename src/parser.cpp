#include "parser.h"

#include <QStringList>

bool SimpleRecordParser::parseLine(qint64 sectionId,
                                   qint64 lineNumber,
                                   quint64 sourceOffset,
                                   const QByteArray &line,
                                   DataRow *row) const
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#'))
        return false;

    const QString raw = QString::fromUtf8(trimmed);
    int separator = raw.indexOf(QLatin1Char(','));
    if (separator < 0)
        separator = raw.indexOf(QLatin1Char('\t'));

    row->sectionId = sectionId;
    row->lineNumber = lineNumber;
    row->sourceOffset = sourceOffset;
    row->raw = raw;

    if (separator < 0) {
        row->key = raw;
        row->value.clear();
    } else {
        row->key = raw.left(separator).trimmed();
        row->value = raw.mid(separator + 1).trimmed();
    }
    return true;
}

bool parseHeading(const QByteArray &line, int *level, QString *title)
{
    const QByteArray trimmed = line.trimmed();
    int count = 0;
    while (count < trimmed.size() && trimmed.at(count) == '#')
        ++count;

    if (count == 0 || count >= trimmed.size())
        return false;
    if (trimmed.at(count) != ' ' && trimmed.at(count) != '\t')
        return false;

    const QString text = QString::fromUtf8(trimmed.mid(count)).trimmed();
    if (text.isEmpty())
        return false;

    *level = count;
    *title = text;
    return true;
}
