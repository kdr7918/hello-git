#include "fast_text_reader.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cstring>

namespace {

struct Result
{
    Result() : lines(0), checksum(0), nanoseconds(0) {}
    qint64 lines;
    quint64 checksum;
    qint64 nanoseconds;
};

Result referenceMemchr(const QString &path)
{
    Result result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        return result;

    QByteArray buffer(1024 * 1024, '\0');
    bool hasData = false;
    char last = '\n';
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        const qint64 count = file.read(buffer.data(), buffer.size());
        if (count <= 0)
            break;
        hasData = true;
        last = buffer.at(static_cast<int>(count - 1));
        const char *position = buffer.constData();
        const char *end = position + count;
        while (position < end) {
            const void *found = std::memchr(position, '\n',
                                            static_cast<size_t>(end - position));
            if (!found)
                break;
            position = static_cast<const char *>(found) + 1;
            ++result.lines;
        }
        result.checksum += static_cast<quint64>(count);
    }
    if (hasData && last != '\n')
        ++result.lines;
    result.nanoseconds = timer.nsecsElapsed();
    return result;
}

Result fastReader(const QString &path)
{
    Result result;
    FastTextReader reader;
    if (!reader.open(path))
        return result;

    QElapsedTimer timer;
    timer.start();
    LineWindow window;
    while (reader.nextWindow(&window)) {
        ++result.lines;
        result.checksum += static_cast<quint64>(window.current.sourceSpan().length());
    }
    result.nanoseconds = timer.nsecsElapsed();
    return result;
}

double mebibytesPerSecond(qint64 bytes, qint64 nanoseconds)
{
    if (nanoseconds <= 0)
        return 0.0;
    return (bytes / (1024.0 * 1024.0)) / (nanoseconds / 1000000000.0);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    if (application.arguments().size() != 2) {
        output << "usage: fast-parser-benchmark <large-text-file>\n";
        return 2;
    }

    const QString path = application.arguments().at(1);
    const qint64 bytes = QFileInfo(path).size();

    // Warm the filesystem cache, then time both implementations.
    referenceMemchr(path);
    fastReader(path);
    const Result reference = referenceMemchr(path);
    const Result fast = fastReader(path);

    output << "bytes: " << bytes << '\n'
           << "reference read+memchr: "
           << mebibytesPerSecond(bytes, reference.nanoseconds) << " MiB/s, "
           << reference.lines << " lines\n"
           << "FastTextReader:       "
           << mebibytesPerSecond(bytes, fast.nanoseconds) << " MiB/s, "
           << fast.lines << " lines\n";

    if (reference.lines != fast.lines || reference.checksum != fast.checksum) {
        output << "ERROR: result mismatch\n";
        return 1;
    }
    return 0;
}
