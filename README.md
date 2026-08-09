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

대용량 파일에서는 `IndexedRdbFile`을 사용한다. 이 클래스는 표준 C++ 스트림으로 읽은
file-byte snapshot을 관리하며, index와 detail 데이터는 모두 내부의 canonical
`Database`에 기록한다.

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

전체 예시의 `tagged_keys(database, loaded)` 함수는 Check에 속한 모든 Result의
`TaggedValue::id`만 `StringId`로 모은다. 문자열을 복사하지 않고 파일 등장 순서와
중복 key를 유지하며, 실제 key 문자열은 `database.strings.get(id)`로 조회한다.

`load_check()`는 동일 Check에 대해 idempotent하며, `load_all()`은 모든 Check를 같은
`Database`에 완성한다. Result가 0개인 Check도 `detail_loaded`로 index-only 상태와
구분한다.

MainWindow 앱은 Check index가 준비되면 하나의 백그라운드 스레드에서 모든 Check의
Detail을 파일 순서대로 미리 읽는다. 완성된 Check는 같은 `Database`에 캐시되므로
Table Row를 선택할 때 다시 파싱하지 않는다. 아직 순서가 오지 않은 Check를 선택하면
추가 파서를 만들지 않고 백그라운드 작업을 기다리며, 현재 파싱 중인 Check는
10,000 Result 단위로 화면에 반영한다.

## 백그라운드 full parse 후 교체

`IndexedRdbFile` 복사는 같은 immutable file-byte buffer를 공유하고 `Database`와 tag parser
상태는 독립적으로 깊은 복사한다. 따라서 복사가 완료된 뒤 서로 다른 복사본에서
`load_check()`를 호출해도 상대 복사본의 Database는 변경되지 않는다.

전체 파싱 결과를 전면 객체로 교체할 때는 복사 대입도 가능하지만, 대용량 pool을 다시 복사하지
않도록 worker 결과를 **move 대입**하는 것이 권장된다.

```cpp
#include <future>

std::future<rdb::IndexedRdbFile> pending = std::async(
    std::launch::async,
    []() {
        rdb::IndexedRdbFile parsed("results.rdb");
        parsed.load_all();
        return parsed;
    });

// UI/foreground thread에서 기존 index-only 객체를 계속 사용한다.
rdb::IndexedRdbFile active("results.rdb");

// worker 완료 후 foreground reader가 없는 동기화 지점에서 O(1)로 교체한다.
active = pending.get();
```

`IndexedRdbFile`은 내부 동기화를 제공하지 않는다. 같은 인스턴스의 `load_check()`/`load_all()`/
대입/swap과 read/copy/move를 겹치면 data race이므로 외부에서 동기화해야 한다. 복사가 완료된
서로 다른 인스턴스는 독립적으로 lazy load할 수 있다. worker 완료를 기다린 뒤 mutex,
event-loop handoff 등으로 기존 reader가 없는 시점에 교체해야 하며, 대입하면 기존
`database()`에서 얻은 참조·포인터·iterator는 모두 무효화된다.

file-byte buffer는 생성 시점의 내용을 소유하므로 이후 detail 파싱은 동일한 immutable
snapshot을 사용한다. 표준 C++11만 사용하므로 파일 변경 검증은 크기 변화를 기준으로 한다.

## 수명과 순서

- `CheckId`는 `Database::rule_checks`의 index이며 파일 순서대로 고정된다.
- Check 이름은 중복될 수 있고 `find_checks()`는 모든 ID를 파일 순서대로 반환한다.
- 선택 load 시 전역 pool의 물리적 순서는 load 순서일 수 있으므로 항상 `Range`로 순회한다.
- `StringId`, `CheckId`, 복사한 `Range`, 복사한 `Point`/`Edge`는 안정적이다.
- `StringRef`, vector 참조·포인터·iterator는 이후 `load_check()`의 재할당으로 무효화될 수 있다.
- pathname이 다른 파일로 교체돼도 이미 읽은 file-byte snapshot은 유지된다.

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
