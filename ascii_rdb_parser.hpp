#ifndef ASCII_RDB_PARSER_HPP
#define ASCII_RDB_PARSER_HPP

#include "ascii_rdb.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace rdb {

// 수십 GB 파일에서도 바이트 위치를 표현할 수 있도록 64비트를 사용한다.
typedef std::uint64_t FileOffset;

// 문법 오류가 난 파일 위치와 줄 번호를 함께 전달한다.
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
    // 운영체제 read() 한 번에 읽을 버퍼 크기와, 비정상적으로 긴 한 줄의 허용 한계다.
    std::size_t read_buffer_bytes;
    std::size_t max_line_bytes;
    // 표준 위치 외에 좌표 뒤에 태그가 붙는 확장 형식도 허용할지 정한다.
    bool allow_properties_after_geometry;

    ParseOptions()
        : read_buffer_bytes(4U * 1024U * 1024U),
          max_line_bytes(64U * 1024U * 1024U),
          allow_properties_after_geometry(true) {}
};

/*
 * 파일 처음부터 끝까지 한 번 순차 읽어 압축 Database로 만드는 전체 파서다.
 * 입력 파일 바이트는 버퍼로만 읽지만, 파싱 결과는 GUI 사용을 위해 Database에 보관한다.
 */
class AsciiRdbParser {
public:
    Database parse_file(const std::string& path,
                        const ParseOptions& options = ParseOptions()) const;
};

} // namespace rdb

#endif // ASCII_RDB_PARSER_HPP
