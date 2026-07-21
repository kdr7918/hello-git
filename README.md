# Calibre ASCII RDB parser (C++11)

Compact, dependency-free C++11 parser for the ASCII Results Database (RDB)
emitted by Calibre DRC flows.  It reads a file sequentially and returns a
fully materialized, memory-conscious `Database` model suitable for a Qt table
or tree model.

The repository includes synthetic RDB fixtures only; validate the parser with
an actual Calibre output before treating every optional tag or layout variant
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
#include "calibre_ascii_rdb_parser.hpp"

calibre::rdb::ParseOptions options;
options.allow_properties_after_geometry = true;

calibre::rdb::AsciiRdbParser parser;
calibre::rdb::Database database = parser.parse_file("result.rdb", options);

for (std::size_t i = 0; i < database.rule_checks.size(); ++i) {
    const calibre::rdb::RuleCheck& check = database.rule_checks[i];
    // check.results indexes database.results.
}
```

Malformed input and model-capacity overflows throw `calibre::rdb::ParseError`
or `std::length_error`.  `ParseError` contains the source byte offset and line
number, which can be reported directly in a UI.

## Repository layout

- `calibre_ascii_rdb.hpp` — compact public data model
- `calibre_ascii_rdb_parser.hpp/.cpp` — full one-pass parser
- `rdb_parser_tests.cpp` — executable test suite
- `*_sample.rdb` — small and large synthetic fixtures

## Known scope

This is an ASCII RDB reader, not a binary RDB reader.  It currently models
polygon and edge result records plus generic tagged values; unfamiliar vendor
extensions are preserved as `id + payload` tag strings when they occur in a
supported tag location.
