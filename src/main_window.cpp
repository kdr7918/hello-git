#include "main_window.h"

#include "detail_table_model.h"
#include "toc_tree_model.h"
#include "workers.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QThread>
#include <QTreeView>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_documentGeneration(0),
      m_sectionGeneration(0),
      m_currentSectionId(-1),
      m_internalSelectionChange(false),
      m_indexing(false),
      m_fullParsing(false),
      m_tocModel(new TocTreeModel(this)),
      m_detailModel(new DetailTableModel(this)),
      m_tocView(0),
      m_detailView(0),
      m_fileLabel(0),
      m_statusLabel(0),
      m_progressBar(0),
      m_cancelButton(0)
{
    qRegisterMetaType<QVector<TocEntry> >("QVector<TocEntry>");
    qRegisterMetaType<QVector<DataRow> >("QVector<DataRow>");
    qRegisterMetaType<ParsedDocumentPtr>("ParsedDocumentPtr");
    setupUi();
}

MainWindow::~MainWindow()
{
    stopAllThreads();
}

void MainWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    QHBoxLayout *commandLayout = new QHBoxLayout;

    QPushButton *openButton = new QPushButton(tr("Open file..."), central);
    m_fileLabel = new QLabel(tr("No file selected"), central);
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_progressBar = new QProgressBar(central);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setMinimumWidth(180);
    m_cancelButton = new QPushButton(tr("Interrupt"), central);
    m_cancelButton->setEnabled(false);

    commandLayout->addWidget(openButton);
    commandLayout->addWidget(m_fileLabel, 1);
    commandLayout->addWidget(m_progressBar);
    commandLayout->addWidget(m_cancelButton);

    m_tocView = new QTreeView(central);
    m_tocView->setModel(m_tocModel);
    m_tocView->setAlternatingRowColors(true);
    m_tocView->setUniformRowHeights(true);
    m_tocView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tocView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    m_detailView = new QTableView(central);
    m_detailView->setModel(m_detailModel);
    m_detailView->setAlternatingRowColors(true);
    m_detailView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_detailView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_detailView->setSortingEnabled(false);
    m_detailView->setWordWrap(false);
    m_detailView->verticalHeader()->setDefaultSectionSize(22);
    m_detailView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_detailView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_detailView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_detailView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_detailView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(m_tocView);
    splitter->addWidget(m_detailView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);

    m_statusLabel = new QLabel(tr("Choose a large text file."), central);
    rootLayout->addLayout(commandLayout);
    rootLayout->addWidget(splitter, 1);
    rootLayout->addWidget(m_statusLabel);

    setCentralWidget(central);
    setWindowTitle(tr("Large File TOC / Detail Viewer"));
    resize(1200, 720);

    connect(openButton, &QPushButton::clicked,
            this, &MainWindow::chooseFile);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &MainWindow::cancelParsing);
    connect(m_tocView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onTocCurrentChanged);
    connect(m_detailView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onDetailSelectionChanged);
}

void MainWindow::chooseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open large data file"), QString(), tr("Text files (*.txt *.csv *.log);;All files (*)"));
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::loadFile(const QString &filePath)
{
    if (!QFileInfo(filePath).isFile()) {
        QMessageBox::warning(this, tr("Open failed"),
                             tr("The selected path is not a file."));
        return;
    }

    ++m_documentGeneration;
    ++m_sectionGeneration;
    for (QSet<QThread *>::const_iterator it = m_threads.constBegin();
         it != m_threads.constEnd(); ++it) {
        (*it)->requestInterruption();
    }

    m_filePath = filePath;
    m_fullDocument.clear();
    m_currentSectionId = -1;
    m_desiredSelection.clear();
    m_tocModel->setEntries(QVector<TocEntry>());
    replaceDetailRows(QVector<DataRow>());
    m_fileLabel->setText(QFileInfo(filePath).fileName());
    m_fileLabel->setToolTip(filePath);
    startTocParse();
}

void MainWindow::registerThread(QThread *thread)
{
    m_threads.insert(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        m_threads.remove(thread);
        if (m_indexThread == thread)
            m_indexThread.clear();
        if (m_fullThread == thread)
            m_fullThread.clear();
        if (m_sectionThread == thread)
            m_sectionThread.clear();
        thread->deleteLater();
    });
}

void MainWindow::requestInterruption(QPointer<QThread> thread)
{
    if (thread)
        thread->requestInterruption();
}

void MainWindow::startTocParse()
{
    const quint64 generation = m_documentGeneration;
    QThread *thread = new QThread(this);
    TocParseWorker *worker = new TocParseWorker(m_filePath);
    worker->moveToThread(thread);
    registerThread(thread);
    m_indexThread = thread;
    m_indexing = true;
    m_fullParsing = false;
    m_cancelButton->setEnabled(true);
    m_statusLabel->setText(tr("Scanning the table of contents..."));
    showProgress(0, 1);

    connect(thread, &QThread::started, worker, &TocParseWorker::process);
    connect(worker, &TocParseWorker::progress, this,
            [this, generation](qint64 completed, qint64 total) {
        if (generation == m_documentGeneration)
            showProgress(completed, total);
    });
    connect(worker, &TocParseWorker::succeeded, this,
            [this, generation](const QVector<TocEntry> &entries) {
        if (generation != m_documentGeneration)
            return;
        m_indexing = false;
        m_tocModel->setEntries(entries);
        m_tocView->expandAll();
        const QModelIndex first = m_tocModel->firstEntryIndex();
        if (first.isValid())
            m_tocView->setCurrentIndex(first);
        m_statusLabel->setText(tr("TOC ready. Parsing the complete file in background..."));
        startFullParse(entries);
    });
    connect(worker, &TocParseWorker::failed, this,
            [this, generation](const QString &message) {
        if (generation != m_documentGeneration)
            return;
        m_indexing = false;
        m_cancelButton->setEnabled(false);
        m_statusLabel->setText(message);
        QMessageBox::critical(this, tr("Parse failed"), message);
    });
    connect(worker, &TocParseWorker::cancelled, this,
            [this, generation]() {
        if (generation != m_documentGeneration)
            return;
        m_indexing = false;
        m_cancelButton->setEnabled(false);
        m_statusLabel->setText(tr("TOC scan interrupted."));
    });
    connect(worker, &TocParseWorker::workFinished,
            thread, &QThread::quit, Qt::DirectConnection);
    connect(worker, &TocParseWorker::workFinished,
            worker, &QObject::deleteLater);
    thread->start();
}

void MainWindow::startFullParse(const QVector<TocEntry> &entries)
{
    const quint64 generation = m_documentGeneration;
    QThread *thread = new QThread(this);
    FullParseWorker *worker = new FullParseWorker(m_filePath, entries);
    worker->moveToThread(thread);
    registerThread(thread);
    m_fullThread = thread;
    m_fullParsing = true;
    m_cancelButton->setEnabled(true);
    showProgress(0, 1);

    connect(thread, &QThread::started, worker, &FullParseWorker::process);
    connect(worker, &FullParseWorker::progress, this,
            [this, generation](qint64 completed, qint64 total) {
        if (generation == m_documentGeneration)
            showProgress(completed, total);
    });
    connect(worker, &FullParseWorker::succeeded, this,
            [this, generation](const ParsedDocumentPtr &document) {
        if (generation != m_documentGeneration)
            return;
        m_fullParsing = false;
        m_fullDocument = document;
        ++m_sectionGeneration;
        requestInterruption(m_sectionThread);

        const QHash<qint64, QVector<DataRow> >::const_iterator found =
            document->rowsBySection.constFind(m_currentSectionId);
        if (found != document->rowsBySection.constEnd())
            replaceDetailRows(found.value());

        m_progressBar->setValue(m_progressBar->maximum());
        m_cancelButton->setEnabled(false);
        m_statusLabel->setText(tr("Background parsing complete. Final table installed."));
    });
    connect(worker, &FullParseWorker::failed, this,
            [this, generation](const QString &message) {
        if (generation != m_documentGeneration)
            return;
        m_fullParsing = false;
        m_cancelButton->setEnabled(false);
        m_statusLabel->setText(message);
        QMessageBox::critical(this, tr("Parse failed"), message);
    });
    connect(worker, &FullParseWorker::cancelled, this,
            [this, generation]() {
        if (generation != m_documentGeneration)
            return;
        m_fullParsing = false;
        m_cancelButton->setEnabled(false);
        m_statusLabel->setText(tr("Background parsing interrupted. On-demand parsing remains available."));
    });
    connect(worker, &FullParseWorker::workFinished,
            thread, &QThread::quit, Qt::DirectConnection);
    connect(worker, &FullParseWorker::workFinished,
            worker, &QObject::deleteLater);
    thread->start();
}

void MainWindow::onTocCurrentChanged(const QModelIndex &current,
                                     const QModelIndex &)
{
    if (!current.isValid())
        return;

    const TocEntry entry = m_tocModel->entryForIndex(current);
    if (entry.id < 0)
        return;
    m_currentSectionId = entry.id;

    if (m_fullDocument) {
        const QHash<qint64, QVector<DataRow> >::const_iterator found =
            m_fullDocument->rowsBySection.constFind(entry.id);
        replaceDetailRows(found == m_fullDocument->rowsBySection.constEnd()
                              ? QVector<DataRow>()
                              : found.value());
        m_statusLabel->setText(tr("Showing completed data for '%1'.").arg(entry.title));
        return;
    }

    startSectionParse(entry);
}

void MainWindow::startSectionParse(const TocEntry &entry)
{
    requestInterruption(m_sectionThread);
    const quint64 generation = ++m_sectionGeneration;
    QThread *thread = new QThread(this);
    SectionParseWorker *worker = new SectionParseWorker(
        m_filePath, entry, generation);
    worker->moveToThread(thread);
    registerThread(thread);
    m_sectionThread = thread;
    m_statusLabel->setText(tr("Quick-parsing '%1' in 10,000-row batches...")
                               .arg(entry.title));

    connect(thread, &QThread::started, worker, &SectionParseWorker::process);
    connect(worker, &SectionParseWorker::batchReady, this,
            [this](quint64 batchGeneration,
                   qint64 sectionId,
                   const QVector<DataRow> &rows,
                   bool resetModel) {
        if (batchGeneration != m_sectionGeneration ||
            sectionId != m_currentSectionId || m_fullDocument) {
            return;
        }
        if (resetModel)
            replaceDetailRows(rows);
        else
            appendDetailRows(rows);
        m_statusLabel->setText(tr("Section rows loaded: %1")
                                   .arg(m_detailModel->rowCount()));
    });
    connect(worker, &SectionParseWorker::failed, this,
            [this](quint64 workerGeneration, const QString &message) {
        if (workerGeneration == m_sectionGeneration)
            m_statusLabel->setText(message);
    });
    connect(worker, &SectionParseWorker::workFinished,
            thread, &QThread::quit, Qt::DirectConnection);
    connect(worker, &SectionParseWorker::workFinished,
            worker, &QObject::deleteLater);
    thread->start();
}

void MainWindow::onDetailSelectionChanged()
{
    if (m_internalSelectionChange)
        return;

    m_desiredSelection.clear();
    const QModelIndexList selected =
        m_detailView->selectionModel()->selectedRows(0);
    for (int i = 0; i < selected.size(); ++i)
        m_desiredSelection.insert(m_detailModel->stableKeyAt(selected.at(i).row()));
}

void MainWindow::replaceDetailRows(const QVector<DataRow> &rows)
{
    m_internalSelectionChange = true;
    m_detailModel->replaceRows(rows);
    restoreDetailSelection();
    m_internalSelectionChange = false;
}

void MainWindow::appendDetailRows(const QVector<DataRow> &rows)
{
    m_internalSelectionChange = true;
    m_detailModel->appendRows(rows);
    restoreDetailSelection();
    m_internalSelectionChange = false;
}

void MainWindow::restoreDetailSelection()
{
    QItemSelectionModel *selection = m_detailView->selectionModel();
    selection->clearSelection();
    for (QSet<quint64>::const_iterator it = m_desiredSelection.constBegin();
         it != m_desiredSelection.constEnd(); ++it) {
        const int row = m_detailModel->rowForStableKey(*it);
        if (row >= 0) {
            const QModelIndex first = m_detailModel->index(row, 0);
            const QModelIndex last = m_detailModel->index(
                row, m_detailModel->columnCount() - 1);
            QItemSelection range(first, last);
            selection->select(range, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
}

void MainWindow::showProgress(qint64 completed, qint64 total)
{
    if (total <= 0) {
        m_progressBar->setRange(0, 0);
        return;
    }
    if (m_progressBar->minimum() != 0 || m_progressBar->maximum() != 1000)
        m_progressBar->setRange(0, 1000);
    const qint64 bounded = qMax<qint64>(0, qMin(completed, total));
    m_progressBar->setValue(static_cast<int>((bounded * 1000) / total));
}

void MainWindow::cancelParsing()
{
    ++m_sectionGeneration;
    for (QSet<QThread *>::const_iterator it = m_threads.constBegin();
         it != m_threads.constEnd(); ++it) {
        (*it)->requestInterruption();
    }
    m_cancelButton->setEnabled(false);
    m_statusLabel->setText(tr("Interruption requested..."));
}

void MainWindow::stopAllThreads()
{
    const QList<QThread *> threads = m_threads.values();
    for (int i = 0; i < threads.size(); ++i)
        threads.at(i)->requestInterruption();
    for (int i = 0; i < threads.size(); ++i)
        threads.at(i)->wait();
    m_threads.clear();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopAllThreads();
    event->accept();
}
