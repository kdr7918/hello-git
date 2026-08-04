# ASCII RDB Parser

ASCII Results Database(RDB)를 읽는 C++11 파서다.

## 구성

- `Parser/` — 전체 RDB 파서, 빠른 Check index, 선택 Check detail
- `*_sample.rdb` — 파서 자동 테스트용 fixture

대용량 RDB의 Check 탐색에는 다음 두 파서를 사용한다.

1. `FastCheckIndexParser` — Check 이름, Result 수, comment, 파일 offset 인덱싱
2. `CheckDetailParser` — index offset에서 선택한 Check만 상세 또는 batch 파싱하며,
   좌표 앞뒤의 property는 `DetailResult::properties`에 발견 순서대로 통합

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
