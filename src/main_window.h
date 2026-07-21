#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "data_types.h"

#include <QMainWindow>
#include <QPointer>
#include <QSet>

class DetailTableModel;
class QLabel;
class QModelIndex;
class QProgressBar;
class QPushButton;
class QTableView;
class QThread;
class QTreeView;
class TocTreeModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow() override;

    void loadFile(const QString &filePath);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void chooseFile();
    void cancelParsing();
    void onTocCurrentChanged(const QModelIndex &current,
                             const QModelIndex &previous);
    void onDetailSelectionChanged();

private:
    void setupUi();
    void registerThread(QThread *thread);
    void requestInterruption(QPointer<QThread> thread);
    void startTocParse();
    void startFullParse(const QVector<TocEntry> &entries);
    void startSectionParse(const TocEntry &entry);
    void showProgress(qint64 completed, qint64 total);
    void replaceDetailRows(const QVector<DataRow> &rows);
    void appendDetailRows(const QVector<DataRow> &rows);
    void restoreDetailSelection();
    void stopAllThreads();

    QString m_filePath;
    quint64 m_documentGeneration;
    quint64 m_sectionGeneration;
    qint64 m_currentSectionId;
    bool m_internalSelectionChange;
    bool m_indexing;
    bool m_fullParsing;

    TocTreeModel *m_tocModel;
    DetailTableModel *m_detailModel;
    QTreeView *m_tocView;
    QTableView *m_detailView;
    QLabel *m_fileLabel;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_cancelButton;

    ParsedDocumentPtr m_fullDocument;
    QSet<quint64> m_desiredSelection;
    QSet<QThread *> m_threads;
    QPointer<QThread> m_indexThread;
    QPointer<QThread> m_fullThread;
    QPointer<QThread> m_sectionThread;
};

#endif // MAIN_WINDOW_H
