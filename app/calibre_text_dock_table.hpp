#ifndef CALIBRE_TEXT_DOCK_TABLE_HPP
#define CALIBRE_TEXT_DOCK_TABLE_HPP

#include "rdb_table_model.hpp"

#include <QDockWidget>
#include <QPersistentModelIndex>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

class QEvent;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTreeView;
class QWidget;
class QString;
class QPoint;
class RdbTreeModel;

// Check Index와 Detail 결과를 표시하고 백그라운드 파싱 수명을 관리한다.
class CalibreTextDockTable : public QDockWidget {
public:
    typedef std::function<void(int)> ProgressCallback;

    explicit CalibreTextDockTable(QWidget* parent = 0);
    ~CalibreTextDockTable() override;

    void LoadRdbIndex(const QString& path, bool allParameters);
    std::size_t LoadRdbIndex(
        const QString& path,
        RdbTableModel::ModelType detailType,
        const ProgressCallback& progressCallback = ProgressCallback());

protected:
    // QDockWidget 가상 함수이므로 Qt가 요구하는 소문자 이름을 유지한다.
    bool event(QEvent* event) override;

private:
    struct ParserTask;

    void OnCheckRowSelected(const QModelIndex& current);
    void OnCheckTreeSelected(const QModelIndex& current);
    void StartBackgroundDetailParser(
        const std::vector<rdb::CheckId>& priorityCheckIds =
            std::vector<rdb::CheckId>());
    void RestartBackgroundDetailParserForSelection();
    void CancelDetailParsers();
    void ReapFinishedParserTasks();
    void ConfigureDetailHeader();
    void ConfigureCheckView();
    void SelectFirstCheck();
    void EnableTreeGroupingMenu(bool enabled);
    void ShowTreeGroupingMenu(const QPoint& position);
    void ApplyTreeGrouping(int depth, const QString& category);
    void SearchTree();
    void MoveTreeSearch(int direction);
    void ClearTreeSearchResults();
    void SelectTreeSearchResult();
    void ShowStatusMessage(const QString& message);
    void ShowDetailError(const QString& message);

    QStackedWidget* check_stack_;
    QTableView* check_table_;
    QWidget* all_params_page_;
    QTreeView* check_tree_view_;
    QLineEdit* tree_search_edit_;
    QPushButton* tree_search_button_;
    QPushButton* tree_search_previous_button_;
    QPushButton* tree_search_next_button_;
    QTableView* coords_table_;
    RdbTableModel* check_model_;
    RdbTableModel* detail_model_;
    RdbTreeModel* tree_model_;
    QString source_path_;
    RdbTableModel::ModelType detail_type_;
    unsigned long long active_request_id_;
    QString source_file_identity_;
    std::vector<std::unique_ptr<ParserTask> > parser_tasks_;
    std::vector<QPersistentModelIndex> tree_search_results_;
    std::size_t tree_search_position_;
    QString tree_search_text_;
    bool selection_restart_enabled_;
    bool tree_grouping_menu_enabled_;
};

#endif // CALIBRE_TEXT_DOCK_TABLE_HPP
