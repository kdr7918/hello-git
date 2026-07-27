#ifndef QT_RDB_VIEWER_HPP
#define QT_RDB_VIEWER_HPP

#include "ascii_rdb_parser.hpp"
#include "rdb_check_detail.hpp"
#include "rdb_check_geometry.hpp"
#include "rdb_check_geometry_detail.hpp"
#include "rdb_viewer_models.hpp"

#include <QHash>
#include <QMainWindow>
#include <QPair>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>
#include <vector>

class QDockWidget;
class QLabel;
class QLineEdit;
class QProgressBar;
class QStandardItem;
class QStandardItemModel;
class QStackedWidget;
class QTableView;
class QTreeView;

struct RdbViewerAsyncState;

struct GroupingDimension {
    enum Kind { CheckName, TaggedValueKey };

    Kind kind;
    QString key;

    GroupingDimension() : kind(CheckName) {}
    explicit GroupingDimension(Kind type, const QString& value = QString())
        : kind(type), key(value) {}
    bool equals(const GroupingDimension& other) const {
        return kind == other.kind && key == other.key;
    }
};

struct GroupCondition {
    GroupingDimension dimension;
    QString value;
};

struct TreeSelection {
    QVector<int> checkRows;
    std::vector<GroupCondition> conditions;
    QString identity;
};

class RdbViewer : public QMainWindow {
public:
    explicit RdbViewer(QWidget* parent = 0);
    ~RdbViewer() override;

    void openFile(const QString& path);
    void openFile(const QString& path, RdbViewerMode mode);

protected:
    bool event(QEvent* event) override;

private:
    typedef std::shared_ptr<std::atomic_bool> CancellationToken;

    void openWithMode(RdbViewerMode mode);
    void startIndexParsing(quint64 fileGeneration);
    void startBackgroundParsing(quint64 fileGeneration);
    void startSelectedDetailParsing(const QVector<int>& rows, quint64 requestId);
    void showSelectedCheckRow(int row);
    void showSelectedTreeNode(const QModelIndex& current);
    void showSelectedChecks(const QVector<int>& rows);
    void showBackgroundDetail();
    void replaceDetailRowsPreservingSelection(const QVector<DetailRow>& rows);
    void appendDetailRowsPreservingSelection(const QVector<DetailRow>& rows);
    void cancelSelectedDetail();
    void cancelAllParsing();
    CancellationToken newCancellationToken();
    void reportError(const QString& title, const std::exception& error);

    void rebuildIndexTree(bool preserveSelection);
    void rebuildFullTree(bool preserveSelection);
    void appendFullTreeLevel(QStandardItem* parent,
                             int depth,
                             const QVector<QPair<int, rdb::Index> >& results,
                             const std::vector<GroupCondition>& conditions);
    void appendTreeRow(QStandardItem* parent,
                       const QString& label,
                       qulonglong count,
                       const TreeSelection& selection);
    QString addTreeSelection(const TreeSelection& selection);
    QString currentTreeSelectionIdentity() const;
    void restoreTreeSelection(const QString& identity);
    void showGroupingMenu(const QPoint& point);
    void setGrouping(int depth, const GroupingDimension& dimension);
    QStringList availableTagKeys() const;
    void findTreeText(bool forward);

    bool resultMatchesConditions(const rdb::Database& database,
                                 int checkRow,
                                 const rdb::Result& result,
                                 const std::vector<GroupCondition>& conditions) const;
    QStringList valuesForDimension(const rdb::Database& database,
                                   int checkRow,
                                   const rdb::Result& result,
                                   const GroupingDimension& dimension) const;
    static QString conditionIdentity(const std::vector<GroupCondition>& conditions);
    static QString checkIdentity(const QString& name);

    static QVector<DetailRow> rowsFromDetailBatch(
        const std::vector<rdb::DetailResult>& batch, rdb::CheckOffset offset);
    static QVector<DetailRow> rowsFromGeometryDetailBatch(
        const std::vector<rdb::GeometryDetailResult>& batch, rdb::CheckOffset offset);
    static QVector<DetailRow> rowsFromGeometryDatabase(
        const rdb::GeometryDatabase& database, const QVector<int>& rows);
    QVector<DetailRow> rowsFromDatabase() const;
    static QString coordinatesFromPoints(const std::vector<rdb::Point>& points);
    static QString coordinatesFromEdges(const std::vector<rdb::Edge>& edges);
    static QString resultKey(rdb::CheckOffset offset, rdb::ResultKind kind, std::uint32_t ordinal);
    static void appendTags(DetailRow& row,
                           const rdb::Database& database,
                           const rdb::Range& range);

    RdbViewerMode mode_;
    QString path_;
    quint64 fileGeneration_;
    quint64 detailRequestId_;
    QVector<int> selectedCheckRows_;
    std::vector<GroupCondition> selectedConditions_;
    CancellationToken selectedCancellation_;
    std::vector<CancellationToken> cancellationTokens_;
    std::shared_ptr<RdbViewerAsyncState> asyncState_;
    std::shared_ptr<rdb::Database> fullDatabase_;
    std::shared_ptr<rdb::GeometryDatabase> geometryDatabase_;

    CheckTableModel* checkModel_;
    ResultTableModel* detailModel_;
    QStandardItemModel* treeModel_;
    QDockWidget* dock_;
    QStackedWidget* leftStack_;
    QTableView* checkView_;
    QTreeView* treeView_;
    QTableView* detailView_;
    QLineEdit* treeSearch_;
    QLabel* statusLabel_;
    QProgressBar* indexProgress_;

    std::vector<GroupingDimension> grouping_;
    QHash<QString, TreeSelection> treeSelections_;
    quint64 treeSelectionSerial_;
    bool rebuildingTree_;
};

#endif // QT_RDB_VIEWER_HPP
