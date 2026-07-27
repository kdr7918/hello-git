# ASCII RDB parser (C++11)

Compact, dependency-free C++11 parser for the ASCII Results Database (RDB)
emitted by DRC flows.  It reads a file sequentially and returns a
fully materialized, memory-conscious `Database` model suitable for a Qt table
or tree model.

The repository includes synthetic RDB fixtures only; validate the parser with
an actual production output before treating every optional tag or layout variant
as supported.

## What is parsed

- Top-cell name and database precision (DBU) from the first line
- Rule-check name and `<current> <original> <check-text count> <timestamp>`
  header
- Rule-check text lines
- Polygon records: `p <ordinal> <vertex-count>` followed by `x y` vertices
- Edge records: `e <ordinal> <edge-count>` followed by `x1 y1 x2 y2` edges
- Tagged values such as `PP`, `PA`, `EL`, `EW`, `CN`, `DA`, and `DV`

Tags in the standard position (between a `p`/`e` line and geometry) are
always parsed.  The optional local extension that places tags after geometry
is enabled by default and can be disabled through `ParseOptions`.

## Design

`Database` avoids allocating a vector and string for every RDB record:

- All geometry, results, tags, and check-text references are global contiguous
  arrays.
- Records refer to these arrays with compact 32-bit `Range` values.
- Text lives in a byte arena and is referenced by a 32-bit `StringId`.
- Property identifiers are interned (`PP`, `CN`, etc.); each payload remains
  available as its original string.
- File positions use 64-bit `FileOffset`; coordinates use signed 64-bit
  integers.

The parser reads the input through a configurable 4 MiB buffer, rather than
loading the source bytes all at once.  The parsed database itself is retained
in memory, so the RAM requirement grows with the result count, geometry, and
text retained for the UI.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The project requires CMake 3.16+ and a C++11 compiler.  It has no runtime
dependencies and does not require Qt.

## Basic use

```cpp
#include "ascii_rdb_parser.hpp"

rdb::ParseOptions options;
options.allow_properties_after_geometry = true;

rdb::AsciiRdbParser parser;
rdb::Database database = parser.parse_file("result.rdb", options);

for (std::size_t i = 0; i < database.rule_checks.size(); ++i) {
    const rdb::RuleCheck& check = database.rule_checks[i];
    // check.results indexes database.results.
}
```

Malformed input and model-capacity overflows throw `rdb::ParseError`
or `std::length_error`.  `ParseError` contains the source byte offset and line
number, which can be reported directly in a UI.

## Incremental UI parsers

Three additional header-only components support an on-demand UI workflow.
They avoid retaining data outside each component's declared output.

```cpp
#include "rdb_check_index.hpp"
#include "rdb_check_detail.hpp"
#include "rdb_check_geometry.hpp"

rdb::FastCheckIndexParser index_parser;
rdb::FastCheckIndexOptions index_options;
index_options.progress_callback = [](int progress) {
    // progress is monotonic and in [0, 100]. 100 means parsing is complete.
};
rdb::CheckIndexDatabase index =
    index_parser.parse_database("result.rdb", index_options);
std::string top_cell = index.top_cell_name;
double precision = index.database_precision;
std::string comment = index.checks[0].comment; // Multi-line text joined with '\n'.
// index.checks[i].name / offset / geometry_count / comment

// 같은 RDB에서 여러 Check를 선택한다면 파일 매핑을 한 번 재사용한다.
rdb::CheckDetailFile detail_file("result.rdb");
rdb::CheckDetail selected = detail_file.parse_at(index.checks[0].offset);

rdb::CheckGeometryParser geometry_parser;
rdb::GeometryDatabase geometry = geometry_parser.parse_file("result.rdb");
```

- `FastCheckIndexParser` uses a sequential `read()` buffer and `memchr(':')`
  to parse the top-cell name/database precision and find `HH:MM:SS` rule
  headers. `CheckIndexDatabase` retains those header fields plus each check
  name, byte offset, current `p/e` result count, and one `std::string comment`.
  Declared comment lines between the time header and the first `p/e` signature
  are joined with `\n` without a trailing newline. Blank lines and surrounding
  spaces are preserved; CRLF is normalized by removing `\r`.
  `FastCheckIndexOptions::progress_callback` receives strictly increasing
  integer progress values from `0` through `100`; callback exceptions stop the
  parse, and `100` is emitted only after comment collection and file-consistency
  verification complete.
  Database precision is a
  finite positive `double`; decimal and scientific notation use the classic C
  locale. This is the fastest TreeView stage. The legacy `parse_file()` API
  still returns only the check list.
- `CheckDetailParser` seeks to one indexed offset and parses that check's
  check text, tags, and geometry without loading the earlier checks.
  `CheckDetailFile` is the preferred reusable session for repeated selection.
- `CheckGeometryParser` scans the complete file once and retains check name,
  offset, result count, `p/e` metadata, and coordinates only.

`geometry_count` in `CheckIndexEntry` and `GeometryCheck` means the count of
`p/e` result records (defect shapes), not the total vertex or edge count.

`FastCheckIndexParser` is a trusted-format fast path: each rule header must
contain three leading decimal counts and an `HH:MM:SS` timestamp.  Use the
coordinate parser when a nonstandard file must be structurally interpreted.

## Repository layout

- `examples/python_log_keyword_counter/` — 날짜 범위별 로그 4번째 줄 키워드 카운터 예제
- `examples/qt_rdb_viewer/` — Qt 6 TableView + 백그라운드/배치/cancellation GUI 예제
- `Parser/ascii_rdb.hpp` — compact public data model
- `Parser/ascii_rdb_parser.hpp/.cpp` — full one-pass parser
- `Parser/rdb_check_index.hpp` — buffered `memchr(':')` fast name/offset/result-count indexer
- `Parser/rdb_check_detail.hpp` — offset-based single-check detail parser
- `Parser/rdb_check_geometry.hpp` — complete coordinate-only parser
- `Parser/rdb_parser_tests.cpp` — executable test suite
- `*_sample.rdb` — small and large synthetic fixtures used by the parser tests

## Known scope

This is an ASCII RDB reader, not a binary RDB reader.  It currently models
polygon and edge result records plus generic tagged values; unfamiliar vendor
extensions are preserved as `id + payload` tag strings when they occur in a
supported tag location.
