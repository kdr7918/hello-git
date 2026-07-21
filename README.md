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

## 고속 텍스트 파서

`FastTextReader`는 `QFile::read()`로 기본 1 MiB 버퍼를 채우고 `memchr()`로 개행을 한 번씩만 검색합니다. `QFile::readLine()`, 라인별 `QByteArray`, `QString` 변환을 사용하지 않습니다. 버퍼 경계를 넘는 긴 라인만 내부 버퍼를 확장하고, 일반 라인은 `ByteView`로 참조하므로 라인 및 split 결과의 바이트 복사가 없습니다.

현재 라인을 처리하면서 다음 라인을 안전하게 참조할 수 있습니다. 두 View는 다음 `nextWindow()`, `seek()`, `setRange()`, `close()` 호출 전까지 유효합니다.

```cpp
FastTextReader reader;
reader.open(fileName);

LineWindow window;
while (reader.nextWindow(&window)) {
    const LineView &line = window.current;

    // 현재 라인 처리 중 다음 라인 look-ahead
    if (window.hasNext && window.next.bytes.startsWith('#')) {
        // next line is a heading
    }

    // 쉼표 split: 원본 버퍼를 가리키므로 문자열 복사가 없음
    QVector<ByteView> columns;
    line.bytes.split(',', &columns);

    // 또는 allocation 없는 필드 커서와 숫자 파서
    FieldCursor fields = line.fields(',');
    ByteView name;
    qint64 count = 0;
    double ratio = 0.0;
    fields.readString(&name);
    fields.readInt64(&count);
    fields.readDouble(&ratio);

    // memchr 기반 문자 검색과 절대 파일 위치
    const qint64 commaOffset = line.findAbsolute(',');

    // 정규식은 편의 경로이며 UTF-8 -> QString 변환 비용이 있으므로
    // 성능이 중요한 단순 검색에는 findAbsolute()/ByteView::find() 사용
    RegexHit hit;
    line.findRegex(QRegularExpression("ID=[0-9]+"), &hit);

    // 나중에 정확히 같은 라인으로 복귀 가능한 위치
    const SeekPoint saved = line.seekPoint();
}
```

저장한 위치 또는 임의의 바이트 범위로 바로 이동할 수 있습니다.

```cpp
reader.seek(saved);
reader.setRange(sectionBegin, sectionEnd, firstLineNumber);
```

`LineView`에는 `beginOffset`, `contentEndOffset`, `nextOffset`, `lineNumber`가 있으며, `FieldCursor::lastSourceSpan()`으로 마지막 필드의 절대 바이트 범위도 얻을 수 있습니다. 숫자 파서는 locale과 임시 문자열 없이 부호 있는/없는 64비트 정수, 소수 및 지수 표기 실수를 읽습니다.

### 테스트와 처리량 비교

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

cmake -S . -B build-bench -DBUILD_FAST_PARSER_BENCHMARK=ON
cmake --build build-bench --parallel
./build-bench/fast-parser-benchmark /path/to/large-file.txt
```

벤치마크는 동일한 1 MiB `read + memchr` 기준 구현과 `FastTextReader`의 라인 수, 소비 바이트, MiB/s를 비교합니다. 정규식은 의도적으로 핫패스 밖의 편의 기능입니다.

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
