# Qt 5.9 C++11 ASCII RDB Viewer

`Parser/`의 고속 파서를 Qt Widgets 화면에 연결한 예제다. Qt 5.9 API와 C++11만
사용하며, Qt 5가 없는 개발 환경에서는 CMake가 Qt 6로 호환성 빌드한다.

## 사용

`File` 메뉴에서 모드를 먼저 선택한다.

- `Open Coords Only…` — 왼쪽 Check 표와 오른쪽 단일 `Coords` 열을 표시한다.
- `Open All Params…` — 왼쪽 Tree와 태그 Key별 Results 열을 표시한다.

파일을 열면 아래 DockWidget이 열리고 다음 작업이 함께 시작된다.

1. `FastCheckIndexParser`가 Check Name/Count 목록을 먼저 완성한다.
2. Coords Only는 `CheckGeometryParser`, All Params는 `AsciiRdbParser`가 파일 전체를
   백그라운드에서 읽어 최종 자료구조를 완성한다.
3. 전체 작업 중 사용자가 Check를 선택하면 해당 Check만 다시 읽고, 10,000 Result씩
   오른쪽 표에 추가한다. Coords Only는 `CheckGeometryDetailParser`를 사용해 태그를
   보관하지 않는다.

새 파일을 열거나 선택을 바꾸면 이전 작업을 cancellation token과 요청 번호로 폐기한다.
모델을 배치 추가 또는 전체 자료로 교체할 때는 Result 안정 키, 현재 행, 다중 선택,
스크롤 위치를 복원한다.

## All Params Tree 그룹화

초기 Tree는 `Check Name`만 기준으로 묶으며, 같은 이름의 Check는 한 노드와 합산 Count로
표시한다. 전체 파싱이 끝나면 Tree 헤더를 우클릭해 grouping depth 1~3을 설정할 수 있다.
후보는 `Check Name`과 RDB에서 발견한 Tagged Value의 **Key**뿐이다. 이미 다른 depth에
선택한 기준은 비활성화되어 중복 선택할 수 없다.

태그 Key를 선택하면 실제 태그 Value가 Tree 노드가 된다. 노드를 선택하면 현재까지의
모든 Tree 조건을 만족하는 Result만 오른쪽 표에 표시한다. Tree 하단 검색은 대소문자를
무시한 정확 일치 BFS 검색이며 Prev/Next에서 순환한다.

## 빌드

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DBUILD_QT_RDB_VIEWER=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/examples/qt_rdb_viewer/qt-rdb-viewer standard_sample.rdb
```

명령행으로 파일을 넘긴 경우에는 Coords Only로 연다. All Params는 File 메뉴의 전용 동작을
사용한다.
