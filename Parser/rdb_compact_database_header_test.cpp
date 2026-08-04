#include "rdb_compact_database.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

static_assert(sizeof(rdb::CheckId) == 4U, "compact check IDs must be 32 bit");
static_assert(std::is_same<
                  decltype(std::declval<const rdb::CompactResultView&>().vertex(0U)),
                  rdb::Point>::value,
              "compact vertices must be returned by value");
static_assert(std::is_same<
                  decltype(std::declval<const rdb::CompactResultView&>().edge(0U)),
                  rdb::Edge>::value,
              "compact edges must be returned by value");
static_assert(sizeof(rdb::CheckOffset) == 8U, "file offsets must remain 64 bit");
static_assert(sizeof(rdb::CompactCheckRecord) <= 32U, "compact check record is too large");
static_assert(sizeof(rdb::CompactResultRecord) <= 32U, "compact result record is too large");
static_assert(sizeof(rdb::CompactPropertyRecord) <= 12U, "compact property record is too large");

int main() {
    rdb::CompactCheckDatabase database;
    return database.check_count() == 0U ? 0 : 1;
}