#include "parser.h"

#include <QStringList>

bool SimpleRecordParser::parseLine(qint64 sectionId,
                                   qint64 lineNumber,
                                   quint64 sourceOffset,
                                   ByteView line,
                                   DataRow *row) const
{
    const ByteView trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#'))
        return false;

    int separator = trimmed.find(',');
    if (separator < 0)
        separator = trimmed.find('\t');

    row->sectionId = sectionId;
    row->lineNumber = lineNumber;
    row->sourceOffset = sourceOffset;
    row->raw = trimmed.toUtf8();

    if (separator < 0) {
        row->key = row->raw;
        row->value.clear();
    } else {
        row->key = trimmed.left(separator).trimmed().toUtf8();
        row->value = trimmed.mid(separator + 1).trimmed().toUtf8();
    }
    return true;
}

bool parseHeading(ByteView line, int *level, QString *title)
{
    const ByteView trimmed = line.trimmed();
    int count = 0;
    while (count < trimmed.size() && trimmed.at(count) == '#')
        ++count;

    if (count == 0 || count >= trimmed.size())
        return false;
    if (trimmed.at(count) != ' ' && trimmed.at(count) != '\t')
        return false;

    const QString text = trimmed.mid(count).toUtf8().trimmed();
    if (text.isEmpty())
        return false;

    *level = count;
    *title = text;
    return true;
}
