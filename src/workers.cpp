#include "workers.h"

#include "fast_text_reader.h"
#include "parser.h"

#include <QThread>
#include <QVector>

namespace {

const int kBatchSize = 10000;

bool interrupted()
{
    QThread *thread = QThread::currentThread();
    return thread && thread->isInterruptionRequested();
}

void emitTocProgress(TocParseWorker *worker,
                     qint64 current,
                     qint64 total,
                     qint64 *lastReported)
{
    const qint64 reportStep = 4 * 1024 * 1024;
    if (current == total || current - *lastReported >= reportStep) {
        emit worker->progress(current, total);
        *lastReported = current;
    }
}

} // namespace

TocParseWorker::TocParseWorker(const QString &filePath, QObject *parent)
    : QObject(parent), m_filePath(filePath)
{
}

void TocParseWorker::process()
{
    FastTextReader reader;
    if (!reader.open(m_filePath)) {
        emit failed(tr("Cannot open %1: %2").arg(m_filePath, reader.errorString()));
        emit workFinished();
        return;
    }

    const qint64 total = reader.fileSize();
    qint64 lastReported = -4 * 1024 * 1024;
    qint64 nextId = 0;
    int activeEntry = -1;
    QVector<TocEntry> entries;
    QVector<QPair<int, qint64> > ancestors;

    LineWindow window;
    while (reader.nextWindow(&window)) {
        if (interrupted()) {
            emit cancelled();
            emit workFinished();
            return;
        }

        const LineView &line = window.current;
        const qint64 lineStart = line.beginOffset;
        int level = 0;
        QString title;

        if (parseHeading(line.bytes, &level, &title)) {
            if (activeEntry >= 0) {
                TocEntry &previous = entries[activeEntry];
                previous.byteLength = lineStart - previous.byteOffset;
            }

            while (!ancestors.isEmpty() && ancestors.last().first >= level)
                ancestors.removeLast();

            TocEntry entry;
            entry.id = nextId++;
            entry.parentId = ancestors.isEmpty() ? -1 : ancestors.last().second;
            entry.level = level;
            entry.title = title;
            entry.byteOffset = line.nextOffset;
            entries.append(entry);
            activeEntry = entries.size() - 1;
            ancestors.append(qMakePair(level, entry.id));
        } else if (!line.bytes.trimmed().isEmpty()) {
            if (activeEntry < 0) {
                TocEntry preamble;
                preamble.id = nextId++;
                preamble.parentId = -1;
                preamble.level = 1;
                preamble.title = tr("Preamble");
                preamble.byteOffset = lineStart;
                entries.append(preamble);
                activeEntry = 0;
                ancestors.append(qMakePair(1, preamble.id));
            }
            ++entries[activeEntry].estimatedRows;
        }

        emitTocProgress(this, line.nextOffset, total, &lastReported);
    }

    if (reader.hasError()) {
        emit failed(tr("Read failed: %1").arg(reader.errorString()));
        emit workFinished();
        return;
    }

    if (activeEntry >= 0) {
        TocEntry &last = entries[activeEntry];
        last.byteLength = total - last.byteOffset;
    }

    emit progress(total, total);
    emit succeeded(entries);
    emit workFinished();
}

FullParseWorker::FullParseWorker(const QString &filePath,
                                 const QVector<TocEntry> &entries,
                                 QObject *parent)
    : QObject(parent), m_filePath(filePath), m_entries(entries)
{
}

void FullParseWorker::process()
{
    FastTextReader reader;
    if (!reader.open(m_filePath)) {
        emit failed(tr("Cannot open %1: %2").arg(m_filePath, reader.errorString()));
        emit workFinished();
        return;
    }

    qint64 total = 0;
    for (int i = 0; i < m_entries.size(); ++i)
        total += m_entries.at(i).byteLength;

    qint64 completed = 0;
    qint64 lastReported = -4 * 1024 * 1024;
    const qint64 reportStep = 4 * 1024 * 1024;
    ParsedDocumentPtr document(new ParsedDocument);
    SimpleRecordParser parser;

    for (int sectionIndex = 0; sectionIndex < m_entries.size(); ++sectionIndex) {
        const TocEntry &entry = m_entries.at(sectionIndex);
        if (!reader.setRange(entry.byteOffset,
                             entry.byteOffset + entry.byteLength,
                             1)) {
            emit failed(tr("Cannot seek to offset %1: %2")
                            .arg(entry.byteOffset)
                            .arg(reader.errorString()));
            emit workFinished();
            return;
        }

        QVector<DataRow> &rows = document->rowsBySection[entry.id];
        if (entry.estimatedRows > 0 && entry.estimatedRows < 100000000)
            rows.reserve(static_cast<int>(entry.estimatedRows));

        LineWindow window;
        while (reader.nextWindow(&window)) {
            if (interrupted()) {
                emit cancelled();
                emit workFinished();
                return;
            }

            const LineView &line = window.current;
            completed += line.nextOffset - line.beginOffset;

            DataRow row;
            if (parser.parseLine(entry.id,
                                 line.lineNumber,
                                 static_cast<quint64>(line.beginOffset),
                                 line.bytes,
                                 &row)) {
                rows.append(row);
            }

            if (completed == total || completed - lastReported >= reportStep) {
                emit progress(completed, total);
                lastReported = completed;
            }
        }
        if (reader.hasError()) {
            emit failed(tr("Read failed: %1").arg(reader.errorString()));
            emit workFinished();
            return;
        }
    }

    emit progress(total, total);
    emit succeeded(document);
    emit workFinished();
}

SectionParseWorker::SectionParseWorker(const QString &filePath,
                                       const TocEntry &entry,
                                       quint64 generation,
                                       QObject *parent)
    : QObject(parent),
      m_filePath(filePath),
      m_entry(entry),
      m_generation(generation)
{
}

void SectionParseWorker::process()
{
    FastTextReader reader;
    if (!reader.open(m_filePath)) {
        emit failed(m_generation,
                    tr("Cannot open %1: %2").arg(m_filePath, reader.errorString()));
        emit workFinished();
        return;
    }
    if (!reader.setRange(m_entry.byteOffset,
                         m_entry.byteOffset + m_entry.byteLength,
                         1)) {
        emit failed(m_generation,
                    tr("Cannot seek to offset %1: %2")
                        .arg(m_entry.byteOffset)
                        .arg(reader.errorString()));
        emit workFinished();
        return;
    }

    SimpleRecordParser parser;
    QVector<DataRow> batch;
    batch.reserve(kBatchSize);
    bool firstBatch = true;

    LineWindow window;
    while (reader.nextWindow(&window)) {
        if (interrupted()) {
            emit cancelled(m_generation);
            emit workFinished();
            return;
        }

        const LineView &line = window.current;

        DataRow row;
        if (parser.parseLine(m_entry.id,
                             line.lineNumber,
                             static_cast<quint64>(line.beginOffset),
                             line.bytes,
                             &row)) {
            batch.append(row);
        }

        if (batch.size() == kBatchSize) {
            emit batchReady(m_generation, m_entry.id, batch, firstBatch);
            firstBatch = false;
            batch.clear();
            batch.reserve(kBatchSize);
        }
    }

    if (reader.hasError()) {
        emit failed(m_generation, tr("Read failed: %1").arg(reader.errorString()));
        emit workFinished();
        return;
    }

    if (!batch.isEmpty() || firstBatch)
        emit batchReady(m_generation, m_entry.id, batch, firstBatch);
    emit workFinished();
}
