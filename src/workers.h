#ifndef WORKERS_H
#define WORKERS_H

#include "data_types.h"

#include <QObject>
#include <QString>

class TocParseWorker : public QObject
{
    Q_OBJECT
public:
    explicit TocParseWorker(const QString &filePath, QObject *parent = 0);

public slots:
    void process();

signals:
    void progress(qint64 completedBytes, qint64 totalBytes);
    void succeeded(const QVector<TocEntry> &entries);
    void failed(const QString &message);
    void cancelled();
    void workFinished();

private:
    QString m_filePath;
};

class FullParseWorker : public QObject
{
    Q_OBJECT
public:
    FullParseWorker(const QString &filePath,
                    const QVector<TocEntry> &entries,
                    QObject *parent = 0);

public slots:
    void process();

signals:
    void progress(qint64 completedBytes, qint64 totalBytes);
    void succeeded(const ParsedDocumentPtr &document);
    void failed(const QString &message);
    void cancelled();
    void workFinished();

private:
    QString m_filePath;
    QVector<TocEntry> m_entries;
};

class SectionParseWorker : public QObject
{
    Q_OBJECT
public:
    SectionParseWorker(const QString &filePath,
                       const TocEntry &entry,
                       quint64 generation,
                       QObject *parent = 0);

public slots:
    void process();

signals:
    void batchReady(quint64 generation,
                    qint64 sectionId,
                    const QVector<DataRow> &rows,
                    bool resetModel);
    void failed(quint64 generation, const QString &message);
    void cancelled(quint64 generation);
    void workFinished();

private:
    QString m_filePath;
    TocEntry m_entry;
    quint64 m_generation;
};

#endif // WORKERS_H
