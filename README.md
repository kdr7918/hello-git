# ASCII RDB Parser

ASCII Results Database(RDB)를 읽는 C++11 파서다.

## 단일 최종 모델: `rdb::Database`

최종 `Database`는 **Check Index Parser 결과로 헤더와 Check 레코드를 먼저 만들고,
선택한 Check의 Detail Parser 결과를 같은 flat pool에 append**하여 완성한다. 별도의
Compact Database나 detail 전용 최종 모델은 없다.

`Database`는 ASCII RDB 파일 헤더와 Check index/detail을 다음 flat pool에 보관한다.

- 파일 헤더: `top_cell_name`, `database_precision`
- Check index: 이름, comment, 파일 offset, current/original Result 수, 선언된 check-text 수
- Check detail: 실행 시각, check-text Range, Result Range, loaded 상태
- Result detail: kind, ordinal, signature suffix, Property Range, geometry Range
- 전역 pool: `results`, `tagged_values`, `vertices`, `edges`, `check_text_lines`, `strings`

`Result::properties`는 좌표 전, 좌표 사이, 좌표 뒤 Property를 분리하지 않고 파일에서
발견한 순서대로 보관한다.

## Check index + Detail append

대용량 파일에서는 `IndexedRdbFile`을 사용한다. 이 클래스는 open-file snapshot과 mmap
수명만 관리하며, index와 detail 데이터는 모두 내부의 canonical `Database`에 기록한다.

```cpp
#include "rdb_indexed_file.hpp"

rdb::IndexedRdbFile file("results.rdb");
const rdb::Database& database = file.database();

std::vector<rdb::CheckId> ids = database.find_checks("M1.SPACING.1");
if (!ids.empty()) {
    const rdb::RuleCheck& indexed = database.check(ids[0]);
    std::cout << indexed.offset << '\n';
    std::cout << indexed.current_result_count << '\n';
    std::cout << database.strings.get(indexed.comment).str() << '\n';

    file.load_check(ids[0]);
    const rdb::RuleCheck& loaded = database.check(ids[0]);

    for (rdb::Index i = 0; i < loaded.results.count; ++i) {
        const rdb::Result& result = database.results[loaded.results.begin + i];

        for (rdb::Index p = 0; p < result.properties.count; ++p) {
            const rdb::TaggedValue& property =
                database.tagged_values[result.properties.begin + p];
            std::cout << database.strings.get(property.id).str() << ' '
                      << database.strings.get(property.payload).str() << '\n';
        }

        if (result.kind == rdb::ResultKind::Polygon) {
            for (rdb::Index v = 0; v < result.geometry.count; ++v) {
                const rdb::Point& point = database.vertices[result.geometry.begin + v];
                std::cout << point.x << ' ' << point.y << '\n';
            }
        } else {
            for (rdb::Index e = 0; e < result.geometry.count; ++e) {
                const rdb::Edge& edge = database.edges[result.geometry.begin + e];
                std::cout << edge.first.x << ' ' << edge.first.y << " -> "
                          << edge.second.x << ' ' << edge.second.y << '\n';
            }
        }
    }
}
```

`load_check()`는 동일 Check에 대해 idempotent하며, `load_all()`은 모든 Check를 같은
`Database`에 완성한다. Result가 0개인 Check도 `detail_loaded`로 index-only 상태와
구분한다.

## 수명과 순서

- `CheckId`는 `Database::rule_checks`의 index이며 파일 순서대로 고정된다.
- Check 이름은 중복될 수 있고 `find_checks()`는 모든 ID를 파일 순서대로 반환한다.
- 선택 load 시 전역 pool의 물리적 순서는 load 순서일 수 있으므로 항상 `Range`로 순회한다.
- `StringId`, `CheckId`, 복사한 `Range`, 복사한 `Point`/`Edge`는 안정적이다.
- `StringRef`, vector 참조·포인터·iterator는 이후 `load_check()`의 재할당으로 무효화될 수 있다.
- pathname이 다른 파일로 교체돼도 이미 연 snapshot을 유지하며, 같은 inode의 변경은
  detail 저장 전에 거부한다.

## 사용 예시 실행

전체 예시는 [`examples/rdb_database_example.cpp`](examples/rdb_database_example.cpp)에 있다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j

# 첫 Check 선택
./build/examples/rdb-database-example standard_sample.rdb

# 이름으로 선택
./build/examples/rdb-database-example standard_sample.rdb M1.SPACING.1
```

예시는 파일 헤더, Check index, 선택 detail, Property/Polygon/Edge 순회와 최종 Database
pool 크기를 출력한다. 잘못된 인자와 찾을 수 없는 Check는 별도 종료 코드로 처리한다.

## 구성

- `Parser/ascii_rdb.hpp` — canonical Database와 flat record/pool 모델
- `Parser/ascii_rdb_parser.*` — 전체 파일 → Database
- `Parser/rdb_check_index.hpp` — 빠른 Check index scanner
- `Parser/rdb_check_detail.hpp` — 선택 Check detail parser
- `Parser/rdb_indexed_file.*` — snapshot 관리와 index/detail → 같은 Database 통합
- `examples/rdb_database_example.cpp` — 통합 사용 예시
- `*_sample.rdb` — 자동 테스트 fixture

## 빌드와 테스트

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```
