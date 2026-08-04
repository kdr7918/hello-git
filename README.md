# ASCII RDB Parser

ASCII Results Database(RDB)를 읽는 C++11 파서다.

## 구성

- `Parser/` — 전체 RDB 파서, 빠른 Check index, 선택 Check detail
- `examples/rdb_indexed_example.cpp` — index, 선택 detail, geometry/property 순회 예시
- `*_sample.rdb` — 파서 자동 테스트용 fixture

대용량 RDB의 Check 탐색에는 다음 두 파서를 사용한다.

1. `FastCheckIndexParser` — Check 이름, Result 수, comment, 파일 offset 인덱싱
2. `CheckDetailParser` — index offset에서 선택한 Check만 상세 또는 batch 파싱

전체 파서의 `Result::properties`와 선택 상세의 `DetailResult::properties`는 모두 좌표 전,
좌표 사이, 좌표 뒤 Property를 위치별로 분리하지 않고 파일에서 발견한 순서대로 통합한다.

인덱스와 선택 상세를 작은 flat pool에 함께 보관하려면 `IndexedRdbFile`을 사용한다.

```cpp
#include "rdb_compact_database.hpp"

rdb::IndexedRdbFile file("results.rdb");
std::vector<rdb::CheckId> ids = file.find_checks("M1.SPACING.1");
if (!ids.empty()) {
    rdb::CompactCheckView check = file.load_check(ids[0]);
    if (check.detail_result_count() != 0)
        std::cout << check.result(0).property_count() << " properties\n";
}
```

`CompactCheckView`/result/property proxy와 ID는 database가 살아 있는 동안 사용할 수 있다.
`vertex()`와 `edge()`는 값을 복사해 반환하므로 이후 detail load 뒤에도 반환값이 유효하다.
단, `TextView`는 내부 byte pool을 빌려 보므로 이후 `load_check()`/`load_all()`이 pool을
재할당하면 무효화될 수 있다. 문자열이 다시 필요할 때 해당 accessor로 새 view를 얻거나
`str()`로 복사해야 한다.

## 사용 예시 실행

`rdb-indexed-example`은 다음 흐름을 한 번에 보여 준다.

1. 파일 전체의 Check 이름과 offset을 index
2. 이름으로 Check 검색
3. 선택 Check만 `load_check()`로 상세 파싱
4. Result, Property, Polygon vertex, Edge 순회
5. compact storage 메모리 사용량 출력

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j

# Check 이름을 생략하면 첫 번째 Check를 선택한다.
./build/examples/rdb-indexed-example standard_sample.rdb

# 이름이 같은 Check가 여러 개면 첫 번째 항목을 선택한다.
./build/examples/rdb-indexed-example standard_sample.rdb M1.SPACING.1
```

프로그램 전체 코드는 [`examples/rdb_indexed_example.cpp`](examples/rdb_indexed_example.cpp)에
있으며, 잘못된 인자, 찾을 수 없는 Check, RDB 파싱 오류도 종료 코드와 오류 메시지로 처리한다.

메모리를 줄이기 위해 check 이름/offset 검색과 tag-name interning은 보조 hash map 없이
선형 탐색한다(각각 check 수와 고유 tag-name 수에 대해 O(n)). `memory_usage()`는 compact
database가 소유한 byte pool/vector의 used/capacity storage만 포함하며 mmap, 객체 자체,
allocator bookkeeping과 parsing 중 임시 객체는 포함하지 않는다.

## 빌드와 테스트

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```
