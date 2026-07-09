# OASIS Repetition — 기하학적 의미

> 대상 소스: `src/oasis/oasis.h` (Repetition 클래스, RepetitionType 열거),
> `src/oasis/element.cc` (RepetitionElem::decode — 12종 복원)
> 프로젝트: Anuvad (IC 레이아웃 포맷 GDSII/OASIS 변환 C++ 라이브러리)
> 작성일: 2026-07-09
> 집중: Repetition의 기하학적 의미(도형의 규칙적 배열 / 위치 집합)와
>       12종 타입 + 4가지 저장 방식

---

## 1. 기하학적 의미

OASIS **REPETITION**은 하나의 기하 요소(폴리곤, 경로, 사다리꼴 등)를
**동일한 형태로 여러 위치에 반복 배치**할 때, 각 사본을 일일이 기술하지
않고 **위치 집합(Set of positions)** 을 compact하게 부호화하는 메커니즘입니다
(spec §7.6).

기하학적으로 Repetition은 다음을 정의합니다:

- **기준 요소**: 반복의 원형(prototype) — 이미 정의된 도형 1개.
- **변환 집합**: 원형을 어느 좌표로 평행이동(translation)할 것인지 규정.
  Repetition은 **회전·스케일 없이 평행이동만** 허용합니다.
- **위치 생성**: `(x, y)` 원형 좌표에 각 반복 오프셋 `dᵢ`를 더해
  `Pᵢ = (x + dᵢ.x, y + dᵢ.y)` 집합을 만듭니다.

```
원형 ⊗ Repetition  →  { P₀, P₁, ..., Pₙ₋₁ }
```

즉 Repetition은 **"도형을 복제할 위치들의 기하학적 패턴"** 입니다.

---

## 2. `RepetitionType` — 12종 기하 패턴

OASIS는 12종 반복 타입을 정의합니다(`oasis.h`)：

```cpp
enum RepetitionType {
    Rep_ReusePrevious   = 0,   // 이전 요소의 repetition 재사용
    Rep_Matrix          = 1,   // 축 정렬 격자 행렬 (x×y 배열)
    Rep_UniformX        = 2,   // 수평 균일 간격 1행
    Rep_UniformY        = 3,   // 수직 균일 간격 1열
    Rep_VaryingX        = 4,   // 수평, 간격 가변
    Rep_GridVaryingX    = 5,   // VaryingX + 그리드 정렬
    Rep_VaryingY        = 6,   // 수직, 간격 가변
    Rep_GridVaryingY    = 7,   // VaryingY + 그리드 정렬
    Rep_TiltedMatrix    = 8,   // 기울어질 수 있는 행렬 (임의 각도 축)
    Rep_Diagonal        = 9,   // 임의 각도 균일 간격 1줄
    Rep_Arbitrary       = 10,  // 완전 자유 위치
    Rep_GridArbitrary   = 11   // 자유 + 그리드 정렬
};
```

### 2.1 기하학 분류

| 타입 | 기하 패턴 | 차원 | 간격 |
|------|-----------|------|------|
| `ReusePrevious` | 이전과 동일 | — | — |
| `Matrix` | X-Y 축 직교 격자 | 2D | 균일 (xspace, yspace) |
| `UniformX` | 수평 1행 | 1D | 균일 xspace |
| `UniformY` | 수직 1열 | 1D | 균일 yspace |
| `VaryingX` | 수평 1행 | 1D | **가변** (각 위치별 오프셋) |
| `GridVaryingX` | VaryingX + 그리드 | 1D | 가변, 그리드 배수 |
| `VaryingY` | 수직 1열 | 1D | 가변 |
| `GridVaryingY` | VaryingY + 그리드 | 1D | 가변, 그리드 배수 |
| `TiltedMatrix` | 기울어진 격자 | 2D | 균일 (n축, m축 벡터) |
| `Diagonal` | 임의 각도 1줄 | 1D | 균일 (Δ 벡터) |
| `Arbitrary` | 자유 배치 | 임의 | 완전 자유 |
| `GridArbitrary` | 자유 + 그리드 | 임의 | 자유, 그리드 배수 |

---

## 3. 저장 방식 — 4가지 StorageType (12종 → 4종 병합)

코드 복잡도를 줄이기 위해 Anuvad는 12종을 **4가지 저장 타입**으로
통합합니다(`oasis.h` 주석):

```cpp
enum StorageType {
    StorNone,           // Rep_ReusePrevious
    StorMatrix,         // Rep_Matrix, Rep_TiltedMatrix
    StorUniformLine,    // Rep_UniformX, Rep_UniformY, Rep_Diagonal
    StorArbitrary       // Rep_VaryingX/Y, GridVaryingX/Y, Arbitrary, GridArbitrary
};
```

### 3.1 병합 관계 (기하학적 특수화)

- **StorMatrix**: `Matrix`는 `TiltedMatrix`의 특수형 — x-space를
  n-변위 `(xspace, 0)`, y-space를 m-변위 `(0, yspace)`로 취급.
  - `deltas[0]` = n-변위(한 행 방향), `deltas[1]` = m-변위(한 열 방향)
  - `ndimen(xdimen)` = n축 점 수, `mdimen(ydimen)` = m축 점 수
- **StorUniformLine**: `UniformX`는 `Diagonal`의 특수형(변위 `(xspace,0)`),
  `UniformY`는 `(0, yspace)`.
  - `deltas[0]` = 인접 점 간 변위, `dimen` = 점 수
- **StorArbitrary**: 나머지 6종. 각 쌍에서 첫째가 둘째의 `grid=1` 특수형.
  - `deltas[0]` = `(0,0)`, `deltas[j]` = j번째 점의 **절대 오프셋**
  - `grid` 배수로 오프셋이 주어짐(그리드 타입은 ×grid 필요)

---

## 4. 디코딩 코드 (상세)

`RepetitionElem::decode()`(`element.cc`)가 바이너리를 12종으로 복원합니다.

```cpp
Repetition
RepetitionElem::decode () const {
    if (repBlob.empty()) return Repetition();

    BlobReader  reader(repBlob.data, repBlob.data + repBlob.size);
    Ulong  repType = reader.readUInt();

    Repetition  rep;
    switch (static_cast<RepetitionType>(repType)) {
        case Rep_ReusePrevious:
            rep.makeReuse();
            break;

        case Rep_Matrix: {
            Ulong  xd = reader.readUInt();
            Ulong  yd = reader.readUInt();
            long   xs = reader.readSInt();
            long   ys = reader.readSInt();
            rep.makeMatrix(xd, yd, xs, ys);
            break;
        }

        case Rep_UniformX: {
            Ulong  dimen = reader.readUInt();
            long   space = reader.readSInt();
            rep.makeUniformX(dimen, space);
            break;
        }

        case Rep_UniformY: {
            Ulong  dimen = reader.readUInt();
            long   space = reader.readSInt();
            rep.makeUniformY(dimen, space);
            break;
        }

        case Rep_VaryingX: {
            Ulong  dimen = reader.readUInt();
            rep.makeVaryingX(dimen);
            for (++dimen; dimen != 0; --dimen)
                rep.addVaryingXoffset(reader.readSInt());
            break;
        }

        case Rep_GridVaryingX: {
            Ulong  dimen = reader.readUInt();
            long   grid  = reader.readSInt();
            rep.makeGridVaryingX(dimen, grid);
            for (++dimen; dimen != 0; --dimen)
                rep.addVaryingXoffset(reader.readSInt() * grid);
            break;
        }

        // ... VaryingY / GridVaryingY 는 X↔Y 대칭 ...

        case Rep_TiltedMatrix: {
            Ulong  nd = reader.readUInt();
            Ulong  md = reader.readUInt();
            Delta  ndisp = reader.readGDelta();
            Delta  mdisp = reader.readGDelta();
            rep.makeTiltedMatrix(nd, md, ndisp, mdisp);
            break;
        }

        case Rep_Diagonal: {
            Ulong  dimen = reader.readUInt();
            Delta  disp = reader.readGDelta();
            rep.makeDiagonal(dimen, disp);
            break;
        }

        case Rep_Arbitrary: {
            Ulong  dimen = reader.readUInt();
            rep.makeArbitrary(dimen);
            for (++dimen; dimen != 0; --dimen)
                rep.addDelta(reader.readGDelta());
            break;
        }

        case Rep_GridArbitrary: {
            Ulong  dimen = reader.readUInt();
            long   grid  = reader.readSInt();
            rep.makeGridArbitrary(dimen, grid);
            for (++dimen; dimen != 0; --dimen)
                rep.addDelta(reader.readGDelta());   // 각 Delta는 grid 배수
            break;
        }
    }
    return rep;
}
```

### 4.1 단계별 설명

1. **repType 읽기**: 맨 앞 varint가 12종 중 하나.
2. **타입별 필드**:
   - `Matrix`: `xd, yd`(차원) + `xs, ys`(간격) → 직교 격자.
   - `UniformX/Y`: `dimen` + `space` → 1D 균일.
   - `VaryingX/Y`: `dimen` 후 각 점 오프셋을 개별 읽기 → 1D 가변.
   - `GridVarying*`: 오프셋에 `grid` 곱셈 → 그리드 정렬.
   - `TiltedMatrix`: `nd, md` + 두 `Delta`(n축, m축 벡터) → 기울어진 격자.
   - `Diagonal`: `dimen` + `Delta`(방향·간격 벡터) → 임의 각도 1줄.
   - `Arbitrary/GridArbitrary`: `dimen` 후 각 점 위치를 `GDelta`로 읽기.

---

## 5. 위치 생성 (기하학적 적용)

복원된 `Repetition`을 원형 `(x,y)`에 적용해 실제 사본 좌표를 만듭니다:

```
Matrix (xd×yd):      P(i,j) = (x,y) + i·(xs,0) + j·(0,ys)
TiltedMatrix:        P(i,j) = (x,y) + i·ndisp + j·mdisp
UniformX (dimen):    P(k)   = (x,y) + k·(space,0)
Diagonal:            P(k)   = (x,y) + k·disp
Arbitrary:           P(k)   = (x,y) + deltas[k]      (절대 오프셋)
```

각 `Pᵢ`에 원형 도형을 평행이동 복제하면 전체 배열이 완성됩니다.

---

## 6. 예시

**Matrix**: `xd=3, yd=2, xs=10, ys=20`, 원형 `(0,0)`:

```
(0,0)  (10,0)  (20,0)
(0,20) (10,20) (20,20)
→ 3×2 = 6개 사본, 직교 격자
```

**TiltedMatrix**: `nd=2, md=2, ndisp=(10,5), mdisp=(-5,10)`:

```
(0,0)
(10,5)              (5,10)
(20,10)             (15,20)
→ 45°-ish 기울어진 2×2 격자
```

---

## 7. 요약

- **Repetition**은 도형을 평행이동 복제할 **위치 집합의 기하 패턴**을
  compact하게 부호화(spec §7.6). 회전/스케일 없음.
- **12종 타입**: 축 격자(Matrix), 1D 균일(UniformX/Y), 1D 가변(Varying*),
  기울어진 격자(TiltedMatrix), 임의 각도(Diagonal), 자유(Arbitrary) 등.
- Anuvad는 이를 **4가지 StorageType**으로 병합(특수형을 일반형의 부분집합으로
  취급)해 코드 단순화.
- 디코딩은 `RepetitionElem::decode()`가 12종을 복원하며, 위치는 원형
  `(x,y)`에 오프셋을 더해 생성.

---

## 참고: 관련 소스 위치

- `src/oasis/oasis.h` — `Repetition` 클래스, `RepetitionType`, `StorageType`,
  make* 메서드 시그니처
- `src/oasis/element.h` — `RepetitionElem` (`decode` 선언, `repBlob`)
- `src/oasis/element.cc` — `RepetitionElem::decode()` 구현 (위 인용),
  `BlobReader`, `readGDelta`
