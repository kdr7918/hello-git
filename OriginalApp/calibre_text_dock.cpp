#include "calibre_text_dock.hpp"

#include "ui_calibre_text_dock.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCursor>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QModelIndex>
#include <QPoint>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QThread>
#include <QTreeView>

#include <stdexcept>

const int CalibreTextDock::TREE_VIEW_KEY = 0;
const int CalibreTextDock::TREE_VIEW_COUNT = 1;
int CalibreTextDock::CALIBRE_TEXT_DOCK_NUM = 0;

CalibreTextDock::CalibreTextDock(QWidget* parent)
    : QDockWidget(parent),
      ui_(new Ui::CalibreTextDock),
      is_parse_complete_(false),
      dock_id_(++CALIBRE_TEXT_DOCK_NUM),
      type_(COORDS_ONLY),
      header_menu_(new QMenu(this)),
      chip_model_(new RDBModel(RDBModel::CHIP_TABLE, this)),
      coord_model_(new RDBModel(RDBModel::COORDS_ONLY, this)),
      bg_model_(new RDBModel(RDBModel::ALL_PARAMS, this)),
      tree_model_(new RDBTreeModel(this)),
      chip_delegate_(new QStyledItemDelegate(this)),
      coord_delegate_(new QStyledItemDelegate(this)),
      index_thread_(0),
      bg_parser_(0),
      detail_thread_(0),
      detail_parser_(0) {
    qRegisterMetaType<RDB_INDEX_RESULT_PTR>("RDB_INDEX_RESULT_PTR");
    qRegisterMetaType<RDB_ALL_DATA_LIST>("RDB_ALL_DATA_LIST");
    ui_->setupUi(this);
    InitUI();
    InitSignalSlot();
    SetType(type_);
}

CalibreTextDock::~CalibreTextDock() {
    StopWorkers();
    delete ui_;
}

void CalibreTextDock::SetType(RDB_TYPE type) {
    type_ = type;
    coord_model_->SetType(
        type_ == ALL_PARAMS
            ? RDBModel::ALL_PARAMS : RDBModel::COORDS_ONLY);
    ui_->chip_name_table_view->setVisible(type_ == COORDS_ONLY);
    ui_->rdb_tree_view->setVisible(type_ == ALL_PARAMS);
    ui_->search_edit->setVisible(type_ == ALL_PARAMS);
    ui_->next_btn->setVisible(type_ == ALL_PARAMS);
    ui_->prev_btn->setVisible(type_ == ALL_PARAMS);
    ui_->search_index_label->setVisible(type_ == ALL_PARAMS);
}

void CalibreTextDock::ParseRDBCheck(
    const QString& filePath,
    const ProgressCallback& progressCallback) {
    if (filePath.isEmpty()) {
        throw std::invalid_argument("RDB file path must not be empty");
    }
    Clear();
    file_path_ = filePath;
    progress_callback_ = progressCallback;
    index_interrupt_.reset(new std::atomic<bool>(false));
    index_thread_ = new QThread;
    bg_parser_ = new BgParser(this, file_path_, index_interrupt_);
    bg_parser_->moveToThread(index_thread_);

    connect(index_thread_, &QThread::started,
            bg_parser_, &BgParser::run);
    connect(bg_parser_, &BgParser::ProgressChanged,
            this, &CalibreTextDock::OnCheckIndexProgress);
    connect(bg_parser_, &BgParser::CompleteBgParsing,
            this, &CalibreTextDock::OnCompleteCheckIndex);
    connect(bg_parser_, &BgParser::ParsingFailed,
            this, &CalibreTextDock::OnWorkerFailed);
    connect(bg_parser_, &BgParser::ParsingCancelled,
            this, &CalibreTextDock::OnWorkerCancelled);
    connect(bg_parser_, &BgParser::CompleteBgParsing,
            index_thread_, &QThread::quit, Qt::DirectConnection);
    connect(bg_parser_, &BgParser::ParsingFailed,
            index_thread_, &QThread::quit, Qt::DirectConnection);
    connect(bg_parser_, &BgParser::ParsingCancelled,
            index_thread_, &QThread::quit, Qt::DirectConnection);
    index_thread_->start();
}

void CalibreTextDock::OnClickedTreeView(const QModelIndex& modelIndex) {
    if (!modelIndex.isValid()) return;
    const RDB_ALL_DATA_LIST values = tree_model_->GetCoordsList(modelIndex);
    if (!values.empty()) {
        coord_model_->SetActiveCoords(
            values, tree_model_->GetHeaderList(modelIndex));
    } else {
        bool validCheck = false;
        const quint64 checkIndex =
            tree_model_->GetChipIndex(modelIndex, &validCheck);
        if (validCheck) coord_model_->SetActiveCheck(checkIndex);
    }
    emit UpdateCoordinateTableView();
}

void CalibreTextDock::OnClickedChipNameTableView(
    const QModelIndex& modelIndex) {
    if (!modelIndex.isValid()) return;
    const RDB_DATA_PTR check = chip_model_->CheckAt(modelIndex.row());
    if (!check) return;
    coord_model_->SetActiveCheck(check->index);
    emit ChangedCursor(check->index);
    emit UpdateCoordinateTableView();
}

void CalibreTextDock::OnClickedCoordsTableView(
    const QModelIndex& modelIndex) {
    if (!modelIndex.isValid()) return;
    const RDB_ALL_DATA_PTR value = coord_model_->CoordAt(modelIndex.row());
    if (value) emit ChangedCursor(value->index);
}

void CalibreTextDock::OnUpdateCoordsTable(
    quint64 checkIndex,
    const RDB_ALL_DATA_LIST& values,
    const QStringList& headers) {
    coord_model_->AppendCoords(checkIndex, values, headers);
    bg_model_->AppendCoords(checkIndex, values, headers);
    for (int i = 0; i < headers.size(); ++i) {
        if (!headers[i].isEmpty()) parse_header_list_.insert(headers[i]);
    }
    emit UpdateCoordinateTableView();
}

void CalibreTextDock::OnUpdateCoordinateTableView() {
    ui_->coordinate_table_view->viewport()->update();
}

void CalibreTextDock::OnCustomContextMenuRequested(const QPoint& position) {
    if (type_ != ALL_PARAMS) return;
    InitContextMenu();
    header_menu_->popup(
        ui_->rdb_tree_view->header()->mapToGlobal(position));
}

void CalibreTextDock::OnChangedCursor(quint64 index) {
    Q_UNUSED(index)
}

void CalibreTextDock::OnClearAllData() {
    Clear();
}

void CalibreTextDock::OnCompleteBgParsing() {
    is_parse_complete_ = true;
    tree_model_->SetCoords(bg_model_->CoordList());
    InitContextMenu();
    DestroyDetailWorker();
    SelectFirstNavigationItem();
}

void CalibreTextDock::OnTreeHeaderClicked(int section) {
    if (section != TREE_VIEW_KEY || type_ != ALL_PARAMS) return;
    InitContextMenu();
    header_menu_->popup(QCursor::pos());
}

void CalibreTextDock::OnMenuClicked(QAction* action) {
    if (!action) return;
    QStringList keys;
    const QList<QAction*> actions = header_menu_->actions();
    for (int i = 0; i < actions.size(); ++i) {
        if (actions[i]->isCheckable() && actions[i]->isChecked()) {
            keys.append(actions[i]->data().toString());
        }
    }
    if (keys.size() > 3) {
        action->setChecked(false);
        keys.removeAll(action->data().toString());
    }
    if (keys.isEmpty()) {
        keys.append(QStringLiteral("Check Name"));
        const QList<QAction*> currentActions = header_menu_->actions();
        for (int i = 0; i < currentActions.size(); ++i) {
            if (currentActions[i]->data().toString() == keys.front()) {
                currentActions[i]->setChecked(true);
                break;
            }
        }
    }
    tree_model_->SetCompKeys(keys);
    ui_->rdb_tree_view->expandToDepth(0);
}

void CalibreTextDock::OnSearchNextItem() {
    const QString requested = ui_->search_edit->text();
    QModelIndex found;
    if (requested != search_text_) {
        search_text_ = requested;
        tree_model_->InitSearch(search_text_);
        found = tree_model_->GetSearchIndex();
    } else {
        const int direction = sender() == ui_->prev_btn ? -1 : 1;
        found = tree_model_->SearchNext(direction);
    }
    if (found.isValid()) {
        ui_->rdb_tree_view->setCurrentIndex(found);
        ui_->rdb_tree_view->scrollTo(found);
    }
    UpdateSearchLabel();
}

void CalibreTextDock::OnCompleteCheckParsing(quint64 checkIndex) {
    Q_UNUSED(checkIndex)
}

void CalibreTextDock::OnCheckIndexProgress(int value) {
    if (progress_callback_) progress_callback_(value);
    emit CheckIndexProgress(value);
}

void CalibreTextDock::OnCompleteCheckIndex(
    const RDB_INDEX_RESULT_PTR& result) {
    DestroyIndexWorker();
    chip_model_->SetIndexResult(result);
    coord_model_->SetIndexResult(result);
    bg_model_->SetIndexResult(result);
    tree_model_->SetDBU(result ? result->dbu : 0.0);
    tree_model_->SetChecks(result ? result->chips : RDB_DATA_LIST());
    SelectFirstNavigationItem();
    StartDetailParser();
}

void CalibreTextDock::OnWorkerFailed(const QString& message) {
    DestroyIndexWorker();
    DestroyDetailWorker();
    emit ParsingFailed(message);
}

void CalibreTextDock::OnWorkerCancelled() {
    if (sender() == bg_parser_) DestroyIndexWorker();
    if (sender() == detail_parser_) DestroyDetailWorker();
}

void CalibreTextDock::InitCheckTable() {
    ui_->chip_name_table_view->setModel(chip_model_);
    ui_->chip_name_table_view->setItemDelegate(chip_delegate_);
    ui_->chip_name_table_view->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    ui_->chip_name_table_view->setSelectionMode(
        QAbstractItemView::SingleSelection);
    ui_->chip_name_table_view->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    ui_->chip_name_table_view->hideColumn(SEEK);
}

void CalibreTextDock::InitCheckTree() {
    ui_->rdb_tree_view->setModel(tree_model_);
    ui_->rdb_tree_view->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    ui_->rdb_tree_view->setSelectionMode(
        QAbstractItemView::SingleSelection);
    ui_->rdb_tree_view->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    ui_->rdb_tree_view->header()->setContextMenuPolicy(
        Qt::CustomContextMenu);
}

void CalibreTextDock::InitCoordTable() {
    ui_->coordinate_table_view->setModel(coord_model_);
    ui_->coordinate_table_view->setItemDelegate(coord_delegate_);
    ui_->coordinate_table_view->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    ui_->coordinate_table_view->setSelectionMode(
        QAbstractItemView::SingleSelection);
    ui_->coordinate_table_view->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    ui_->coordinate_table_view->setWordWrap(false);
}

void CalibreTextDock::InitContextMenu() {
    header_menu_->clear();
    QStringList available;
    available << QStringLiteral("Check Name") << QStringLiteral("Type");
    QStringList parsedHeaders = parse_header_list_.values();
    parsedHeaders.sort(Qt::CaseInsensitive);
    available.append(parsedHeaders);
    const QStringList selected = tree_model_->GetCompKey();
    for (int i = 0; i < available.size(); ++i) {
        QAction* action = header_menu_->addAction(available[i]);
        action->setData(available[i]);
        action->setCheckable(true);
        action->setChecked(selected.contains(available[i]));
    }
}

void CalibreTextDock::InitSignalSlot() {
    connect(ui_->rdb_tree_view, &QTreeView::clicked,
            this, &CalibreTextDock::OnClickedTreeView);
    connect(ui_->chip_name_table_view, &QTableView::clicked,
            this, &CalibreTextDock::OnClickedChipNameTableView);
    connect(ui_->coordinate_table_view, &QTableView::clicked,
            this, &CalibreTextDock::OnClickedCoordsTableView);
    connect(ui_->rdb_tree_view->header(), &QHeaderView::sectionClicked,
            this, &CalibreTextDock::OnTreeHeaderClicked);
    connect(
        ui_->rdb_tree_view->header(),
        &QHeaderView::customContextMenuRequested,
        this,
        &CalibreTextDock::OnCustomContextMenuRequested);
    connect(header_menu_, &QMenu::triggered,
            this, &CalibreTextDock::OnMenuClicked);
    connect(ui_->next_btn, &QPushButton::clicked,
            this, &CalibreTextDock::OnSearchNextItem);
    connect(ui_->prev_btn, &QPushButton::clicked,
            this, &CalibreTextDock::OnSearchNextItem);
    connect(ui_->search_edit, &QLineEdit::returnPressed,
            this, &CalibreTextDock::OnSearchNextItem);
    connect(this, &CalibreTextDock::UpdateCoordsTable,
            this, &CalibreTextDock::OnUpdateCoordsTable);
    connect(this, &CalibreTextDock::UpdateCoordinateTableView,
            this, &CalibreTextDock::OnUpdateCoordinateTableView);
    connect(this, &CalibreTextDock::ChangedCursor,
            this, &CalibreTextDock::OnChangedCursor);
    connect(this, &CalibreTextDock::CompleteCheckParsing,
            this, &CalibreTextDock::OnCompleteCheckParsing);
}

void CalibreTextDock::InitUI() {
    InitCheckTable();
    InitCheckTree();
    InitCoordTable();
    InitContextMenu();
    ui_->splitter->setStretchFactor(0, 1);
    ui_->splitter->setStretchFactor(1, 2);
    ui_->search_index_label->setText(QStringLiteral("0 / 0"));
    setObjectName(QStringLiteral("CalibreTextDock_%1").arg(dock_id_));
}

void CalibreTextDock::Clear() {
    StopWorkers();
    is_parse_complete_ = false;
    file_path_.clear();
    parse_header_list_.clear();
    search_text_.clear();
    progress_callback_ = ProgressCallback();
    chip_model_->Clear();
    coord_model_->Clear();
    bg_model_->Clear();
    tree_model_->SetChecks(RDB_DATA_LIST());
    ui_->search_edit->clear();
    UpdateSearchLabel();
}

void CalibreTextDock::StartDetailParser() {
    if (file_path_.isEmpty() || chip_model_->ChipList().empty()) {
        is_parse_complete_ = true;
        return;
    }
    detail_interrupt_.reset(new std::atomic<bool>(false));
    detail_thread_ = new QThread;
    detail_parser_ = new RDBDetailParser(
        file_path_, chip_model_->ChipList(), detail_interrupt_);
    detail_parser_->moveToThread(detail_thread_);

    connect(detail_thread_, &QThread::started,
            detail_parser_, &RDBDetailParser::run);
    connect(detail_parser_, &RDBDetailParser::BatchReady,
            this, &CalibreTextDock::UpdateCoordsTable);
    connect(detail_parser_, &RDBDetailParser::CheckParsingComplete,
            this, &CalibreTextDock::CompleteCheckParsing);
    connect(detail_parser_, &RDBDetailParser::Complete,
            this, &CalibreTextDock::OnCompleteBgParsing);
    connect(detail_parser_, &RDBDetailParser::ParsingFailed,
            this, &CalibreTextDock::OnWorkerFailed);
    connect(detail_parser_, &RDBDetailParser::ParsingCancelled,
            this, &CalibreTextDock::OnWorkerCancelled);
    connect(detail_parser_, &RDBDetailParser::Complete,
            detail_thread_, &QThread::quit, Qt::DirectConnection);
    connect(detail_parser_, &RDBDetailParser::ParsingFailed,
            detail_thread_, &QThread::quit, Qt::DirectConnection);
    connect(detail_parser_, &RDBDetailParser::ParsingCancelled,
            detail_thread_, &QThread::quit, Qt::DirectConnection);
    detail_thread_->start();
}

void CalibreTextDock::StopWorkers() {
    if (index_interrupt_) index_interrupt_->store(true);
    if (detail_interrupt_) detail_interrupt_->store(true);
    if (index_thread_) {
        index_thread_->quit();
        index_thread_->wait();
    }
    if (detail_thread_) {
        detail_thread_->quit();
        detail_thread_->wait();
    }
    DestroyIndexWorker();
    DestroyDetailWorker();
}

void CalibreTextDock::DestroyIndexWorker() {
    if (index_thread_ && index_thread_->isRunning()) {
        index_thread_->quit();
        index_thread_->wait();
    }
    delete bg_parser_;
    bg_parser_ = 0;
    delete index_thread_;
    index_thread_ = 0;
    index_interrupt_.reset();
}

void CalibreTextDock::DestroyDetailWorker() {
    if (detail_thread_ && detail_thread_->isRunning()) {
        detail_thread_->quit();
        detail_thread_->wait();
    }
    delete detail_parser_;
    detail_parser_ = 0;
    delete detail_thread_;
    detail_thread_ = 0;
    detail_interrupt_.reset();
}

void CalibreTextDock::SelectFirstNavigationItem() {
    if (type_ == COORDS_ONLY) {
        const QModelIndex first = chip_model_->index(0, 0);
        if (first.isValid()) {
            ui_->chip_name_table_view->setCurrentIndex(first);
            OnClickedChipNameTableView(first);
        }
        return;
    }
    const QModelIndex first = tree_model_->index(0, 0);
    if (first.isValid()) {
        ui_->rdb_tree_view->setCurrentIndex(first);
        OnClickedTreeView(first);
    }
}

void CalibreTextDock::UpdateSearchLabel() {
    const int count = tree_model_->SearchCount();
    const int position = tree_model_->SearchPosition();
    ui_->search_index_label->setText(
        QStringLiteral("%1 / %2").arg(position < 0 ? 0 : position + 1).arg(count));
    ui_->next_btn->setEnabled(count > 0);
    ui_->prev_btn->setEnabled(count > 0);
}
