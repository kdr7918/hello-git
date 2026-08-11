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

    /// Parser 결과를 GUI thread까지 전달하고 미처리 배치 수를 증가시킨다.
    RDB_DETAIL_BATCH(
        const std::vector<rdb::DetailResult>& source,
        const std::shared_ptr<std::atomic<std::size_t> >& pending);
    /// 마지막 배치 참조가 사라질 때 미처리 배치 수를 감소시킨다.
    ~RDB_DETAIL_BATCH();
};

typedef std::shared_ptr<RDB_DETAIL_BATCH> RDB_DETAIL_BATCH_PTR;

/// Check Index 결과를 Table/TreeModel이 공유할 Database로 변환한다.
RDB_DATABASE_PTR MakeDatabaseFromIndex(const rdb::CheckIndexDatabase& index);
/// 상세 결과를 제외한 Check Index 영역만 복제해 선택 파싱용 DB를 만든다.
RDB_DATABASE_PTR CloneIndexDatabase(const RDB_DATABASE_PTR& source);
/// StringTable의 ID를 화면 표시용 QString으로 안전하게 변환한다.
QString RDBString(const rdb::StringTable& strings, rdb::StringId id);
/// StringTable의 ID가 외부 문자열과 바이트 단위로 같은지 비교한다.
bool RDBSameText(
    const rdb::StringTable& strings,
    rdb::StringId id,
    const std::string& value);

Q_DECLARE_METATYPE(RDB_DATABASE_PTR)
Q_DECLARE_METATYPE(RDB_DETAIL_BATCH_PTR)
Q_DECLARE_METATYPE(RDB_CHECK_DETAIL_PTR)

#endif // DATABASE_APP_RDB_DATABASE_SUPPORT_HPP
