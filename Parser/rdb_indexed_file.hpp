#ifndef RDB_INDEXED_FILE_HPP
#define RDB_INDEXED_FILE_HPP

#include "rdb_check_detail.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rdb {

// Index와 선택 detail을 모두 canonical rdb::Database에 기록한다.
// 표준 C++로 읽은 동일 파일 byte snapshot을 복사본끼리 공유한다.
class IndexedRdbFile {
public:
    explicit IndexedRdbFile(
        const std::string& path,
        const FastCheckIndexOptions& options = FastCheckIndexOptions());
    IndexedRdbFile(const IndexedRdbFile&) = default;
    IndexedRdbFile(IndexedRdbFile&&) noexcept = default;

    // Copy assignment deep-copies Database/parser state. Passing an rvalue moves
    // the fully parsed pools, which is the efficient worker-thread replacement path.
    IndexedRdbFile& operator=(IndexedRdbFile other) noexcept;
    void swap(IndexedRdbFile& other) noexcept;

    const Database& database() const { return database_; }
    std::size_t check_count() const { return database_.check_count(); }
    const RuleCheck& check(CheckId id) const { return database_.check(id); }
    std::vector<CheckId> find_checks(const std::string& name) const {
        return database_.find_checks(name);
    }

    const RuleCheck& load_check(CheckId id);
    void load_all();

private:
    void verify_file_unchanged();

    // Copies share immutable file bytes while Database state is deep-copied.
    std::shared_ptr<detail::FileBuffer> file_;
    Database database_;
    // Property tag interning is parser-session state, not canonical Database data.
    std::vector<StringId> tag_names_;
};

} // namespace rdb

#endif // RDB_INDEXED_FILE_HPP
