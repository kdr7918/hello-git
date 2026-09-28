# 인코딩 없는 Property 예제

**PropertyList를 그대로 저장합니다.** detail 디렉터리, blob, encode/decode,
이름 ID, offset, 별도 byte pool을 제거했습니다.

## 사용법

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

## 실제 저장 구조

```cpp
struct ShapeEntry {
    const void* shape;
    PropertyList properties;
};

std::vector<ShapeEntry> shapes;
```

```text
Layout
  PropertyStore
    shapes_ → Shape 주소 + PropertyList
    cells_  → Cell ID + PropertyList
    file_   → PropertyList

PropertyList = vector<Property>
Property     = 이름 + standard + 값 목록
PropertyValue = 타입 + 문자열/숫자 필드
```

saveShape는 목록을 복사해서 저장합니다. finish에서 주소순으로 정렬하고,
getShape는 이진 탐색해서 목록의 복사본을 돌려줍니다. 없는 Shape는 빈 목록입니다.
같은 이름의 여러 Property와 값 순서는 유지합니다. Cell/File도 같은 방식입니다.

## 읽는 순서

1. [property-usage.cc](property-usage.cc) — 가장 짧은 사용 예제
2. [property.h](property.h) — Property 구조체와 값 추가 함수
3. [property-store.h](property-store.h) — 실제 저장 멤버
4. [property-store.cc](property-store.cc) — 복사 저장/정렬/조회
5. [INTEGRATION.md](INTEGRATION.md) — Multigon에 붙여넣을 위치
6. [CREATOR.md](CREATOR.md) — 저장된 Property를 Creator로 출력하는 예시
   ([property-creator.h](property-creator.h): 타입별 변환 함수)

`oasis-property-adapter.h`는 OASIS 객체를 이 구조체로 복사할 때 사용합니다.
**재인코딩이나 파서 raw capture를 수행하지 않습니다.**

## 숫자·문자열 추가

```cpp
property.addInteger(-10);
property.addUnsigned(10);
property.addReal(1.25);
property.addFloat(0.5f);
property.addRatio(1, 3);
property.addText(std::string("A\0B", 3), PropertyValue::Binary);
```

GDS는 isGds=true와 gdsAttribute를 사용합니다.
값 구조체는 type에 해당하는 필드만 읽습니다. 직접 필드를 변경할 경우 타입과 값의
일관성은 호출자가 책임집니다. 이 저장소는 파일 포맷 검증기를 대체하지 않습니다.

## 빌드

```bash
g++ -std=c++11 -Wall -Wextra -Wpedantic -Werror -pthread \
    property-usage.cc property-store.cc -o /tmp/property-example
/tmp/property-example
```

출력:
```text
net_name = VDD
```

## 계약과 한계

- Property가 없는 Shape/Cell은 연결 항목을 만들지 않습니다.
- 한 owner의 전체 목록을 한 번에 저장합니다. 중복 owner는 finish에서 거부합니다.
- finish 이후에는 저장/편집할 수 없습니다. 모든 worker join 이후 finish하고,
  그 이후 독자에게 전달합니다. 조회와 finish/파괴를 동시에 수행하지 않습니다.
- 조회 결과는 복사본이므로 변경해도 저장소는 바뀌지 않습니다.
- 이전 compact 버전과 달리 문자열·vector를 영구 보관합니다. 이름 공유도 없으며,
  메모리 사용량은 증가할 수 있습니다. **단순함 우선 버전이며 성능은 미측정입니다.**
- 실제 Multigon 코드 변경, SDK/Writer 연결, 파일 roundtrip은 하지 않았습니다.
- 이름 레코드 자체의 Property, 생략된 TEXT/X/NODE 속성, 편집/삭제는 미구현입니다.
- Multigon 원본 소스는 포함하지 않습니다. 이전 인코딩 버전은 Git 이력에 있습니다.

검증: strict C++11 예제/회귀 및 ASan+UBSan 실행, 실제 Multigon 헤더를 이용한
adapter 문법 검사. 회귀는 타입·정수 경계·binary NUL·순서·중복 이름·조회 복사본
수명·file/cell/shape 연결·finish 제한·병렬 저장을 확인합니다.
