#ifndef RDB_INDEXED_FILE_HPP
#define RDB_INDEXED_FILE_HPP

#include "rdb_check_detail.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rdb {

// Index와 선택 detail을 모두 canonical rdb::Database에 기록한다.
// 이 클래스는 같은 open-file snapshot의 mmap 수명과 변경 검증만 소유한다.
class IndexedRdbFile {
public:
    explicit IndexedRdbFile(
        const std::string& path,
        const FastCheckIndexOptions& options = FastCheckIndexOptions());

    const Database& database() const { return database_; }
    std::size_t check_count() const { return database_.check_count(); }
    const RuleCheck& check(CheckId id) const { return database_.check(id); }
    std::vector<CheckId> find_checks(const std::string& name) const {
        return database_.find_checks(name);
    }

    const RuleCheck& load_check(CheckId id);
    void load_all();

private:
    IndexedRdbFile(const IndexedRdbFile&);
    IndexedRdbFile& operator=(const IndexedRdbFile&);
    void verify_file_unchanged();

    detail::MappedFile file_;
    detail::FileState file_state_;
    Database database_;
    // Property tag interning is parser-session state, not canonical Database data.
    std::vector<StringId> tag_names_;
};

} // namespace rdb

#endif // RDB_INDEXED_FILE_HPP
