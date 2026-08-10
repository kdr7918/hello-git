# Database RDB App

`OriginalApp`의 `CalibreTextDock`, Designer UI, signal/slot, QThread 흐름을
유지하면서 `RDB_DATA`와 `RDB_ALL_DATA` 객체 그래프를 공용
`rdb::Database` 저장소로 교체한 독립 프로젝트다.

## 데이터 소유 구조와 파싱 흐름

- Check Index 파싱이 끝나면 전체 상세 BG 파서가 전용 `QThread`와 전용
  `rdb::Database`에서 처음 Check부터 마지막 Check까지 계속 동작한다.
- BG 완료 전의 Table/Tree 선택은 별도 `QThread`에서 선택 Check만 파싱해
  Coord Table의 임시 `rdb::Database`에 표시한다. CheckName으로 합쳐진 Tree
  행은 그 행이 보관한 모든 `CheckId`를 순서대로 파싱한다.
- 선택 중 다른 행을 고르면 기존 선택 파싱을 취소하고 새 요청을 시작한다.
  BG가 완료되어도 선택 파싱을 취소하며, 완성된 BG Database를 모든 모델에
  즉시 연결한다.
- BG와 선택 파서의 쓰기 Database를 분리하므로 동시 파싱 중 같은 컨테이너를
  수정하는 data race가 없다.
- BG 완료 후 Chip/Coord/Bg TableModel과 TreeModel은 같은
  `std::shared_ptr<Database>`를 공유한다.
- BG 완료 전에는 Tree header 우클릭 메뉴와 grouping을 막고, 완료 후에만
  활성화한다.
- Tree node는 결과 객체가 아니라 `CheckId`와 `ResultIndex`만 보관한다.
- 좌표 문자열과 QVariant는 View가 요청한 셀에서만 생성한다.
- 선택 Detail parser는 10,000개 단위로 전달하며 미처리 GUI 배치는 최대
  2개다.

## 유지한 UI objectName

- `coordinate_table_view`
- `chip_name_table_view`
- `search_index_label`
- `rdb_tree_view`
- `search_edit`
- `next_btn`
- `prev_btn`
- `splitter`

## 빌드

최종 타겟은 RHEL 8, Qt 5.9, C++11이다.

```bash
cmake -S DatabaseApp -B build-database-app -DCMAKE_BUILD_TYPE=Release
cmake --build build-database-app -j
ctest --test-dir build-database-app --output-on-failure
./build-database-app/database-rdb-app standard_sample.rdb
```

Check Index는 항상 전용 `QThread`에서 동작한다. 진행률은
`CalibreTextDock::CheckIndexProgress(int)` signal과 `ParseRDBCheck()`의 선택적
callback으로 제공하며 앱 내부에서 ProgressBar를 소유하지 않는다.
