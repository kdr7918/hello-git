#ifndef PARSER_H
#define PARSER_H

#include "data_types.h"
#include "fast_text_reader.h"

#include <QString>

// Replace this interface when the real record format is known.  Thread
// workers and models do not depend on a concrete parser implementation.
class IRecordParser
{
public:
    virtual ~IRecordParser() {}

    virtual bool parseLine(qint64 sectionId,
                           qint64 lineNumber,
                           quint64 sourceOffset,
                           ByteView line,
                           DataRow *row) const = 0;
};

// Minimal CSV/TSV parser used by the example application.
class SimpleRecordParser : public IRecordParser
{
public:
    bool parseLine(qint64 sectionId,
                   qint64 lineNumber,
                   quint64 sourceOffset,
                   ByteView line,
                   DataRow *row) const override;
};

bool parseHeading(ByteView line, int *level, QString *title);

#endif // PARSER_H
