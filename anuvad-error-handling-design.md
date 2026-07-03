# Anuvad Error 처리 설계 정리

대상 프로젝트: `anuvad` v0.10 — IC 레이아웃 파일 포맷(GDSII/OASIS) 변환 C++ 라이브러리 및 도구 모음.

## 1. 전체 원칙

이 프로젝트의 Error 처리 설계는 다음 구조를 따른다.

```text
라이브러리 내부: 예외 throw
CLI 최상단: std::exception catch
사용자 출력: FatalError()
프로세스 종료: exit(1)
경고: WarningHandler callback
```

즉, 라이브러리 내부는 직접 `exit()`하지 않고 `std::runtime_error`, `std::overflow_error`, `std::bad_alloc` 같은 예외를 사용한다. 실행 파일의 `main()` 경계에서 예외를 catch하고, 사용자에게 메시지를 출력한 뒤 종료한다.

핵심 유틸리티 위치:

```text
src/misc/utils.h
src/misc/utils.cc
```

---

## 2. 공통 Error 출력 API

### `Error(...)`

```cpp
void Error(const char* fmt, ...);
```

역할:

- `stderr`에 메시지 출력
- 프로세스는 종료하지 않음
- `SetProgramName(argv[0])`가 호출되어 있으면 프로그램 이름을 prefix로 붙임

출력 형태:

```text
program-name: message
```

주요 사용처:

- warning callback 구현
- 복구 가능한 파일별 오류 메시지
- 계속 진행 가능한 분석/통계 도구

예:

```cpp
static void
DisplayWarning(const char* msg) {
    Error("%s", msg);
}
```

### `FatalError(...)`

```cpp
void FatalError(const char* fmt, ...) NORETURN;
```

역할:

- `stderr`에 메시지 출력
- `exit(1)` 호출
- CLI 최상단의 최종 실패 처리용

대표 패턴:

```cpp
try {
    ...
}
catch (const std::exception& exc) {
    FatalError("%s", exc.what());
}
```

### `ThrowRuntimeError(...)`

```cpp
void ThrowRuntimeError(const char* fmt, ...) NORETURN;
```

역할:

- printf-style 포맷으로 메시지 생성
- `std::runtime_error` throw
- 라이브러리 내부의 복구 불가능 오류 전달용

구현 특징:

- 내부 버퍼 크기는 256 byte
- `VSNprintf()`로 메시지 포맷
- `exception::what()`으로 포맷된 메시지 회수 가능

---

## 3. 경고 처리: `WarningHandler`

경고는 전역 stderr 출력이 아니라 callback 주입 방식이다.

위치:

```text
src/misc/utils.h
```

정의:

```cpp
typedef boost::function<void (const char*)> WarningHandler;
```

동작 흐름:

```text
CLI DisplayWarning()
    ↓
Error("%s", msg)
    ↓
Parser / Converter 생성자에 WarningHandler 전달
    ↓
내부 warn()에서 필요 시 callback 호출
```

OASIS parser 예:

```cpp
if (warnHandler != nullptr)
    warnHandler(msgbuf);
```

설계 의도:

- 라이브러리는 출력 방식을 강제하지 않음
- CLI, GUI, embedding 환경에서 warning 표시 방식을 바꿀 수 있음
- `nullptr`이면 warning을 무시할 수 있음
- warning과 fatal error를 명확히 분리

---

## 4. CLI 경계 설계

실행 파일들은 보통 다음 패턴을 사용한다.

```cpp
SetProgramName(argv[0]);

try {
    // parser / converter / validator 실행
}
catch (const std::exception& exc) {
    FatalError("%s", exc.what());
}
```

대표 파일:

```text
src/oasis/oasis-validate.cc
src/oasis/oasis-print.cc
src/oasis/ascii2oasis.cc
src/gdsii/ascii2gds.cc
src/gdsii/gds2ascii.cc
src/gdsii/gds-recstats.cc
src/conv/gds2oasis.cc
```

CLI 경계에서만 `FatalError()`를 쓰는 구조라서, 라이브러리 API를 다른 프로그램에 embedding할 때는 호출자가 예외를 직접 처리할 수 있다.

---

## 5. Scanner / Parser 오류 설계

Scanner와 Parser는 직접 `ThrowRuntimeError()`를 호출하기보다, 각 계층에 `abortXxx()` helper를 둔다.

목적:

- 파일명 포함
- byte offset 포함
- record name / record ID 포함
- CBLOCK 내부 offset 포함
- ASCII 입력에서는 line number 포함

이 구조 덕분에 오류 메시지가 단순한 `failed`가 아니라, 사용자가 파일의 어느 위치에서 문제가 났는지 알 수 있다.

---

## 6. GDSII Scanner 오류

위치:

```text
src/gdsii/scanner.h
src/gdsii/scanner.cc
```

문서화된 정책:

```cpp
// Any of the methods may throw runtime_error.
// The constructor may also throw bad_alloc.
```

내부 helper:

```cpp
GdsScanner::abortScanner(...)
```

메시지 형태:

```text
file 'foo.gds', offset 1234: <reason>
```

예:

```cpp
abortScanner("%s record body has invalid length %u: must be a multiple of %u",
             rti->name, length, sizeUnit);
```

GDSII scanner는 binary record 단위로 읽기 때문에 byte offset 중심의 context를 붙인다.

---

## 7. OASIS Scanner 오류

위치:

```text
src/oasis/scanner.cc
```

내부 helper:

```cpp
OasisScanner::abortScanner(...)
```

대표 오류:

- OASIS magic 불일치
- unexpected EOF
- `read()` 실패
- `mmap()` 실패
- `lseek()` 실패
- seek offset 범위 초과
- invalid compression type
- CBLOCK 중첩
- validation signature 불일치
- 잘못된 real encoding

예:

```cpp
abortScanner("this is not an OASIS file");
abortScanner("unexpected EOF");
abortScanner("invalid compression type %lu", compType);
abortScanner("mmap failed: %s", strerror(errno));
```

`OASIS_SCANNER_USE_MMAP=1` 경로에서도 동일하게 `abortScanner()`를 사용한다. 따라서 read path와 mmap path의 Error semantics는 동일하게 유지된다.

---

## 8. OASIS Parser 오류

위치:

```text
src/oasis/parser.cc
```

핵심 helper:

```cpp
ParserImpl::abortParser(...)
ParserImpl::warn(...)
ParserImpl::formatMessage(...)
```

오류 메시지 형태:

```text
file 'foo.oas', START(1) record at offset NNN: <reason>
```

CBLOCK 내부 record라면 다음처럼 표현된다.

```text
file 'foo.oas', RECTANGLE(...) record at uncompressed offset NNN in CBLOCK at offset MMM: <reason>
```

설계상 장점:

- 현재 record 기준으로 오류 원인을 설명
- compressed CBLOCK 내부 위치까지 보존
- parser 상위 계층은 `runtime_error`만 catch해도 충분
- warning도 같은 context formatting을 사용하되 `warning: ` prefix를 붙임

관련 코드:

```cpp
ParserImpl::abortParser(...)
{
    formatMessage(msgbuf, sizeof msgbuf, "", fmt, ap);
    ThrowRuntimeError("%s", msgbuf);
}

ParserImpl::warn(...)
{
    formatMessage(msgbuf, sizeof msgbuf, "warning: ", fmt, ap);
    if (warnHandler != nullptr)
        warnHandler(msgbuf);
}
```

---

## 9. ASCII OASIS Reader 오류

위치:

```text
src/oasis/asc-recreader.cc
```

내부 helper:

```cpp
AsciiRecordReader::abortReader(...)
```

메시지 형태:

```text
file 'foo.ascoas', <record> record at line N: <reason>
```

예:

```cpp
abortReader("field '%s' appears twice", GetKeywordName(keyword));
abortReader("field '%s' is not legal for this record", GetKeywordName(keyword));
```

ASCII 입력은 binary offset보다 사용자가 직접 찾기 쉬운 line number를 중심으로 context를 구성한다.

---

## 10. Writer 오류 설계

Writer 계층은 I/O 실패 시 `runtime_error`를 던진다.

예: GDSII ASCII writer

```text
src/gdsii/asc-writer.cc
```

내부 helper:

```cpp
AsciiWriter::abortWriter()
```

메시지 형태:

```text
file 'out.asc': write failed: <strerror(errno)>
```

중요한 설계 규칙:

```cpp
// Close the file here rather than in the destructor so that we can
// throw an exception if close fails. Destructors must not throw.
```

즉:

- 명시적 `close()` / `endFile()` 류 public method에서 오류 throw
- destructor에서는 throw하지 않음
- exception safety를 고려한 구조

---

## 11. zlib / CBLOCK 오류 설계

위치:

```text
src/oasis/compressor.h
src/oasis/compressor.cc
```

zlib 오류는 프로젝트 공통 exception 체계로 변환된다.

예:

```cpp
ThrowRuntimeError("%szlib error %d while decompressing CBLOCK: %s",
                  context.c_str(), zstat, msg);
```

특징:

- compressor/decompressor 생성 시 context string을 보관
- parser/scanner가 만든 파일/offset context를 zlib 오류 prefix로 사용
- zlib return code와 zlib message를 함께 포함

즉 low-level zlib 오류도 최종 사용자에게는 파일 위치가 붙은 parser 오류처럼 보인다.

---

## 12. Overflow 처리

정수 overflow는 `std::overflow_error`로 분리된다.

위치:

```text
src/misc/arith.h
src/misc/arith.cc
```

대표 API:

```cpp
CheckedPlus()
CheckedMinus()
CheckedMult()
IntegerOverflow()
```

오류 메시지:

```text
integer overflow while computing X + Y
```

사용 대상:

- geometry 계산
- OASIS delta / repetition
- writer geometry encoding
- 좌표 변환과 반복 계산

의미상 분류:

```text
포맷/파일 오류 → std::runtime_error
산술 범위 초과 → std::overflow_error
메모리 부족 → std::bad_alloc
```

CLI 최상단에서는 보통 `std::exception`으로 통합 catch한다.

---

## 13. 도메인 특수 예외

### `BadTrapezoidError`

위치:

```text
src/oasis/trapezoid.h
```

정의:

```cpp
struct BadTrapezoidError { };
```

역할:

- OASIS trapezoid 생성 인자가 유효하지 않을 때 사용
- `runtime_error`보다 가벼운 domain-specific exception
- 내부 geometry/trapezoid 변환 로직에서 제어 흐름과 오류 표현을 겸함

관련 주석:

```cpp
/* throw (BadTrapezoidError) */
/* throw (BadTrapezoidError, overflow_error) */
```

---

## 14. `assert()` 사용 정책

`assert()`는 사용자 입력 오류 처리용이 아니라 내부 불변식 검증용이다.

예:

```cpp
assert(currRecord != nullptr);
assert(rep != nullptr);
assert(!readingFromCblock());
```

정책:

```text
파일/사용자 입력이 잘못됨 → runtime_error
산술 범위 초과 → overflow_error
메모리 부족 → bad_alloc
내부 상태 불변식 위반 → assert
```

release build는 기본적으로 `-DNDEBUG`가 포함되어 assert가 제거된다. 따라서 새 코드에서 사용자 입력 검증을 `assert()`로 처리하면 안 된다.

---

## 15. Error 처리 계층도

```text
[CLI main]
    SetProgramName(argv[0])
    try/catch std::exception
    FatalError(exc.what())
        ↓

[Public library API]
    runtime_error / overflow_error / bad_alloc throw
    WarningHandler callback 전달
        ↓

[Parser / Scanner / Writer]
    abortParser / abortScanner / abortReader / abortWriter
    파일명 + offset/line/record/CBLOCK context 추가
    ThrowRuntimeError()
        ↓

[misc utils]
    Error()              stderr 출력, 계속 진행
    FatalError()         stderr 출력 후 exit(1)
    ThrowRuntimeError()  formatted runtime_error throw
        ↓

[Low-level helpers]
    CheckedPlus/Minus/Mult → overflow_error
    zlib error             → runtime_error
    OpenFile/ReadNBytes    → runtime_error 계열
```

---

## 16. 새 코드 작성 규칙

해야 할 것:

- 라이브러리 내부 오류는 `ThrowRuntimeError()` 또는 typed exception 사용
- 파일/record/offset context가 필요한 계층은 `abortXxx()` helper를 만들어 context 포함
- warning은 `WarningHandler`로 전달
- CLI `main()` 경계에서만 `FatalError()`로 종료
- destructor에서는 throw하지 않기
- arithmetic overflow 가능성이 있으면 `CheckedPlus()`, `CheckedMinus()`, `CheckedMult()` 사용

피해야 할 것:

- 라이브러리 내부에서 `exit()` 호출
- parser/scanner 내부에서 직접 `fprintf(stderr, ...)` 호출
- 사용자 입력 오류를 `assert()`로 처리
- warning을 exception으로 승격
- context 없는 `throw std::runtime_error("failed")`

---

## 17. 한 줄 요약

> 라이브러리는 context-rich exception을 던지고, CLI는 최상단에서 이를 사용자 메시지로 변환하며, recoverable warning은 callback으로 분리한다.

특히 parser/scanner 계층은 IC layout 파일 특성상 파일명, byte offset, record name, CBLOCK offset을 오류 메시지에 포함하는 방향으로 설계되어 있다.
