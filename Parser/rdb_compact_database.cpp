#include "rdb_compact_database.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace rdb {
namespace {

const std::size_t compact_max =
    static_cast<std::size_t>(std::numeric_limits<CompactIndex>::max());

void require_array_capacity(std::size_t current, std::size_t added, const char* what) {
    if (added > compact_max || current > compact_max - added) {
        throw std::length_error(std::string(what) + " exceeds 32-bit capacity");
    }
}

void require_text_capacity(std::size_t current, const std::string& value) {
    require_array_capacity(current, value.size(), "compact RDB text pool");
}

void add_text_requirement(std::size_t base,
                          std::size_t& additional,
                          const std::string& value) {
    require_array_capacity(base, additional, "compact RDB text pool");
    const std::size_t current = base + additional;
    require_array_capacity(current, value.size(), "compact RDB text pool");
    additional += value.size();
}

template <typename T>
void add_memory(CompactMemoryUsage& usage, const std::vector<T>& values) {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    const std::size_t used = values.size() > maximum / sizeof(T)
        ? maximum : values.size() * sizeof(T);
    const std::size_t capacity = values.capacity() > maximum / sizeof(T)
        ? maximum : values.capacity() * sizeof(T);
    usage.used_bytes = usage.used_bytes > maximum - used ? maximum : usage.used_bytes + used;
    usage.capacity_bytes = usage.capacity_bytes > maximum - capacity
        ? maximum : usage.capacity_bytes + capacity;
}

bool valid_kind(ResultKind kind) {
    return kind == ResultKind::Polygon || kind == ResultKind::EdgeCluster;
}

} // namespace

CompactCheckDatabase::CompactCheckDatabase()
    : database_precision_(0.0), loaded_check_count_(0) {}

TextRef CompactCheckDatabase::append_text(const std::string& value) {
    require_text_capacity(text_pool_.size(), value);
    const CompactIndex offset = static_cast<CompactIndex>(text_pool_.size());
    if (!value.empty()) text_pool_.insert(text_pool_.end(), value.begin(), value.end());
    return TextRef(offset, static_cast<CompactIndex>(value.size()));
}

TextView CompactCheckDatabase::text(const TextRef& ref) const {
    const std::size_t offset = ref.offset;
    const std::size_t size = ref.size;
    if (offset > text_pool_.size() || size > text_pool_.size() - offset) {
        throw std::logic_error("corrupt compact RDB text reference");
    }
    return TextView(size == 0 ? 0 : &text_pool_[offset], ref.size);
}

CompactCheckDatabase CompactCheckDatabase::from_index(const CheckIndexDatabase& index) {
    if (!std::isfinite(index.database_precision) || index.database_precision <= 0.0) {
        throw std::invalid_argument("compact RDB database precision must be a finite positive value");
    }
    if (index.top_cell_name.empty()) {
        throw std::invalid_argument("compact RDB top-cell name must not be empty");
    }
    if (index.checks.size() >= compact_max) {
        throw std::length_error("compact RDB check list exceeds 32-bit ID capacity");
    }

    std::size_t bytes = index.top_cell_name.size();
    if (bytes > compact_max) throw std::length_error("compact RDB text pool exceeds 32-bit capacity");
    for (std::size_t i = 0; i < index.checks.size(); ++i) {
        const CheckIndexEntry& entry = index.checks[i];
        if (entry.name.empty()) throw std::invalid_argument("compact RDB check name must not be empty");
        require_text_capacity(bytes, entry.name);
        bytes += entry.name.size();
        require_text_capacity(bytes, entry.comment);
        bytes += entry.comment.size();
    }

    CompactCheckDatabase database;
    database.database_precision_ = index.database_precision;
    database.text_pool_.reserve(bytes);
    database.checks_.reserve(index.checks.size());
    database.top_cell_name_ = database.append_text(index.top_cell_name);
    for (std::size_t i = 0; i < index.checks.size(); ++i) {
        const CheckIndexEntry& entry = index.checks[i];
        CompactCheckRecord record;
        record.offset = entry.offset;
        record.name = database.append_text(entry.name);
        record.comment = database.append_text(entry.comment);
        record.result_count = entry.geometry_count;
        database.checks_.push_back(record);
    }
    return database;
}

TextView CompactCheckDatabase::top_cell_name() const { return text(top_cell_name_); }

const CompactCheckRecord& CompactCheckDatabase::check_record(CheckId id) const {
    if (id == invalid_check_id() || static_cast<std::size_t>(id) >= checks_.size()) {
        throw std::out_of_range("compact RDB check ID is out of range");
    }
    return checks_[id];
}

CompactCheckRecord& CompactCheckDatabase::check_record(CheckId id) {
    if (id == invalid_check_id() || static_cast<std::size_t>(id) >= checks_.size()) {
        throw std::out_of_range("compact RDB check ID is out of range");
    }
    return checks_[id];
}

const CompactDetailRecord& CompactCheckDatabase::detail_record(CheckId id) const {
    const CompactCheckRecord& record = check_record(id);
    if (record.detail == std::numeric_limits<CompactIndex>::max()) {
        throw std::logic_error("compact RDB check detail has not been loaded");
    }
    if (static_cast<std::size_t>(record.detail) >= details_.size()) {
        throw std::logic_error("corrupt compact RDB detail reference");
    }
    return details_[record.detail];
}

CompactCheckView CompactCheckDatabase::check(CheckId id) const {
    (void)check_record(id);
    return CompactCheckView(this, id);
}

CheckId CompactCheckDatabase::find_check_by_offset(CheckOffset offset) const {
    for (std::size_t i = 0; i < checks_.size(); ++i) {
        if (checks_[i].offset == offset) return static_cast<CheckId>(i);
    }
    return invalid_check_id();
}

std::vector<CheckId> CompactCheckDatabase::find_checks(const std::string& name) const {
    std::vector<CheckId> found;
    for (std::size_t i = 0; i < checks_.size(); ++i) {
        const TextView value = text(checks_[i].name);
        if (value.size == name.size() &&
            (value.size == 0 || std::memcmp(value.data, name.data(), value.size) == 0)) {
            found.push_back(static_cast<CheckId>(i));
        }
    }
    return found;
}

CompactIndex CompactCheckDatabase::intern_tag_name(const std::string& name) {
    for (std::size_t i = 0; i < tag_names_.size(); ++i) {
        const TextView value = text(tag_names_[i]);
        if (value.size == name.size() &&
            (value.size == 0 || std::memcmp(value.data, name.data(), value.size) == 0)) {
            return static_cast<CompactIndex>(i);
        }
    }
    require_array_capacity(tag_names_.size(), 1U, "compact RDB tag-name table");
    const TextRef ref = append_text(name);
    tag_names_.push_back(ref);
    return static_cast<CompactIndex>(tag_names_.size() - 1U);
}

void CompactCheckDatabase::store_detail(CheckId id, const CheckDetail& detail) {
    CompactCheckRecord& check = check_record(id);
    if (check.detail != std::numeric_limits<CompactIndex>::max()) {
        throw std::logic_error("compact RDB check detail is already loaded");
    }
    const TextView indexed_name = text(check.name);
    if (detail.name.size() != indexed_name.size ||
        (indexed_name.size != 0 &&
         std::memcmp(detail.name.data(), indexed_name.data, indexed_name.size) != 0)) {
        throw std::invalid_argument("detail check name does not match compact index");
    }
    if (detail.offset != check.offset) {
        throw std::invalid_argument("detail check offset does not match compact index");
    }
    if (detail.current_result_count != check.result_count ||
        detail.results.size() != detail.current_result_count) {
        throw std::invalid_argument("detail result count does not match compact index/header");
    }
    if (detail.current_result_count > detail.original_result_count) {
        throw std::invalid_argument("detail current result count exceeds original result count");
    }

    require_array_capacity(details_.size(), 1U, "compact RDB detail list");
    require_array_capacity(results_.size(), detail.results.size(), "compact RDB result list");
    require_array_capacity(check_text_.size(), detail.check_text.size(), "compact RDB check-text list");
    std::size_t additional_text = 0;
    add_text_requirement(text_pool_.size(), additional_text, detail.executed_at);
    for (std::size_t i = 0; i < detail.check_text.size(); ++i) {
        add_text_requirement(text_pool_.size(), additional_text, detail.check_text[i]);
    }

    std::size_t property_add = 0;
    std::size_t point_add = 0;
    std::size_t edge_add = 0;
    std::vector<const std::string*> new_tag_names;
    for (std::size_t i = 0; i < detail.results.size(); ++i) {
        const DetailResult& result = detail.results[i];
        if (!valid_kind(result.kind)) throw std::invalid_argument("detail contains an invalid result kind");
        if ((result.kind == ResultKind::Polygon && !result.edges.empty()) ||
            (result.kind == ResultKind::EdgeCluster && !result.vertices.empty())) {
            throw std::invalid_argument("detail result geometry does not match its kind");
        }
        require_array_capacity(property_add, result.properties.size(), "detail property count");
        property_add += result.properties.size();
        require_array_capacity(point_add, result.vertices.size(), "detail point count");
        point_add += result.vertices.size();
        require_array_capacity(edge_add, result.edges.size(), "detail edge count");
        edge_add += result.edges.size();
        add_text_requirement(text_pool_.size(), additional_text, result.signature_suffix);
        for (std::size_t j = 0; j < result.properties.size(); ++j) {
            const DetailTag& property = result.properties[j];
            if (property.id.empty()) throw std::invalid_argument("detail property tag ID must not be empty");
            add_text_requirement(text_pool_.size(), additional_text, property.payload);
            bool known = false;
            for (std::size_t k = 0; k < tag_names_.size() && !known; ++k) {
                const TextView tag = text(tag_names_[k]);
                known = tag.size == property.id.size() &&
                    (tag.size == 0 || std::memcmp(tag.data, property.id.data(), tag.size) == 0);
            }
            for (std::size_t k = 0; k < new_tag_names.size() && !known; ++k) {
                known = *new_tag_names[k] == property.id;
            }
            if (!known) {
                add_text_requirement(text_pool_.size(), additional_text, property.id);
                new_tag_names.push_back(&property.id);
            }
        }
    }
    require_array_capacity(properties_.size(), property_add, "compact RDB property list");
    require_array_capacity(points_.size(), point_add, "compact RDB point list");
    require_array_capacity(edges_.size(), edge_add, "compact RDB edge list");
    require_array_capacity(tag_names_.size(), new_tag_names.size(), "compact RDB tag-name table");
    require_array_capacity(text_pool_.size(), additional_text, "compact RDB text pool");

    // Allocate every known final capacity first. A reserve failure leaves all logical sizes intact.
    text_pool_.reserve(text_pool_.size() + additional_text);
    details_.reserve(details_.size() + 1U);
    results_.reserve(results_.size() + detail.results.size());
    properties_.reserve(properties_.size() + property_add);
    points_.reserve(points_.size() + point_add);
    edges_.reserve(edges_.size() + edge_add);
    check_text_.reserve(check_text_.size() + detail.check_text.size());
    tag_names_.reserve(tag_names_.size() + new_tag_names.size());

    const std::size_t text_size = text_pool_.size();
    const std::size_t detail_size = details_.size();
    const std::size_t result_size = results_.size();
    const std::size_t property_size = properties_.size();
    const std::size_t point_size = points_.size();
    const std::size_t edge_size = edges_.size();
    const std::size_t check_text_size = check_text_.size();
    const std::size_t tag_name_size = tag_names_.size();

    try {
        CompactDetailRecord stored_detail;
        stored_detail.executed_at = append_text(detail.executed_at);
        stored_detail.current_result_count = detail.current_result_count;
        stored_detail.original_result_count = detail.original_result_count;
        stored_detail.check_text = CompactRange(
            static_cast<CompactIndex>(check_text_.size()),
            static_cast<CompactIndex>(detail.check_text.size()));
        for (std::size_t i = 0; i < detail.check_text.size(); ++i) {
            check_text_.push_back(append_text(detail.check_text[i]));
        }
        stored_detail.results = CompactRange(
            static_cast<CompactIndex>(results_.size()),
            static_cast<CompactIndex>(detail.results.size()));

        for (std::size_t i = 0; i < detail.results.size(); ++i) {
            const DetailResult& source = detail.results[i];
            CompactResultRecord result;
            result.kind = source.kind;
            result.ordinal = source.ordinal;
            result.signature_suffix = append_text(source.signature_suffix);
            result.properties = CompactRange(
                static_cast<CompactIndex>(properties_.size()),
                static_cast<CompactIndex>(source.properties.size()));
            for (std::size_t j = 0; j < source.properties.size(); ++j) {
                const DetailTag& property = source.properties[j];
                const CompactIndex tag = intern_tag_name(property.id);
                properties_.push_back(CompactPropertyRecord(tag, append_text(property.payload)));
            }
            if (source.kind == ResultKind::Polygon) {
                result.geometry = CompactRange(static_cast<CompactIndex>(points_.size()),
                                               static_cast<CompactIndex>(source.vertices.size()));
                points_.insert(points_.end(), source.vertices.begin(), source.vertices.end());
            } else {
                result.geometry = CompactRange(static_cast<CompactIndex>(edges_.size()),
                                               static_cast<CompactIndex>(source.edges.size()));
                edges_.insert(edges_.end(), source.edges.begin(), source.edges.end());
            }
            results_.push_back(result);
        }
        details_.push_back(stored_detail);
        check.detail = static_cast<CompactIndex>(details_.size() - 1U);
        ++loaded_check_count_;
    } catch (...) {
        text_pool_.resize(text_size);
        details_.resize(detail_size);
        results_.resize(result_size);
        properties_.resize(property_size);
        points_.resize(point_size);
        edges_.resize(edge_size);
        check_text_.resize(check_text_size);
        tag_names_.resize(tag_name_size);
        throw;
    }
}

CompactMemoryUsage CompactCheckDatabase::memory_usage() const {
    CompactMemoryUsage usage;
    add_memory(usage, text_pool_);
    add_memory(usage, checks_);
    add_memory(usage, details_);
    add_memory(usage, results_);
    add_memory(usage, properties_);
    add_memory(usage, points_);
    add_memory(usage, edges_);
    add_memory(usage, check_text_);
    add_memory(usage, tag_names_);
    return usage;
}

TextView CompactPropertyView::id() const {
    if (static_cast<std::size_t>(index_) >= database_->properties_.size())
        throw std::out_of_range("compact RDB property index is out of range");
    const CompactPropertyRecord& property = database_->properties_[index_];
    if (static_cast<std::size_t>(property.tag_name) >= database_->tag_names_.size())
        throw std::logic_error("corrupt compact RDB tag-name reference");
    return database_->text(database_->tag_names_[property.tag_name]);
}

TextView CompactPropertyView::payload() const {
    if (static_cast<std::size_t>(index_) >= database_->properties_.size())
        throw std::out_of_range("compact RDB property index is out of range");
    return database_->text(database_->properties_[index_].payload);
}

ResultKind CompactResultView::kind() const { return database_->results_.at(index_).kind; }
std::uint32_t CompactResultView::ordinal() const { return database_->results_.at(index_).ordinal; }
TextView CompactResultView::signature_suffix() const {
    return database_->text(database_->results_.at(index_).signature_suffix);
}
std::size_t CompactResultView::property_count() const { return database_->results_.at(index_).properties.count; }
CompactPropertyView CompactResultView::property(std::size_t index) const {
    const CompactResultRecord& result = database_->results_.at(index_);
    if (index >= result.properties.count) throw std::out_of_range("compact RDB property index is out of range");
    return CompactPropertyView(database_, result.properties.begin + static_cast<CompactIndex>(index));
}
std::size_t CompactResultView::vertex_count() const {
    const CompactResultRecord& result = database_->results_.at(index_);
    return result.kind == ResultKind::Polygon ? result.geometry.count : 0U;
}
Point CompactResultView::vertex(std::size_t index) const {
    const CompactResultRecord& result = database_->results_.at(index_);
    if (result.kind != ResultKind::Polygon || index >= result.geometry.count)
        throw std::out_of_range("compact RDB vertex index is out of range");
    return database_->points_.at(result.geometry.begin + static_cast<CompactIndex>(index));
}
std::size_t CompactResultView::edge_count() const {
    const CompactResultRecord& result = database_->results_.at(index_);
    return result.kind == ResultKind::EdgeCluster ? result.geometry.count : 0U;
}
Edge CompactResultView::edge(std::size_t index) const {
    const CompactResultRecord& result = database_->results_.at(index_);
    if (result.kind != ResultKind::EdgeCluster || index >= result.geometry.count)
        throw std::out_of_range("compact RDB edge index is out of range");
    return database_->edges_.at(result.geometry.begin + static_cast<CompactIndex>(index));
}

TextView CompactCheckView::name() const { return database_->text(database_->check_record(id_).name); }
TextView CompactCheckView::comment() const { return database_->text(database_->check_record(id_).comment); }
CheckOffset CompactCheckView::offset() const { return database_->check_record(id_).offset; }
std::uint32_t CompactCheckView::result_count() const { return database_->check_record(id_).result_count; }
bool CompactCheckView::detail_loaded() const {
    return database_->check_record(id_).detail != std::numeric_limits<CompactIndex>::max();
}
TextView CompactCheckView::executed_at() const { return database_->text(database_->detail_record(id_).executed_at); }
std::uint32_t CompactCheckView::current_result_count() const {
    return database_->detail_record(id_).current_result_count;
}
std::uint32_t CompactCheckView::original_result_count() const {
    return database_->detail_record(id_).original_result_count;
}
std::size_t CompactCheckView::check_text_count() const { return database_->detail_record(id_).check_text.count; }
TextView CompactCheckView::check_text(std::size_t index) const {
    const CompactDetailRecord& detail = database_->detail_record(id_);
    if (index >= detail.check_text.count) throw std::out_of_range("compact RDB check-text index is out of range");
    return database_->text(database_->check_text_.at(
        detail.check_text.begin + static_cast<CompactIndex>(index)));
}
std::size_t CompactCheckView::detail_result_count() const { return database_->detail_record(id_).results.count; }
CompactResultView CompactCheckView::result(std::size_t index) const {
    const CompactDetailRecord& detail = database_->detail_record(id_);
    if (index >= detail.results.count) throw std::out_of_range("compact RDB result index is out of range");
    return CompactResultView(database_, detail.results.begin + static_cast<CompactIndex>(index));
}

IndexedRdbFile::IndexedRdbFile(const std::string& path, const FastCheckIndexOptions& options)
    : file_(path),
      file_state_(file_.state()),
      database_(CompactCheckDatabase::from_index(
          FastCheckIndexParser().parse_database(file_.descriptor(), options))) {
    verify_file_unchanged();
}

void IndexedRdbFile::verify_file_unchanged() const {
    if (!detail::same_file_snapshot(file_state_, file_.state())) {
        throw ScanError(0, "RDB file changed after indexed snapshot was opened");
    }
}

CompactCheckView IndexedRdbFile::load_check(CheckId id) {
    verify_file_unchanged();
    CompactCheckView selected = database_.check(id);
    if (!selected.detail_loaded()) {
        const CheckDetail detail = detail::parse_check_detail(file_, selected.offset());
        verify_file_unchanged();
        database_.store_detail(id, detail);
    }
    verify_file_unchanged();
    return database_.check(id);
}

void IndexedRdbFile::load_all() {
    for (std::size_t i = 0; i < database_.check_count(); ++i) {
        (void)load_check(static_cast<CheckId>(i));
    }
}

} // namespace rdb
