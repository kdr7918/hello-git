# Qt Large File TOC / Detail Viewer

Qt 5.9의 Widgets, `QThread`, `QAbstractItemModel`, `QAbstractTableModel`로 만든 대용량 텍스트 파일 뷰어 예제입니다.

## 동작 흐름

1. `TocParseWorker`가 별도 스레드에서 파일을 순차 스캔해 `#`, `##` 형식의 목차와 각 섹션의 바이트 범위만 먼저 계산합니다. GUI 스레드는 진행률과 인터럽트 버튼을 계속 처리합니다.
2. 스캔 결과를 `TocTreeModel`에 전달해 TreeView를 구성합니다.
3. `FullParseWorker`가 별도 스레드에서 전체 레코드를 파싱합니다.
4. 전체 파싱 중 목차를 선택하면 기존 구간 작업을 중단하고 `SectionParseWorker`가 해당 바이트 범위만 읽습니다. 결과는 정확히 10,000개 단위로 `DetailTableModel`에 추가됩니다.
5. 전체 파싱이 끝나면 현재 섹션의 임시 데이터를 최종 결과로 교체합니다.

TableView 선택은 각 레코드의 파일 오프셋을 안정 키로 저장했다가 모델 갱신 후 다시 적용합니다. 따라서 10,000행 배치 추가와 최종 결과 교체에도 선택이 유지됩니다.

## 입력 예시

```text
# Customers
customer-001,Seoul
customer-002,Busan
## Premium
customer-101,Gold
# Orders
order-001,12500
```

목차 행은 `#` 개수로 계층을 표현합니다. 데이터 행은 예제용 `SimpleRecordParser`가 첫 번째 쉼표 또는 탭을 기준으로 Key/Value를 나눕니다. 실제 포맷을 적용할 때는 `IRecordParser::parseLine()` 구현만 교체하면 됩니다.

## Qt 5.9 빌드

qmake:

```sh
mkdir build && cd build
/path/to/Qt/5.9/bin/qmake ../hello-git.pro
make -j4
./large-file-viewer ../examples/sample.txt
```

CMake:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/5.9/lib/cmake
cmake --build build --parallel
./build/large-file-viewer examples/sample.txt
```

현재 전체 파서는 설명을 단순하게 유지하기 위해 결과를 메모리에 보관합니다. 파일 크기가 가용 메모리보다 클 수 있는 실제 제품에서는 `ParsedDocument` 저장소를 SQLite나 메모리 매핑 인덱스로 교체하는 편이 안전합니다.
