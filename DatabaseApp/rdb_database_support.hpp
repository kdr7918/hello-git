#ifndef DATABASE_APP_RDB_DATABASE_SUPPORT_HPP
#define DATABASE_APP_RDB_DATABASE_SUPPORT_HPP

#include "rdb_check_detail.hpp"

#include <QMetaType>
#include <QString>

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

typedef std::shared_ptr<rdb::Database> RDB_DATABASE_PTR;
typedef std::shared_ptr<rdb::CheckDetail> RDB_CHECK_DETAIL_PTR;

// DetailResult 자체는 parser callback 동안만 유효하다. 이 객체가 배치를
// GUI thread까지 소유하고, 마지막 참조가 해제될 때 pending count를 낮춘다.
struct RDB_DETAIL_BATCH {
    std::vector<rdb::DetailResult> values;
    std::shared_ptr<std::atomic<std::size_t> > pending_batches;

    RDB_DETAIL_BATCH(
        const std::vector<rdb::DetailResult>& source,
        const std::shared_ptr<std::atomic<std::size_t> >& pending);
    ~RDB_DETAIL_BATCH();
};

typedef std::shared_ptr<RDB_DETAIL_BATCH> RDB_DETAIL_BATCH_PTR;

RDB_DATABASE_PTR MakeDatabaseFromIndex(const rdb::CheckIndexDatabase& index);
RDB_DATABASE_PTR CloneIndexDatabase(const RDB_DATABASE_PTR& source);
QString RDBString(const rdb::StringTable& strings, rdb::StringId id);
bool RDBSameText(
    const rdb::StringTable& strings,
    rdb::StringId id,
    const std::string& value);

Q_DECLARE_METATYPE(RDB_DATABASE_PTR)
Q_DECLARE_METATYPE(RDB_DETAIL_BATCH_PTR)
Q_DECLARE_METATYPE(RDB_CHECK_DETAIL_PTR)

#endif // DATABASE_APP_RDB_DATABASE_SUPPORT_HPP
