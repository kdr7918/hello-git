#ifndef RDB_COMPACT_DATABASE_HPP
#define RDB_COMPACT_DATABASE_HPP

#include "rdb_check_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace rdb {

// All compact IDs and ranges are 32 bit. CheckOffset deliberately remains 64 bit.
typedef std::uint32_t CheckId;
typedef std::uint32_t CompactIndex;

inline CheckId invalid_check_id() { return std::numeric_limits<CheckId>::max(); }

struct CompactRange {
    CompactIndex begin;
    CompactIndex count;
    CompactRange() : begin(0), count(0) {}
    CompactRange(CompactIndex first, CompactIndex size) : begin(first), count(size) {}
};

struct TextRef {
    CompactIndex offset;
    CompactIndex size;
    TextRef() : offset(0), size(0) {}
    TextRef(CompactIndex first, CompactIndex length) : offset(first), size(length) {}
};

// A borrowed view into the database byte pool. Loading more details can reallocate
// that pool and invalidate previously returned TextViews; obtain the view again then.
struct TextView {
    const char* data;
    CompactIndex size;
    TextView() : data(0), size(0) {}
    TextView(const char* value, CompactIndex length) : data(value), size(length) {}
    bool empty() const { return size == 0; }
    std::string str() const { return data == 0 ? std::string() : std::string(data, size); }
};

struct CompactCheckRecord {
    CheckOffset offset;
    TextRef name;
    TextRef comment;
    std::uint32_t result_count;
    CompactIndex detail;
    CompactCheckRecord()
        : offset(0), result_count(0), detail(std::numeric_limits<CompactIndex>::max()) {}
};

struct CompactDetailRecord {
    TextRef executed_at;
    std::uint32_t current_result_count;
    std::uint32_t original_result_count;
    CompactRange check_text;
    CompactRange results;
    CompactDetailRecord() : current_result_count(0), original_result_count(0) {}
};

struct CompactResultRecord {
    CompactRange properties;
    CompactRange geometry;
    TextRef signature_suffix;
    std::uint32_t ordinal;
    ResultKind kind;
    CompactResultRecord() : ordinal(0), kind(ResultKind::Polygon) {}
};

struct CompactPropertyRecord {
    CompactIndex tag_name;
    TextRef payload;
    CompactPropertyRecord() : tag_name(0) {}
    CompactPropertyRecord(CompactIndex id, const TextRef& value) : tag_name(id), payload(value) {}
};

static_assert(sizeof(CompactCheckRecord) <= 32U,
              "CompactCheckRecord must remain at most 32 bytes");
static_assert(sizeof(CompactResultRecord) <= 32U,
              "CompactResultRecord must remain at most 32 bytes");
static_assert(sizeof(CompactPropertyRecord) <= 12U,
              "CompactPropertyRecord must remain at most 12 bytes");

struct CompactMemoryUsage {
    std::size_t used_bytes;
    std::size_t capacity_bytes;
    CompactMemoryUsage() : used_bytes(0), capacity_bytes(0) {}
};

class CompactCheckDatabase;

class CompactPropertyView {
public:
    TextView id() const;
    TextView payload() const;
private:
    friend class CompactResultView;
    CompactPropertyView(const CompactCheckDatabase* database, CompactIndex index)
        : database_(database), index_(index) {}
    const CompactCheckDatabase* database_;
    CompactIndex index_;
};

class CompactResultView {
public:
    ResultKind kind() const;
    std::uint32_t ordinal() const;
    TextView signature_suffix() const;
    std::size_t property_count() const;
    CompactPropertyView property(std::size_t index) const;
    std::size_t vertex_count() const;
    // Geometry is returned by value and remains valid after later detail loads.
    Point vertex(std::size_t index) const;
    std::size_t edge_count() const;
    Edge edge(std::size_t index) const;
private:
    friend class CompactCheckView;
    CompactResultView(const CompactCheckDatabase* database, CompactIndex index)
        : database_(database), index_(index) {}
    const CompactCheckDatabase* database_;
    CompactIndex index_;
};

class CompactCheckView {
public:
    CheckId id() const { return id_; }
    TextView name() const;
    TextView comment() const;
    CheckOffset offset() const;
    std::uint32_t result_count() const;
    bool detail_loaded() const;
    TextView executed_at() const;
    std::uint32_t current_result_count() const;
    std::uint32_t original_result_count() const;
    std::size_t check_text_count() const;
    TextView check_text(std::size_t index) const;
    std::size_t detail_result_count() const;
    CompactResultView result(std::size_t index) const;
private:
    friend class CompactCheckDatabase;
    CompactCheckView(const CompactCheckDatabase* database, CheckId id)
        : database_(database), id_(id) {}
    const CompactCheckDatabase* database_;
    CheckId id_;
};

class CompactCheckDatabase {
public:
    CompactCheckDatabase();
    static CompactCheckDatabase from_index(const CheckIndexDatabase& index);

    TextView top_cell_name() const;
    double database_precision() const { return database_precision_; }
    std::size_t check_count() const { return checks_.size(); }
    std::size_t loaded_check_count() const { return loaded_check_count_; }
    CompactCheckView check(CheckId id) const;
    CheckId find_check_by_offset(CheckOffset offset) const;
    // Name and offset lookup are intentionally linear to avoid auxiliary maps.
    // Complexity is O(check_count); tag interning is linear in distinct tag names.
    std::vector<CheckId> find_checks(const std::string& name) const;
    void store_detail(CheckId id, const CheckDetail& detail);
    // Counts owned compact byte-pool/vector storage only. It excludes this object,
    // allocator bookkeeping, temporary parser values, and any mapped RDB file.
    CompactMemoryUsage memory_usage() const;

private:
    friend class CompactPropertyView;
    friend class CompactResultView;
    friend class CompactCheckView;

    TextView text(const TextRef& ref) const;
    CompactCheckRecord& check_record(CheckId id);
    const CompactCheckRecord& check_record(CheckId id) const;
    const CompactDetailRecord& detail_record(CheckId id) const;
    CompactIndex intern_tag_name(const std::string& name);
    TextRef append_text(const std::string& value);

    std::vector<char> text_pool_;
    TextRef top_cell_name_;
    double database_precision_;
    std::vector<CompactCheckRecord> checks_;
    std::vector<CompactDetailRecord> details_;
    std::vector<CompactResultRecord> results_;
    std::vector<CompactPropertyRecord> properties_;
    std::vector<Point> points_;
    std::vector<Edge> edges_;
    std::vector<TextRef> check_text_;
    std::vector<TextRef> tag_names_;
    std::size_t loaded_check_count_;
};

// Owns one mmap and indexes/parses details from that same open-file snapshot.
// Replacing the source path does not switch this object to the replacement file.
class IndexedRdbFile {
public:
    explicit IndexedRdbFile(
        const std::string& path,
        const FastCheckIndexOptions& options = FastCheckIndexOptions());

    CompactCheckDatabase& database() { return database_; }
    const CompactCheckDatabase& database() const { return database_; }
    std::size_t check_count() const { return database_.check_count(); }
    CompactCheckView check(CheckId id) const { return database_.check(id); }
    std::vector<CheckId> find_checks(const std::string& name) const {
        return database_.find_checks(name);
    }
    CompactCheckView load_check(CheckId id);
    void load_all();

private:
    IndexedRdbFile(const IndexedRdbFile&);
    IndexedRdbFile& operator=(const IndexedRdbFile&);
    void verify_file_unchanged() const;
    detail::MappedFile file_;
    detail::FileState file_state_;
    CompactCheckDatabase database_;
};

} // namespace rdb

#endif // RDB_COMPACT_DATABASE_HPP
