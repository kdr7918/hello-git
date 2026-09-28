# Property 추가 코드

Multigon에 **복사·붙여넣기할 Property 관련 추가 코드만** 모은 독립 예제입니다.
기존 hello-git 내용은 현재 트리에서 제거했습니다. Git 이력은 유지했습니다.
Multigon 원본 구현은 포함하지 않으며 실제 Multigon 프로젝트도 수정하지 않았습니다.

## 파일

- [`property.h`](property.h): 임시 Property/List serializer, 타입 구분, 읽기 view
- [`property-store.h`](property-store.h): Layout 소유 byte pool + 정렬된 주소 연결표
- [`oasis-property-adapter.h`](oasis-property-adapter.h): 기존 OASIS Property를 복사·변환하는 추가 함수
- [`INTEGRATION.md`](INTEGRATION.md): **어느 파일/클래스/함수에 붙일지**, 필요한 코드 조각
- [`property-usage.cc`](property-usage.cc): 특정 Shape의 Property 저장·조회 예제

## 구조

```text
Layout
  PropertyStore
    names_       nameId -> 이름
    bytes_       Property와 typed value의 내부 encoding
    lists_       listId -> {offset, byteSize, count}
    elements_    정렬 vector: Shape 주소 -> listId
    cells_       정렬 vector: cellId -> listId
    file_        파일 Property 목록 위치
```

Shape에는 필드를 추가하지 않습니다. Property가 있는 소유자만 등록합니다.
Property 클래스 객체는 임시 변환에 사용하고, 장기 저장은 연속 bytes로 합니다.
중복 Property 이름·값 순서·standard flag·정수 부호·문자열 종류를 유지합니다.
OASIS modal/reference는 Builder 단계에서 해소된 값을 저장합니다.
이는 **의미 보존용 내부 encoding**이지 OASIS 파일의 원본 PROPERTY 바이트가 아닙니다.
Parser가 이미 정규화/반올림한 값을 원본으로 복원한다고 보장하지 않습니다.

## 복사 순서

1. 헤더 세 개를 `src/oasis/layout/`로 복사
2. `INTEGRATION.md`의 Layout/Builder 추가 코드 적용
3. 전체 import가 끝난 뒤 `freeze()` 한 번 호출
4. `store.element(shape.base())` -> `store.forEach(...)`로 조회

**샘플은 읽기 전용 Import 설계입니다.** 수정·삭제, name-table Property,
실제 SDK/Writer 연결, 병렬 coordinator 수집은 안내에 남은 통합 작업입니다.
따라서 헤더만 복사한다고 Multigon 전체 기능이 완성되지는 않습니다.

## 검증된 범위

독립 예제의 strict C++11 컴파일/실행 및 ASan+UBSan 실행을 통과했습니다.
이름 intern, 중복 이름/순서, binary NUL, 정수 경계, rational/float 값,
파일/Cell/Shape 연결, 없는 Shape 조회, freeze 후 변경 거부를 확인합니다.
OASIS adapter는 실제 Multigon 헤더를 대상으로 syntax-only 컴파일을 통과했습니다.
**실제 OASIS 파일 Import/Writer roundtrip 및 메모리·속도 벤치는 수행하지 않았습니다.**

```bash
g++ -std=c++11 -Wall -Wextra -Wpedantic -Werror -pthread \
    property-usage.cc -o /tmp/property-example
/tmp/property-example
```

```text
net_name: 1 values
net_name: 6 values
empty: 0 values
PASS: property ownership, lookup, order and typed values
```

## 선택한 단순화

- Shape 연결에는 unordered_map 대신 정렬 vector + lower_bound 사용
- 이름 intern hash map은 build 중만 사용, freeze 때 제거
- 이름과 owner 등록의 전역 mutex: 병렬 정확성의 기초만 제공, 성능 미검증
- 고정 64-bit 길이/offset encoding: 읽기 쉬운 예제이며 최소 크기 최적화 아님
- 반복 동일 blob도 현재는 복사: dedup은 별도 최적화
- size가 아니라 vector capacity, list descriptor, 이름 및 mutex 비용도 존재
- 조회 view는 frozen store의 수명 안에서만 유효

이 저장소는 필요한 추가 코드를 읽고 옮기는 용도이며, Multigon의 최종 저장 방식이나
메모리/속도 tradeoff를 확정·배포한 결과가 아닙니다.
