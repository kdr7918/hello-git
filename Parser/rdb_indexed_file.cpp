#include "rdb_indexed_file.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace rdb {
namespace {

const std::size_t model_max =
    static_cast<std::size_t>(std::numeric_limits<Index>::max());

void require_capacity(std::size_t current, std::size_t added, const char* what) {
    if (added > model_max || current > model_max - added) {
        throw std::length_error(std::string(what) + " exceeds 32-bit Database capacity");
    }
}

bool same_text(const StringTable& strings, StringId id, const std::string& value) {
    const StringRef stored = strings.get(id);
    return stored.size == value.size() &&
           (stored.size == 0 || std::memcmp(stored.data, value.data(), stored.size) == 0);
}

bool valid_kind(ResultKind kind) {
    return kind == ResultKind::Polygon || kind == ResultKind::EdgeCluster;
}

Database database_from_index(const CheckIndexDatabase& index) {
    if (index.top_cell_name.empty()) {
        throw std::invalid_argument("RDB top-cell name must not be empty");
    }
    if (!std::isfinite(index.database_precision) || index.database_precision <= 0.0) {
        throw std::invalid_argument("RDB database precision must be finite and positive");
    }
    if (index.checks.size() >= model_max) {
        throw std::length_error("RDB check list exceeds 32-bit Database capacity");
    }

    std::size_t string_bytes = index.top_cell_name.size();
    require_capacity(0U, string_bytes, "RDB string bytes");
    for (std::size_t i = 0; i < index.checks.size(); ++i) {
        const CheckIndexEntry& entry = index.checks[i];
        if (entry.name.empty()) throw std::invalid_argument("RDB check name must not be empty");
        if (entry.geometry_count > entry.original_result_count) {
            throw std::invalid_argument("RDB current result count exceeds original result count");
        }
        require_capacity(string_bytes, entry.name.size(), "RDB string bytes");
        string_bytes += entry.name.size();
        require_capacity(string_bytes, entry.comment.size(), "RDB string bytes");
        string_bytes += entry.comment.size();
    }

    Database database;
    database.database_precision = index.database_precision;
    database.strings.reserve(1U + index.checks.size() * 2U, string_bytes);
    database.rule_checks.reserve(index.checks.size());
    database.top_cell_name = database.strings.add(index.top_cell_name);

    for (std::size_t i = 0; i < index.checks.size(); ++i) {
        const CheckIndexEntry& entry = index.checks[i];
        RuleCheck check;
        check.offset = entry.offset;
        check.name = database.strings.add(entry.name);
        check.comment = database.strings.add(entry.comment);
        check.current_result_count = entry.geometry_count;
        check.original_result_count = entry.original_result_count;
        check.declared_check_text_count = entry.check_text_line_count;
        database.rule_checks.push_back(check);
    }
    return database;
}

StringId intern_tag_name(Database& database,
                         std::vector<StringId>& tag_names,
                         const std::string& name) {
    for (std::size_t i = 0; i < tag_names.size(); ++i) {
        if (same_text(database.strings, tag_names[i], name)) {
            return tag_names[i];
        }
    }
    const StringId id = database.strings.add(name);
    tag_names.push_back(id);
    return id;
}

void store_detail(Database& database,
                  std::vector<StringId>& tag_names,
                  CheckId id,
                  const CheckDetail& detail) {
    RuleCheck& check = database.check(id);
    if (check.detail_loaded) return;
    if (!same_text(database.strings, check.name, detail.name)) {
        throw std::invalid_argument("detail check name does not match Database index");
    }
    if (detail.offset != check.offset) {
        throw std::invalid_argument("detail check offset does not match Database index");
    }
    if (detail.current_result_count != check.current_result_count ||
        detail.original_result_count != check.original_result_count ||
        detail.results.size() != detail.current_result_count) {
        throw std::invalid_argument("detail result counts do not match Database index/header");
    }
    if (detail.check_text.size() != check.declared_check_text_count) {
        throw std::invalid_argument("detail check-text count does not match Database index/header");
    }
    if (detail.current_result_count > detail.original_result_count) {
        throw std::invalid_argument("detail current result count exceeds original result count");
    }

    require_capacity(database.results.size(), detail.results.size(), "RDB result list");
    require_capacity(database.check_text_lines.size(), detail.check_text.size(), "RDB check-text list");

    std::size_t property_add = 0;
    std::size_t vertex_add = 0;
    std::size_t edge_add = 0;
    std::size_t additional_strings = 1U + detail.check_text.size();
    std::size_t additional_bytes = detail.executed_at.size();
    std::vector<const std::string*> new_tag_names;

    for (std::size_t i = 0; i < detail.check_text.size(); ++i) {
        require_capacity(additional_bytes, detail.check_text[i].size(), "RDB string bytes");
        additional_bytes += detail.check_text[i].size();
    }

    for (std::size_t i = 0; i < detail.results.size(); ++i) {
        const DetailResult& result = detail.results[i];
        if (!valid_kind(result.kind)) throw std::invalid_argument("detail has invalid result kind");
        if ((result.kind == ResultKind::Polygon && !result.edges.empty()) ||
            (result.kind == ResultKind::EdgeCluster && !result.vertices.empty())) {
            throw std::invalid_argument("detail geometry does not match result kind");
        }
        require_capacity(property_add, result.properties.size(), "detail property count");
        property_add += result.properties.size();
        require_capacity(vertex_add, result.vertices.size(), "detail vertex count");
        vertex_add += result.vertices.size();
        require_capacity(edge_add, result.edges.size(), "detail edge count");
        edge_add += result.edges.size();

        if (!result.signature_suffix.empty()) {
            ++additional_strings;
            require_capacity(additional_bytes, result.signature_suffix.size(), "RDB string bytes");
            additional_bytes += result.signature_suffix.size();
        }
        for (std::size_t j = 0; j < result.properties.size(); ++j) {
            const DetailTag& property = result.properties[j];
            if (property.id.empty()) throw std::invalid_argument("detail property ID is empty");
            if (!property.payload.empty()) {
                ++additional_strings;
                require_capacity(additional_bytes, property.payload.size(), "RDB string bytes");
                additional_bytes += property.payload.size();
            }
            bool known = false;
            for (std::size_t k = 0; k < tag_names.size() && !known; ++k) {
                known = same_text(database.strings, tag_names[k], property.id);
            }
            for (std::size_t k = 0; k < new_tag_names.size() && !known; ++k) {
                known = *new_tag_names[k] == property.id;
            }
            if (!known) {
                ++additional_strings;
                require_capacity(additional_bytes, property.id.size(), "RDB string bytes");
                additional_bytes += property.id.size();
                new_tag_names.push_back(&property.id);
            }
        }
    }

    require_capacity(database.tagged_values.size(), property_add, "RDB property list");
    require_capacity(database.vertices.size(), vertex_add, "RDB vertex list");
    require_capacity(database.edges.size(), edge_add, "RDB edge list");
    require_capacity(tag_names.size(), new_tag_names.size(), "RDB tag-name list");
    require_capacity(database.strings.size(), additional_strings, "RDB string records");
    require_capacity(database.strings.byte_size(), additional_bytes, "RDB string bytes");

    database.strings.reserve(database.strings.size() + additional_strings,
                             database.strings.byte_size() + additional_bytes);
    database.results.reserve(database.results.size() + detail.results.size());
    database.tagged_values.reserve(database.tagged_values.size() + property_add);
    database.vertices.reserve(database.vertices.size() + vertex_add);
    database.edges.reserve(database.edges.size() + edge_add);
    database.check_text_lines.reserve(database.check_text_lines.size() + detail.check_text.size());
    tag_names.reserve(tag_names.size() + new_tag_names.size());

    const StringTable::Checkpoint string_checkpoint = database.strings.checkpoint();
    const std::size_t result_count = database.results.size();
    const std::size_t property_count = database.tagged_values.size();
    const std::size_t vertex_count = database.vertices.size();
    const std::size_t edge_count = database.edges.size();
    const std::size_t text_count = database.check_text_lines.size();
    const std::size_t tag_count = tag_names.size();
    const std::size_t loaded_count = database.loaded_rule_check_count;
    const RuleCheck original_check = check;

    try {
        const StringId executed_at = database.strings.add(detail.executed_at);
        const Range check_text(
            static_cast<Index>(database.check_text_lines.size()),
            static_cast<Index>(detail.check_text.size()));
        for (std::size_t i = 0; i < detail.check_text.size(); ++i) {
            database.check_text_lines.push_back(database.strings.add(detail.check_text[i]));
        }

        const Range results(
            static_cast<Index>(database.results.size()),
            static_cast<Index>(detail.results.size()));
        for (std::size_t i = 0; i < detail.results.size(); ++i) {
            const DetailResult& source = detail.results[i];
            Result result;
            result.kind = source.kind;
            result.ordinal = source.ordinal;
            if (!source.signature_suffix.empty()) {
                result.signature_suffix = database.strings.add(source.signature_suffix);
            }
            result.properties = Range(
                static_cast<Index>(database.tagged_values.size()),
                static_cast<Index>(source.properties.size()));
            for (std::size_t j = 0; j < source.properties.size(); ++j) {
                const DetailTag& property = source.properties[j];
                const StringId tag = intern_tag_name(database, tag_names, property.id);
                const StringId payload = property.payload.empty()
                    ? invalid_string_id() : database.strings.add(property.payload);
                database.tagged_values.push_back(TaggedValue(tag, payload));
            }
            if (source.kind == ResultKind::Polygon) {
                result.geometry = Range(static_cast<Index>(database.vertices.size()),
                                        static_cast<Index>(source.vertices.size()));
                database.vertices.insert(database.vertices.end(),
                                         source.vertices.begin(), source.vertices.end());
            } else {
                result.geometry = Range(static_cast<Index>(database.edges.size()),
                                        static_cast<Index>(source.edges.size()));
                database.edges.insert(database.edges.end(), source.edges.begin(), source.edges.end());
            }
            database.results.push_back(result);
        }

        check.executed_at = executed_at;
        check.check_text = check_text;
        check.results = results;
        check.detail_loaded = true;
        ++database.loaded_rule_check_count;
    } catch (...) {
        database.strings.rollback(string_checkpoint);
        database.results.resize(result_count);
        database.tagged_values.resize(property_count);
        database.vertices.resize(vertex_count);
        database.edges.resize(edge_count);
        database.check_text_lines.resize(text_count);
        tag_names.resize(tag_count);
        check = original_check;
        database.loaded_rule_check_count = loaded_count;
        throw;
    }
}

} // namespace

IndexedRdbFile::IndexedRdbFile(const std::string& path, const FastCheckIndexOptions& options)
    : file_(path),
      file_state_(file_.state()),
      database_(database_from_index(
          FastCheckIndexParser().parse_database(file_.descriptor(), options))) {
    verify_file_unchanged();
}

void IndexedRdbFile::verify_file_unchanged() {
    const detail::FileState current = file_.state();
    if (detail::same_file_snapshot(file_state_, current)) return;
    if (detail::same_file_after_path_replacement(file_state_, current)) {
        file_state_ = current;
        return;
    }
    throw ScanError(0, "RDB file changed after indexed snapshot was opened");
}

const RuleCheck& IndexedRdbFile::load_check(CheckId id) {
    verify_file_unchanged();
    const RuleCheck& selected = database_.check(id);
    if (!selected.detail_loaded) {
        const CheckDetail detail = detail::parse_check_detail(file_, selected.offset);
        verify_file_unchanged();
        store_detail(database_, tag_names_, id, detail);
    }
    return database_.check(id);
}

void IndexedRdbFile::load_all() {
    for (std::size_t i = 0; i < database_.check_count(); ++i) {
        (void)load_check(static_cast<CheckId>(i));
    }
}

} // namespace rdb
