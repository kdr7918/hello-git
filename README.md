# C++11 Fast Text Parser

C++11만 사용한 **고속 줄 단위 텍스트 파서**입니다. 일반 파일에서는 `mmap + memchr`가 기본 고속 경로이고, 이식성/`FILE*` 입력이 필요하면 `ReadBuffer(fread) + memchr`를 사용합니다.

핵심 목표는 다음과 같습니다.

- 줄마다 `std::string`을 만들지 않는 zero-copy 파싱
- `previous / current / next` 세 줄을 동시에 참조
- 모든 줄과 토큰의 **절대 byte offset** 제공
- 저장해 둔 offset으로 `seek()` 후 해당 줄부터 즉시 재파싱
- C++11 호환
- hot path에서 가상 함수, iostream, locale, 정규식 사용 회피

## 구성

```text
include/fast_text_parser.hpp   header-only 라이브러리
examples/parse_file.cpp        이전/다음 줄과 offset 사용 예제
tests/test_fast_text_parser.cpp
benchmark/benchmark.cpp        mmap/ReadBuffer/std::getline 비교
```

## 빠른 시작

```cpp
#include "fast_text_parser.hpp"

#include <iostream>
#include <string>

int main() {
    fasttext::MappedFile file;
    std::string error;
    if (!file.open("input.txt", &error)) return 1;

    fasttext::TextParser parser(file);
    fasttext::LineWindow lines;
    while (parser.next(&lines)) {
        if (fasttext::contains_word(lines.current.text,
                                    fasttext::StringView("ERROR"))) {
            std::cout << "line=" << lines.current.number
                      << " offset=" << lines.current.begin_offset << "\n";

            if (lines.has_previous)
                std::cout << lines.previous.text.to_string() << "\n";
            std::cout << lines.current.text.to_string() << "\n";
            if (lines.has_next)
                std::cout << lines.next.text.to_string() << "\n";
        }
    }
}
```

직접 컴파일:

```bash
g++ -std=c++11 -O3 -DNDEBUG -Iinclude examples/parse_file.cpp -o parse_file
./parse_file input.txt
```

CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/fast_text_parser_benchmark
```

## 입력 backend

### `MappedFile` — regular file 권장

- POSIX에서는 read-only `mmap` 사용
- 파일 전체 복사 없음
- `StringView`가 mapping을 직접 참조
- Windows/non-POSIX에서는 자동으로 `ReadBuffer` fallback
- `MappedFile`은 `TextParser`와 모든 `StringView`보다 오래 살아 있어야 함
- mapping 사용 중 원본 파일을 truncate/교체하면 POSIX에서 `SIGBUS`가 발생할 수 있으므로 파일을 변경하지 않아야 함

```cpp
fasttext::MappedFile file;
file.open(path, &error);
fasttext::TextParser parser(file);
```

### `ReadBuffer` — `fread` 기반 이식 경로

- 1 MiB block 단위 `fread`
- 한 번 읽은 데이터는 연속 `std::vector<char>`에 유지
- 파싱 중 refill/memmove가 없어 모든 view와 offset이 안정적
- 파일 전체를 RAM에 올리므로 매우 큰 regular file에는 `MappedFile` 권장

```cpp
fasttext::ReadBuffer buffer;
buffer.load_file(path, &error);
fasttext::TextParser parser(buffer);
```

이미 메모리에 있는 데이터도 바로 파싱할 수 있습니다.

```cpp
fasttext::TextParser parser(data, size);
```

## 줄 window와 seek용 offset

각 `Line`은 다음 값을 제공합니다.

- `text`: CRLF의 `\r`을 제거한 zero-copy 내용
- `number`: 1부터 시작하는 줄 번호
- `begin_offset`: 줄 첫 byte의 절대 offset
- `end_offset`: 논리적 줄 끝 (`\r`, `\n` 제외)
- `next_offset`: 다음 줄 시작 offset

```cpp
std::size_t resume = lines.current.next_offset;
// ... 나중에
parser.seek(resume);
parser.next(&lines);  // 저장했던 다음 줄
```

`seek(offset)`은 임의 offset이 줄 중간이어도 그 offset을 포함하는 줄의 시작으로 맞춥니다. 줄 번호 복구를 위해 seek 시 앞부분의 newline을 세므로, 반복적인 랜덤 seek가 필요하면 `begin_offset`/`next_offset`을 저장해 사용하는 것이 좋습니다.

`TextParser`는 다음 줄을 한 번만 `memchr`로 찾고 look-ahead 결과를 캐시합니다. 따라서 `previous/current/next`를 제공하면서도 순차 hot path에서 줄을 반복 스캔하지 않습니다.

## 숫자·문자 읽기

`Cursor`는 `[pointer, length]` 범위에서 동작하며 정수 파싱에 allocation/예외/locale을 사용하지 않습니다.

```cpp
fasttext::Cursor cursor(lines.current.text);
cursor.skip_spaces();

std::int64_t id;
if (cursor.read_int64(&id)) {
    cursor.skip_spaces();
    fasttext::StringView name = cursor.read_alpha();
}
```

제공 함수:

- `read_int64()` — overflow 검사 포함
- `read_uint64()` — overflow 검사 포함
- `read_double()` — bounded token을 `strtod`로 변환
- `read_alpha()` — ASCII `A-Z/a-z`
- `read_while_digits()`
- `read_word()` — 공백 전까지
- `read_identifier()` — ASCII identifier
- `read_char()`
- `skip_spaces()`
- `position()`, `absolute_offset()`, `remaining()`

`StringView` 자체에서도 token 전체를 바로 검사하고 값으로 바꿀 수 있습니다.

```cpp
fasttext::StringView token("-3.125e2");

bool alphabet_only = token.is_alpha();   // 비어 있지 않은 ASCII 알파벳만
bool integer_syntax = token.is_integer(); // 부호 + 십진 정수 문법
bool numeric = token.is_number();         // token 전체가 변환 가능한 숫자

double value;
if (token.to_double(&value)) {
    // value == -312.5
}

std::int64_t integer;
token.to_int64(&integer);                 // overflow 검사
std::uint64_t unsigned_integer;
token.to_uint64(&unsigned_integer);       // overflow 검사

// 변환 실패 시 지정한 fallback을 즉시 반환
std::int64_t id = token.int64_value(-1);
std::uint64_t unsigned_id = token.uint64_value(0);
double number = token.number_value(0.0);
```

- `is_alpha()`는 빈 문자열과 숫자/기호/비ASCII 문자를 거부합니다.
- `is_integer()`는 문법만 검사하며 값 범위는 검사하지 않습니다.
- `to_int64()`/`to_uint64()`는 전체 token과 범위를 모두 검사합니다.
- 변환 실패 시 output 인자를 변경하지 않습니다.
- 앞뒤 공백은 자동 허용하지 않으므로 필요하면 먼저 `trim()`을 사용합니다.

`read_double()`과 `StringView::to_double()`은 편의를 위한 정확성 우선 경로입니다. 지원되는 POSIX/Windows에서는 process-lifetime C numeric locale과 `strtod_l`/`_strtod_l`을 사용하고, 그 외 플랫폼에서는 `std::locale::classic()` fallback을 사용합니다. 따라서 프로세스의 `LC_NUMERIC` 설정과 무관하게 `.`을 소수점으로 해석합니다. overflow와 0으로 소실되는 underflow는 모든 backend에서 실패로 처리하며 output을 변경하지 않습니다. 부동소수 변환이 병목이면 C++11 호환 `fast_float` 같은 bounded parser를 별도 backend로 연결하는 것이 좋습니다.

## split / find / word / regex

```cpp
fasttext::StringView line = lines.current.text;

std::size_t colon = fasttext::find_char(line, ':');       // memchr
std::size_t pos = fasttext::find(line, fasttext::StringView("ERROR"));
bool word = fasttext::contains_word(line, fasttext::StringView("ERROR"));

std::vector<fasttext::StringView> fields = fasttext::split(line, ',');

// allocation 없는 split hot path
fasttext::split_each(line, ',', [](fasttext::StringView field) {
    field = fasttext::trim(field);
});
```

정규식은 미리 컴파일한 `std::regex`를 전달합니다.

```cpp
const std::regex re("ERROR-([0-9]+)",
                    std::regex::ECMAScript | std::regex::optimize);
fasttext::Match match;
if (fasttext::regex_find(line, re, &match)) {
    // match.offset은 파일 기준 절대 offset
}
```

> `std::regex`는 매우 느릴 수 있습니다. 단순 문자/문자열/단어 검색은 `find_char`, `find`, `contains_word`를 우선 사용하고, regex 객체는 줄마다 생성하지 마십시오.

## 성능 설계

- `memchr` 사용: 현대 libc의 SIMD 최적화를 활용
- zero-copy `StringView`
- sequential parser는 한 줄당 newline scan 1회
- look-ahead line cache로 `next` 재스캔 제거
- compile-time/header-only 구조, virtual dispatch 없음
- 정수 parser는 bounded pointer loop와 명시적 overflow 검사
- regular file은 `mmap`, 범용 입력은 `fread`

벤치마크는 동일 checksum과 line count를 검증한 뒤 결과를 출력합니다. 기본 생성 파일은 약 100 MiB이고 warm page-cache 조건입니다. 숫자는 CPU/libc/storage에 따라 달라지므로 대상 장비에서 직접 측정하십시오.

이 구현을 작성한 ARM64 Linux 장비, GCC 13.3, 106 MB/2,000,000줄 Release 예시:

```text
mmap + memchr + LineWindow    4.72 GiB/s
fread ReadBuffer + memchr     0.86 GiB/s  (파일 전체 load 포함)
std::getline baseline         2.22 GiB/s
```

이 수치는 warm-cache 단일 실행의 참고값이며 보편적인 성능 보장은 아닙니다. benchmark는 LF뿐 아니라 별도 CRLF/빈 줄 데이터에서도 세 구현의 line count와 offset checksum 일치를 검증했습니다.

```bash
./build/fast_text_parser_benchmark
./build/fast_text_parser_benchmark /path/to/real/data.txt
```

## 입력 규칙 및 lifetime

- LF와 CRLF 지원
- 마지막 newline 없는 파일 지원
- 마지막 newline 뒤에 가상의 빈 줄을 추가하지 않음 (`std::getline`과 같은 동작)
- embedded NUL도 일반 byte로 유지
- `StringView`는 소유하지 않음
- 원본 `std::string`, `ReadBuffer`, `MappedFile`을 파서/view보다 먼저 파괴하거나 변경하면 안 됨
- `MappedFile::close()` 후 기존 view 사용 금지
- regex 문법 오류는 `std::regex` 생성 시 예외가 발생할 수 있음

## 라이선스

MIT
