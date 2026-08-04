# ASCII RDB Parser

ASCII Results Database(RDB)를 읽는 C++11 파서다.

## 구성

- `Parser/` — 전체 RDB 파서, 빠른 Check index, 선택 Check detail
- `*_sample.rdb` — 파서 자동 테스트용 fixture

대용량 RDB의 Check 탐색에는 다음 두 파서를 사용한다.

1. `FastCheckIndexParser` — Check 이름, Result 수, comment, 파일 offset 인덱싱
2. `CheckDetailParser` — index offset에서 선택한 Check만 상세 또는 batch 파싱하며,
   좌표 앞뒤의 property는 `DetailResult::properties`에 발견 순서대로 통합

## 빌드와 테스트

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```
