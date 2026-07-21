#ifndef CALIBRE_ASCII_RDB_PARSER_HPP
#define CALIBRE_ASCII_RDB_PARSER_HPP

#include "calibre_ascii_rdb.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace calibre {
namespace rdb {

typedef std::uint64_t FileOffset;

class ParseError : public std::runtime_error {
public:
    ParseError(FileOffset offset, std::uint64_t line, const std::string& message);

    FileOffset offset() const { return offset_; }
    std::uint64_t line() const { return line_; }

private:
    FileOffset offset_;
    std::uint64_t line_;
};

struct ParseOptions {
    std::size_t read_buffer_bytes;
    std::size_t max_line_bytes;
    /* Accept the local extension where property lines follow geometry. */
    bool allow_properties_after_geometry;

    ParseOptions()
        : read_buffer_bytes(4U * 1024U * 1024U),
          max_line_bytes(64U * 1024U * 1024U),
          allow_properties_after_geometry(true) {}
};

/* Fully parses an ASCII RDB into the compact Database model in one pass. */
class AsciiRdbParser {
public:
    Database parse_file(const std::string& path,
                        const ParseOptions& options = ParseOptions()) const;
};

} // namespace rdb
} // namespace calibre

#endif // CALIBRE_ASCII_RDB_PARSER_HPP
