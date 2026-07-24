# Python 로그 6-케이스·유저별 집계 예제

다음 구조의 로그를 날짜 범위로 조회합니다.

```text
기준폴더/
├── server-a/
│   ├── 20260101/
│   │   ├── cmd_kim_090000.log
│   │   └── cmd_lee_100000.log
│   └── 20260102/
│       └── cmd_park_110000.log
└── server-b/
    └── 20260101/
        └── cmd_choi_120000.log
```

각 `cmd_유저명_HHMMSS.log` 파일의 **4번째 줄만** 검사합니다.

## 분석하는 6개 케이스

`-laypop_title SALT-Workbench` 포함 여부 2가지와 명령 유형 3가지를 조합합니다.

1. SALT-Workbench 있음 / Python
2. SALT-Workbench 있음 / Batch
3. SALT-Workbench 있음 / Python·Batch 없음
4. SALT-Workbench 없음 / Python
5. SALT-Workbench 없음 / Batch
6. SALT-Workbench 없음 / Python·Batch 없음

명령 유형은 다음 문자열로 판정합니다.

- Python: `-python` 포함
- Batch: `-batch` 포함
- 없음: `-python`과 `-batch`가 모두 없음

한 로그에 `-python`과 `-batch`가 모두 있으면 동일한 SALT 그룹의 Python과 Batch에 각각 1건씩 집계합니다.

각 케이스마다 다음 정보를 출력합니다.

- 전체 작업(job) 수: 로그 파일 1개를 작업 1건으로 계산
- 해당 케이스의 작업 수가 많은 유저 상위 10명
- 상위 유저별 작업 수

## 집계 규칙

- 시작일과 종료일을 모두 포함합니다.
- 키워드는 대소문자를 구분하는 부분 문자열로 비교합니다.
- 같은 키워드가 4번째 줄에 여러 번 있어도 해당 파일에서는 1건으로 셉니다.
- 파일명에서 마지막 `_HHMMSS.log` 앞부분을 유저명으로 사용하므로 유저명에 밑줄도 사용할 수 있습니다.
- 4줄 미만인 파일, 잘못된 날짜/시간 이름, 형식에 맞지 않는 파일은 제외합니다.
- 서버·날짜·로그 심볼릭 링크와 특수 파일은 제외합니다.
- 로그는 UTF-8로 읽으며 잘못된 바이트는 대체 문자로 처리합니다.
- 작업 수가 같으면 유저명 오름차순으로 순위를 정합니다.

## 실행

Python 3.10 이상이 필요하며 외부 패키지는 사용하지 않습니다.

```bash
cd examples/python_log_keyword_counter
python count_log_keywords.py /data/logs 20260101 20260131
```

출력 예시:

```text
[SALT-Workbench 있음 / Python] 전체 작업: 12건
   1. kim: 7건
   2. lee: 5건

[SALT-Workbench 있음 / Batch] 전체 작업: 9건
   1. park: 6건
   2. kim: 3건

[SALT-Workbench 있음 / 없음] 전체 작업: 3건
   1. choi: 2건
   2. park: 1건

[SALT-Workbench 없음 / Python] 전체 작업: 8건
   1. lee: 8건

[SALT-Workbench 없음 / Batch] 전체 작업: 5건
   1. kim: 5건

[SALT-Workbench 없음 / 없음] 전체 작업: 2건
   1. han: 2건
```

## 코드에서 사용

```python
from count_log_keywords import analyze_logs

result = analyze_logs(
    root="/data/logs",
    start_date="20260101",
    end_date="20260131",
    top_limit=10,
)

for category, statistics in result.items():
    print(category, statistics["total_jobs"])
    for user_name, job_count in statistics["top_users"]:
        print(user_name, job_count)
```

## 테스트

```bash
cd examples/python_log_keyword_counter
python -m unittest -v test_count_log_keywords.py
```
