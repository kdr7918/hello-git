# ASCII RDB Parser and Qt Viewer

ASCII Results Database(RDB)를 읽는 C++11 파서와 Qt 5.9 호환 Viewer다.

## 구성

- `Parser/` — 전체 RDB, 빠른 Check index, 선택 Check detail, 좌표 전용 파서
- `examples/qt_rdb_viewer/` — DockWidget 기반 Qt Widgets Viewer
- `*_sample.rdb` — Parser와 Viewer 자동 테스트용 fixture

Viewer는 파일을 열 때 다음 작업을 병렬로 수행한다.

1. 빠른 Check index 파싱
2. Coords Only의 전체 좌표 파싱 또는 All Params의 전체 태그 파싱
3. 사용자가 선택한 Check의 10,000 Result 단위 상세 파싱

All Params 모드의 Tree는 기본적으로 Check Name으로 묶고, 전체 파싱 완료 후 헤더
컨텍스트 메뉴에서 Check Name 또는 태그 Key를 최대 3 depth로 그룹화할 수 있다.

## 빌드와 테스트

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DBUILD_QT_RDB_VIEWER=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Qt Viewer는 Qt 5.9 API만 사용한다. CMake는 Qt 5를 우선 사용하며, 개발 환경에 Qt 5가
없을 때만 Qt 6로 호환성 빌드한다.
