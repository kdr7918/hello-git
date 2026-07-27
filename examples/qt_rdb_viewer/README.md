# Qt 6 ASCII RDB TableView 예제

기존 `Parser/` 코드를 실제 Qt GUI에서 조합한 예제입니다. 실행 시 다음 두 작업을 동시에 시작합니다.

- `FastCheckIndexParser::parse_database()`로 Check index를 읽어 위 TableView 모델을 갱신
- `AsciiRdbParser::parse_file()`로 모든 Check 상세 결과를 백그라운드 파싱

Index 완료 후 첫 Check를 자동 선택합니다. 전체 백그라운드 파싱이 아직 끝나지 않았다면 선택 Check는 `CheckDetailParser::parse_file_at_batches()`로 별도 파싱하며 **10,000 result마다** 아래 TableView에 추가합니다. 다른 Check를 선택하면 cancellation token을 올려 기존 파싱을 중단하고 request ID로 이미 큐에 들어간 낡은 batch도 폐기합니다.

전체 백그라운드 파싱이 끝나면 현재 선택 Check의 결과를 전체 파싱 결과로 즉시 교체합니다. 배치 추가와 전체 결과 교체 전후에 `(kind, ordinal)` 안정 키로 다중 선택/current row/스크롤 위치를 복원합니다.

**최종 보관 자료구조는 `Parser/ascii_rdb.hpp`의 `rdb::Database`입니다.** 선택 Check용 `DetailResult` batch와 Qt `DetailRow`는 전체 파싱 완료 전 화면을 빠르게 채우기 위한 임시 데이터이며, 전체 파싱 완료 후 폐기되고 `rdb::Database::{rule_checks,results,vertices,edges,tagged_values}`를 읽어 TableView를 다시 구성합니다.

## 요구사항 대응

1. `startIndexParsing()` — RDB Check Index 파싱
2. `CheckTableModel::setIndex()` — Check TableView 모델 갱신
3. `startFullBackgroundParsing()` — 모든 Check 상세 결과 백그라운드 파싱
4. Index 완료 callback의 `selectRow(0)` — 첫 항목 자동 선택
5. `startSelectedDetailParsing()` — 선택 Check를 10,000개 단위 파싱/갱신
6. `cancelSelectedDetail()` + `detailRequestId_` — 선택 변경 시 실행 중 작업과 stale batch 취소
7. `showBackgroundDetail()` — 전체 파싱 완료 즉시 현재 상세 테이블 교체, 선택 유지

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DBUILD_QT_RDB_VIEWER=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/examples/qt_rdb_viewer/qt-rdb-viewer standard_sample.rdb
```

Qt 6의 Core, Widgets, Concurrent, Test 모듈이 필요합니다. GUI 없는 CI에서는 테스트가 `QT_QPA_PLATFORM=offscreen`으로 실행됩니다.

## 핵심 동시성 규칙

- worker는 Qt model/view를 직접 만지지 않습니다. `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`로 GUI thread에 batch를 전달합니다.
- index/full/selected worker는 모두 atomic cancellation token을 확인합니다. 새 파일을 열면 이전 파일의 세 작업을 모두 취소합니다.
- viewer destructor는 token을 올린 뒤 자체 worker task가 callback enqueue를 끝낼 때까지 join하므로 `QPointer` null 확인과 `invokeMethod()` 사이의 QObject lifetime race가 없습니다.
- cancellation token은 `std::atomic_bool`입니다. 파서는 결과/좌표/태그/입력 줄 루프에서 이를 확인합니다.
- cancellation과 별개로 file generation/request ID를 확인하므로, 이미 event queue에 들어온 이전 파일/이전 선택 결과가 새 테이블에 섞이지 않습니다.
- 전체 DB는 worker 완료 후 `shared_ptr` 소유권으로 GUI thread에 전달하며 이후 읽기 전용으로 사용합니다.
