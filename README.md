# 쉬운 Property 예제

**먼저 `property-usage.cc`만 읽으세요.** 저장과 조회는 아래가 전부입니다.

```cpp
using namespace layout_property;

Property property;
property.name = "net_name";
property.addText("VDD");

PropertyList list;
list.push_back(property);

store.saveShape(shape.base(), list);
store.finish(); // 모든 Shape를 저장한 뒤 한 번만

PropertyList found = store.getShape(shape.base());
```

## 핵심 객체

```text
PropertyValue  = 값 하나 (문자열, 정수, 실수 등)
Property       = 이름 + 값 목록
PropertyList   = Property의 vector
PropertyStore  = Layout이 가진 저장소
```

값 추가:
```cpp
property.addText("VDD");
property.addInteger(-10);
property.addUnsigned(10);
property.addReal(1.25);
property.addFloat(0.5f);
property.addRatio(1, 3);
property.addText(std::string("A\0B", 3), PropertyValue::Binary);
```

조회 결과는 일반 구조체입니다:
```cpp
for (const Property& item : found) {
    for (const PropertyValue& value : item.values) {
        if (value.type == PropertyValue::Text)
            std::cout << item.name << " = " << value.text << '\n';
    }
}
```

GDS는 `property.isGds = true`, `property.gdsAttribute = 17`로 번호를 지정합니다.
Cell/File은 saveCell/getCell, saveFile/getFile을 사용합니다.

## 읽는 순서

1. [property-usage.cc](property-usage.cc) — 짧은 저장/조회 예제
2. [property.h](property.h) — Property 구조체
3. [property-store.h](property-store.h) — 저장/조회 함수
4. [INTEGRATION.md](INTEGRATION.md) — Multigon의 어느 위치에 붙이는지

나머지는 내부 구현이므로 처음에는 건너뛰어도 됩니다:
- `property-store.cc`: 쉬운 구조체와 내부 저장 형식 사이 변환
- `oasis-property-adapter.h`: 기존 OASIS Property -> 쉬운 Property
- `detail/`: 기존 byte encoding / 이름 intern / 정렬된 주소표

## 무엇을 쉽게 바꿨나요?

사용 코드에서 key ID, offset, blob, view 수명, template callback을 직접 다루지 않습니다.
`Property`를 만들고 `saveShape()`/`getShape()`만 사용합니다.

**내부 저장 방식은 유지했습니다.** Shape마다 vector/string 객체를 영구 보관하는
것이 아니라, 입력 객체를 연속 bytes에 변환하고 주소 연결표는 정렬 vector로 둡니다.
이름 hash map은 build 중만 사용합니다. Property 순서·중복 이름·값 타입은 보존합니다.

쉬운 `getShape()`는 **조회한 목록 전체를 복사·복원**합니다. 이 복사 비용과 임시
메모리는 기존 저수준 view 조회보다 추가됩니다. 읽기 쉬운 샘플용 API이며,
대량 Shape 순회 성능을 보장하거나 Multigon 운영 설정을 변경한 것은 아닙니다.
내부 zero-copy view 코드는 `detail/`에 남아 있습니다.

## 빌드

```bash
g++ -std=c++11 -Wall -Wextra -Wpedantic -Werror -pthread \
    property-usage.cc property-store.cc -o /tmp/property-example
/tmp/property-example
```

실제 출력:
```text
net_name = VDD
```

strict C++11 빌드와 별도 회귀/ASan/UBSan 검사를 통과했습니다.
회귀에서는 타입·정수 경계·binary NUL·순서·중복 이름·파일/Cell/Shape 조회·
조회 복사본 수명·finish 전후 제한·병렬 저장을 확인했습니다.
Adapter는 실제 Multigon 헤더로 문법 검사했습니다.

## 아직 하지 않은 것

- Multigon 원본에 실제 적용, SDK/Writer 연결, 파일 roundtrip
- name 레코드 자체와 생략 TEXT/X/NODE의 Property 보존
- 편집/삭제, finish 이후 추가 저장, reset/lazy import
- 원본 OASIS bytes 복원, blob dedup, 성능/메모리 측정

이 저장소는 복사·붙여넣기용 신규 예제만 담습니다. Multigon 원본 소스는 없습니다.
원래 복잡한 버전은 Git 이력에도 남아 있습니다.
