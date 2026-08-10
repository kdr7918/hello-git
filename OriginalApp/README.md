# Original RDB App

This is a standalone RHEL 8, Qt 5.9, C++11 application that preserves the original
`RDB_DATA`/`RDB_ALL_DATA` model contract while using the optimized parsers in
`../Parser`.

The supplied `calibre_text_dock.ui` is a replaceable baseline. A Designer UI
can replace it as long as these object names are preserved:

- `coordinate_table_view`
- `chip_name_table_view`
- `search_index_label`
- `rdb_tree_view`
- `search_edit`
- `next_btn`
- `prev_btn`
- `splitter`

The Check index always runs on its own `QThread`. Progress is exposed through
`CalibreTextDock::CheckIndexProgress(int)` and the optional callback passed to
`ParseRDBCheck()`; this project intentionally does not own a progress bar.

```bash
cmake -S OriginalApp -B build-original-app -DCMAKE_BUILD_TYPE=Release
cmake --build build-original-app -j
./build-original-app/original-rdb-app standard_sample.rdb
```
