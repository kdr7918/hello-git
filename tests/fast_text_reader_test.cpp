#include "fast_text_reader.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <cmath>
#include <limits>

namespace {

bool require(bool condition, const char *message)
{
    if (!condition)
        qCritical() << "FAILED:" << message;
    return condition;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    Q_UNUSED(application)

    QTemporaryFile file;
    if (!require(file.open(), "temporary file open"))
        return 1;

    const QByteArray longField(6000, 'x');
    const QByteArray contents =
        QByteArray("alpha,42,3.5\n") +
        QByteArray("beta,-7,hello\r\n") +
        QByteArray("long,") + longField + QByteArray(",9\n") +
        QByteArray("tail,99,1e2");
    if (!require(file.write(contents) == contents.size(), "temporary file write"))
        return 1;
    file.flush();

    // A 4 KiB buffer forces both compaction and growth for the long line.
    FastTextReader reader(4096);
    if (!require(reader.open(file.fileName()), "reader open"))
        return 1;

    LineWindow window;
    if (!require(reader.nextWindow(&window), "first line"))
        return 1;
    if (!require(window.current.bytes.toByteArray() == "alpha,42,3.5",
                 "current line contents") ||
        !require(window.hasNext && window.next.bytes.toByteArray() == "beta,-7,hello",
                 "look-ahead line contents")) {
        return 1;
    }

    const SeekPoint firstPoint = window.current.seekPoint();
    const SeekPoint secondPoint = window.next.seekPoint();
    if (!require(window.current.findAbsolute(',') == firstPoint.offset + 5,
                 "absolute character position")) {
        return 1;
    }

    FieldCursor fields = window.current.fields();
    ByteView name;
    qint64 integer = 0;
    double decimal = 0.0;
    if (!require(fields.readString(&name) && name.toByteArray() == "alpha",
                 "string field") ||
        !require(fields.readInt64(&integer) && integer == 42,
                 "integer field") ||
        !require(fields.readDouble(&decimal) && std::fabs(decimal - 3.5) < 0.00001,
                 "double field") ||
        !require(fields.lastSourceSpan().begin == firstPoint.offset + 9,
                 "field source span")) {
        return 1;
    }

    qint64 signedLimit = 0;
    quint64 unsignedLimit = 0;
    double exponentValue = 0.0;
    if (!require(ByteView("-9223372036854775808", 20).toInt64(&signedLimit) &&
                     signedLimit == std::numeric_limits<qint64>::min(),
                 "signed integer limit") ||
        !require(ByteView("18446744073709551615", 20).toUInt64(&unsignedLimit) &&
                     unsignedLimit == std::numeric_limits<quint64>::max(),
                 "unsigned integer limit") ||
        !require(ByteView("1.25e2", 6).toDouble(&exponentValue) &&
                     std::fabs(exponentValue - 125.0) < 0.00001,
                 "exponent number")) {
        return 1;
    }

    QVector<ByteView> split;
    window.current.bytes.split(',', &split);
    if (!require(split.size() == 3 && split.at(1).toByteArray() == "42",
                 "zero-copy split")) {
        return 1;
    }

    RegexHit hit;
    if (!require(window.current.findRegex(QRegularExpression("[0-9]+\\.[0-9]+"), &hit) &&
                     hit.captured == "3.5" && hit.sourceOffset == firstPoint.offset + 9,
                 "regular expression search")) {
        return 1;
    }

    if (!require(reader.nextWindow(&window), "second line") ||
        !require(window.current.seekPoint().offset == secondPoint.offset,
                 "cached next line becomes current") ||
        !require(window.current.bytes.toByteArray() == "beta,-7,hello",
                 "CRLF removal") ||
        !require(window.hasNext && window.next.bytes.size() == 6007,
                 "long-line look-ahead")) {
        return 1;
    }

    if (!require(reader.seek(secondPoint), "seek point restore") ||
        !require(reader.nextWindow(&window) &&
                     window.current.bytes.toByteArray() == "beta,-7,hello",
                 "seek result")) {
        return 1;
    }

    const qint64 betaEnd = window.current.nextOffset;
    if (!require(reader.setRange(secondPoint.offset, betaEnd, secondPoint.lineNumber),
                 "bounded range") ||
        !require(reader.nextWindow(&window) && !window.hasNext,
                 "range excludes following line")) {
        return 1;
    }

    qInfo() << "fast_text_reader_test: PASS";
    return 0;
}
