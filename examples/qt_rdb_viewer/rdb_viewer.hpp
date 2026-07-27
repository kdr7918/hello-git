#ifndef QT_RDB_VIEWER_HPP
#define QT_RDB_VIEWER_HPP

#include "ascii_rdb_parser.hpp"
#include "rdb_check_detail.hpp"
#include "rdb_viewer_models.hpp"

#include <QMainWindow>
#include <QPointer>

#include <atomic>
#include <memory>
#include <vector>

class QLabel;
class QProgressBar;
class QTableView;

template <typename T> class QFutureWatcher;
struct RdbViewerAsyncState;

class RdbViewer : public QMainWindow {
public:
    explicit RdbViewer(QWidget* parent = nullptr);
    ~RdbViewer() override;

    void openFile(const QString& path);

private:
    typedef std::shared_ptr<std::atomic_bool> CancellationToken;

    void startIndexParsing(quint64 fileGeneration);
    void startFullBackgroundParsing(quint64 fileGeneration);
    void showSelectedCheck(int row);
    void startSelectedDetailParsing(int row, quint64 requestId);
    void showBackgroundDetail(int row);
    void replaceDetailRowsPreservingSelection(const QVector<DetailRow>& rows);
    void appendDetailRowsPreservingSelection(const QVector<DetailRow>& rows);
    void cancelSelectedDetail();
    void cancelAllParsing();
    CancellationToken newCancellationToken();
    void reportError(const QString& title, const std::exception& error);

    static QVector<DetailRow> rowsFromDetailBatch(const std::vector<rdb::DetailResult>& batch);
    static QVector<DetailRow> rowsFromDatabase(const rdb::Database& database, std::size_t checkIndex);
    static QString resultSummary(const std::string& suffix,
                                 std::size_t beforeTagCount,
                                 std::size_t afterTagCount);

    QString path_;
    quint64 fileGeneration_;
    quint64 detailRequestId_;
    int selectedCheckRow_;
    CancellationToken selectedCancellation_;
    std::vector<CancellationToken> cancellationTokens_;
    std::shared_ptr<RdbViewerAsyncState> asyncState_;
    std::shared_ptr<rdb::Database> fullDatabase_;

    CheckTableModel* checkModel_;
    DetailTableModel* detailModel_;
    QTableView* checkView_;
    QTableView* detailView_;
    QLabel* statusLabel_;
    QProgressBar* indexProgress_;
};

#endif // QT_RDB_VIEWER_HPP
