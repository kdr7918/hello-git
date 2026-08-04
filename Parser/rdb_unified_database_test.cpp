#include "rdb_indexed_file.hpp"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

void check(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "check failed: " << expression << '\n';
        std::exit(EXIT_FAILURE);
    }
}

#define RDB_CHECK(expression) check((expression), #expression)

std::string text(const rdb::Database& database, rdb::StringId id) {
    return database.strings.get(id).str();
}

class TemporaryRdb {
public:
    explicit TemporaryRdb(const std::string& contents) {
        char pattern[] = "/tmp/rdb-unified-test-XXXXXX";
        const int fd = ::mkstemp(pattern);
        if (fd < 0) throw std::runtime_error("mkstemp failed");
        path_ = pattern;
        std::size_t written = 0;
        while (written < contents.size()) {
            const ssize_t count = ::write(fd, contents.data() + written, contents.size() - written);
            if (count <= 0) {
                ::close(fd);
                ::unlink(path_.c_str());
                throw std::runtime_error("temporary RDB write failed");
            }
            written += static_cast<std::size_t>(count);
        }
        if (::close(fd) != 0) {
            ::unlink(path_.c_str());
            throw std::runtime_error("temporary RDB close failed");
        }
    }

    ~TemporaryRdb() { ::unlink(path_.c_str()); }
    const std::string& path() const { return path_; }

private:
    TemporaryRdb(const TemporaryRdb&);
    TemporaryRdb& operator=(const TemporaryRdb&);
    std::string path_;
};

} // namespace

int main() {
    static_assert(sizeof(rdb::CheckId) == 4U, "CheckId must remain 32 bit");
    static_assert(sizeof(rdb::CheckOffset) == 8U, "CheckOffset must remain 64 bit");
    static_assert(
        std::is_same<decltype(std::declval<const rdb::IndexedRdbFile&>().database()),
                     const rdb::Database&>::value,
        "IndexedRdbFile must expose the canonical Database as read-only state");

    rdb::StringTable checkpoint_table;
    checkpoint_table.add("kept", 4U);
    const rdb::StringTable::Checkpoint checkpoint = checkpoint_table.checkpoint();
    checkpoint_table.add("discarded", 9U);
    checkpoint_table.rollback(checkpoint);
    RDB_CHECK(checkpoint_table.size() == 1U);
    RDB_CHECK(checkpoint_table.get(0U).str() == "kept");
    const rdb::StringTable::Checkpoint foreign_checkpoint = checkpoint_table.checkpoint();
    rdb::StringTable other_table;
    other_table.add("other", 5U);
    bool foreign_checkpoint_rejected = false;
    try {
        other_table.rollback(foreign_checkpoint);
    } catch (const std::out_of_range&) {
        foreign_checkpoint_rejected = true;
    }
    RDB_CHECK(foreign_checkpoint_rejected);
    checkpoint_table.clear();
    bool stale_checkpoint_rejected = false;
    try {
        checkpoint_table.rollback(foreign_checkpoint);
    } catch (const std::out_of_range&) {
        stale_checkpoint_rejected = true;
    }
    RDB_CHECK(stale_checkpoint_rejected);

    const std::string path = std::string(RDB_SAMPLE_DIR) + "/standard_sample.rdb";
    rdb::IndexedRdbFile file(path);
    const rdb::Database& database = file.database();

    RDB_CHECK(text(database, database.top_cell_name) == "TOP_CHIP");
    RDB_CHECK(database.database_precision == 1000.0);
    RDB_CHECK(database.check_count() == 3U);
    RDB_CHECK(database.loaded_check_count() == 0U);

    const rdb::RuleCheck& indexed = database.check(0U);
    RDB_CHECK(text(database, indexed.name) == "M1.SPACING.1");
    RDB_CHECK(indexed.offset == 14U);
    RDB_CHECK(indexed.current_result_count == 2U);
    RDB_CHECK(indexed.original_result_count == 2U);
    RDB_CHECK(indexed.declared_check_text_count == 3U);
    RDB_CHECK(text(database, indexed.comment) ==
              "Rule File Pathname: ./demo.svrf\n"
              "Rule File Title: Example DRC deck\n"
              "M1 spacing must be at least 0.14 um.");
    RDB_CHECK(!indexed.detail_loaded);

    const std::vector<rdb::CheckId> found = database.find_checks("M1.SPACING.1");
    RDB_CHECK(found.size() == 1U && found[0] == 0U);
    RDB_CHECK(database.find_check_by_offset(indexed.offset) == 0U);

    file.load_check(0U);
    const rdb::RuleCheck& loaded = database.check(0U);
    RDB_CHECK(loaded.detail_loaded);
    RDB_CHECK(database.loaded_check_count() == 1U);
    RDB_CHECK(text(database, loaded.executed_at) == "Jul 21 10:32:45 2026");
    RDB_CHECK(loaded.check_text.count == 3U);
    RDB_CHECK(loaded.results.count == 2U);

    const rdb::Result& polygon = database.results[loaded.results.begin];
    RDB_CHECK(polygon.kind == rdb::ResultKind::Polygon);
    RDB_CHECK(polygon.properties.count == 5U);
    RDB_CHECK(polygon.geometry.count == 4U);
    const rdb::TaggedValue& first_property =
        database.tagged_values[polygon.properties.begin];
    RDB_CHECK(text(database, first_property.id) == "PP");
    RDB_CHECK(text(database, first_property.payload) == "M1 spacing marker");
    RDB_CHECK(database.vertices[polygon.geometry.begin].x == 10000);
    RDB_CHECK(database.vertices[polygon.geometry.begin].y == 20000);

    file.load_check(0U);
    RDB_CHECK(database.loaded_check_count() == 1U);

    file.load_check(2U);
    const rdb::RuleCheck& empty = database.check(2U);
    RDB_CHECK(empty.detail_loaded);
    RDB_CHECK(empty.current_result_count == 0U);
    RDB_CHECK(empty.results.count == 0U);
    RDB_CHECK(database.loaded_check_count() == 2U);

    RDB_CHECK(database.find_checks("DOES.NOT.EXIST").empty());
    RDB_CHECK(database.find_check_by_offset(0U) == rdb::invalid_check_id());
    bool invalid_id_rejected = false;
    try {
        (void)file.load_check(rdb::invalid_check_id());
    } catch (const std::out_of_range&) {
        invalid_id_rejected = true;
    }
    RDB_CHECK(invalid_id_rejected);
    RDB_CHECK(database.loaded_check_count() == 2U);

    rdb::IndexedRdbFile all_file(path);
    all_file.load_all();
    RDB_CHECK(all_file.database().loaded_check_count() == 3U);
    RDB_CHECK(all_file.database().check(0U).results.count == 2U);
    RDB_CHECK(all_file.database().check(1U).results.count == 1U);
    RDB_CHECK(all_file.database().check(2U).results.count == 0U);
    all_file.load_all();
    RDB_CHECK(all_file.database().loaded_check_count() == 3U);

    const TemporaryRdb decimal_header("TOP_DECIMAL 1.25e-3\n");
    const rdb::IndexedRdbFile indexed_decimal(decimal_header.path());
    RDB_CHECK(indexed_decimal.database().database_precision == 1.25e-3);
    RDB_CHECK(indexed_decimal.database().check_count() == 0U);

    const TemporaryRdb pre_signature_property(
        "TOP 1000\nPRE.SIGNATURE\n1 1 0 Jul 21 10:35:00 2026\n"
        "PB before-signature\np 1 1\n3 4\nPA after-geometry\n");
    rdb::IndexedRdbFile indexed_pre_signature(pre_signature_property.path());
    indexed_pre_signature.load_all();
    const rdb::Database& pre_database = indexed_pre_signature.database();
    RDB_CHECK(pre_database.results[0].properties.count == 2U);
    const rdb::TaggedValue& pre_property =
        pre_database.tagged_values[pre_database.results[0].properties.begin];
    RDB_CHECK(text(pre_database, pre_property.id) == "PB");
    RDB_CHECK(text(pre_database, pre_property.payload) == "before-signature");

    const TemporaryRdb invalid_counts(
        "TOP 1000\nINVALID.COUNTS\n2 1 0 Jul 21 10:35:00 2026\n"
        "p 1 1\n0 0\np 2 1\n1 1\n");
    bool indexed_counts_rejected = false;
    try {
        (void)rdb::IndexedRdbFile(invalid_counts.path());
    } catch (const rdb::ScanError&) {
        indexed_counts_rejected = true;
    }
    RDB_CHECK(indexed_counts_rejected);

    const TemporaryRdb overflowing_counts(
        "TOP 1000\nOVERFLOW.COUNTS\n4294967296 4294967296 0 Jul 21 10:35:00 2026\n");
    bool overflowing_counts_rejected = false;
    try {
        (void)rdb::IndexedRdbFile(overflowing_counts.path());
    } catch (const rdb::ScanError&) {
        overflowing_counts_rejected = true;
    }
    RDB_CHECK(overflowing_counts_rejected);

    const TemporaryRdb undeclared_zero_result(
        "TOP 1000\nZERO.WITH.RESULT\n0 0 0 Jul 21 10:35:00 2026\n"
        "p 1 1\n0 0\n");
    rdb::IndexedRdbFile zero_result_file(undeclared_zero_result.path());
    bool undeclared_result_rejected = false;
    try {
        zero_result_file.load_check(0U);
    } catch (const rdb::ScanError&) {
        undeclared_result_rejected = true;
    }
    RDB_CHECK(undeclared_result_rejected);
    RDB_CHECK(!zero_result_file.database().check(0U).detail_loaded);

    const TemporaryRdb truncated_detail(
        "TOP 1000\nTRUNCATED.DETAIL\n2 2 0 Jul 21 10:35:00 2026\n"
        "p 1 1\n0 0\n");
    rdb::IndexedRdbFile transactional_file(truncated_detail.path());
    RDB_CHECK(transactional_file.database().results.empty());
    bool truncated_rejected = false;
    try {
        transactional_file.load_check(0U);
    } catch (const std::runtime_error&) {
        truncated_rejected = true;
    }
    RDB_CHECK(truncated_rejected);
    RDB_CHECK(!transactional_file.database().check(0U).detail_loaded);
    RDB_CHECK(transactional_file.database().loaded_check_count() == 0U);
    RDB_CHECK(transactional_file.database().results.empty());
    RDB_CHECK(transactional_file.database().tagged_values.empty());
    RDB_CHECK(transactional_file.database().vertices.empty());
    RDB_CHECK(transactional_file.database().edges.empty());

    const TemporaryRdb duplicate_file(
        "TOP 1000\nDUP.CHECK\n2 2 0 Jul 21 10:35:00 2026\n"
        "SHARED first-value\np 1 1\n0 0\ne 2 1\n1 2 3 4\n"
        "DUP.CHECK\n1 1 0 Jul 21 10:36:00 2026\n"
        "SHARED second-value\np 1 1\n10 20\n");
    rdb::IndexedRdbFile duplicates_file(duplicate_file.path());
    const std::vector<rdb::CheckId> duplicates =
        duplicates_file.database().find_checks("DUP.CHECK");
    RDB_CHECK(duplicates.size() == 2U && duplicates[0] == 0U && duplicates[1] == 1U);
    duplicates_file.load_check(0U);
    const rdb::RuleCheck& first_duplicate = duplicates_file.database().check(0U);
    const rdb::Result& first_polygon =
        duplicates_file.database().results[first_duplicate.results.begin];
    const rdb::Result& first_edges =
        duplicates_file.database().results[first_duplicate.results.begin + 1U];
    const rdb::Point copied_point =
        duplicates_file.database().vertices[first_polygon.geometry.begin];
    const rdb::Edge copied_edge =
        duplicates_file.database().edges[first_edges.geometry.begin];
    const rdb::Range first_result_range = first_duplicate.results;
    const rdb::StringId first_tag =
        duplicates_file.database().tagged_values[first_polygon.properties.begin].id;
    duplicates_file.load_check(1U);
    RDB_CHECK(duplicates_file.database().loaded_check_count() == 2U);
    RDB_CHECK(duplicates_file.database().check(0U).results.begin == first_result_range.begin);
    RDB_CHECK(duplicates_file.database().check(0U).results.count == first_result_range.count);
    RDB_CHECK(copied_point.x == 0 && copied_point.y == 0);
    RDB_CHECK(copied_edge.first.x == 1 && copied_edge.first.y == 2);
    RDB_CHECK(copied_edge.second.x == 3 && copied_edge.second.y == 4);
    const rdb::RuleCheck& second_duplicate = duplicates_file.database().check(1U);
    const rdb::Result& second_result =
        duplicates_file.database().results[second_duplicate.results.begin];
    const rdb::TaggedValue& second_property =
        duplicates_file.database().tagged_values[second_result.properties.begin];
    RDB_CHECK(second_property.id == first_tag);
    RDB_CHECK(text(duplicates_file.database(), second_property.id) == "SHARED");
    RDB_CHECK(text(duplicates_file.database(), second_property.payload) == "second-value");

    const TemporaryRdb snapshot_file(
        "TOP 1000\nSNAPSHOT.CHECK\n1 1 0 Jul 21 10:35:00 2026\np 1 1\n7 8\n");
    rdb::IndexedRdbFile snapshot(snapshot_file.path());
    const TemporaryRdb replacement_file(
        "TOP 1000\nREPLACEMENT.CHECK\n1 1 0 Jul 21 10:35:00 2026\np 1 1\n9 9\n");
    RDB_CHECK(::rename(replacement_file.path().c_str(), snapshot_file.path().c_str()) == 0);
    snapshot.load_check(0U);
    const rdb::RuleCheck& snapshot_check = snapshot.database().check(0U);
    const rdb::Result& snapshot_result =
        snapshot.database().results[snapshot_check.results.begin];
    RDB_CHECK(text(snapshot.database(), snapshot_check.name) == "SNAPSHOT.CHECK");
    RDB_CHECK(snapshot.database().vertices[snapshot_result.geometry.begin].x == 7);
    RDB_CHECK(snapshot.database().vertices[snapshot_result.geometry.begin].y == 8);

    const TemporaryRdb modified_file(
        "TOP 1000\nMODIFIED.CHECK\n1 1 0 Jul 21 10:35:00 2026\np 1 1\n1 2\n");
    rdb::IndexedRdbFile modified(modified_file.path());
    const int modified_fd = ::open(modified_file.path().c_str(), O_RDWR | O_CLOEXEC);
    RDB_CHECK(modified_fd >= 0);
    RDB_CHECK(::pwrite(modified_fd, "X", 1, 0) == 1);
    RDB_CHECK(::fsync(modified_fd) == 0);
    RDB_CHECK(::close(modified_fd) == 0);
    bool modification_rejected = false;
    try {
        (void)modified.load_check(0U);
    } catch (const rdb::ScanError&) {
        modification_rejected = true;
    }
    RDB_CHECK(modification_rejected);
    RDB_CHECK(!modified.database().check(0U).detail_loaded);
    RDB_CHECK(modified.database().loaded_check_count() == 0U);

    std::cout << "rdb-unified-database-test: OK\n";
}
