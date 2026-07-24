#!/usr/bin/env python3
"""로그 4번째 줄의 명령 옵션별 작업 수와 상위 사용자를 집계한다."""

import argparse
import os
import re
import stat
from collections import Counter
from datetime import date, datetime
from pathlib import Path
from typing import Optional, Union

SALT_KEYWORD = "-laypop_title SALT-Workbench"
PYTHON_KEYWORD = "-python"
BATCH_KEYWORD = "-batch"
CATEGORY_LABELS = (
    "SALT-Workbench 있음 / Python",
    "SALT-Workbench 있음 / Batch",
    "SALT-Workbench 있음 / 없음",
    "SALT-Workbench 없음 / Python",
    "SALT-Workbench 없음 / Batch",
    "SALT-Workbench 없음 / 없음",
)


def parse_date(value: str) -> date:
    """YYYYMMDD 문자열을 비교 가능한 date로 변환한다."""
    if len(value) != 8 or not value.isdigit():
        raise ValueError(f"날짜는 YYYYMMDD 형식이어야 합니다: {value}")
    try:
        return datetime.strptime(value, "%Y%m%d").date()
    except ValueError as exc:
        raise ValueError(f"유효하지 않은 날짜입니다: {value}") from exc


def parse_log_filename(name: str) -> Optional[str]:
    """cmd_YYYYMMDD_HHMMSS_유저_PID.log에서 유저명을 반환한다."""
    match = re.fullmatch(r"cmd_(\d{8})_(\d{6})_(.+)_(\d+)\.log", name)
    if match is None:
        return None
    try:
        parse_date(match.group(1))
        datetime.strptime(match.group(2), "%H%M%S")
    except ValueError:
        return None
    return match.group(3)


def directory_open_flags() -> int:
    return (
        os.O_RDONLY
        | getattr(os, "O_DIRECTORY", 0)
        | getattr(os, "O_CLOEXEC", 0)
        | getattr(os, "O_NOFOLLOW", 0)
    )


def open_directory(
    path: Union[str, Path], *, dir_fd: Optional[int] = None
) -> int:
    """심볼릭 링크를 따라가지 않고 디렉터리를 연다."""
    return os.open(path, directory_open_flags(), dir_fd=dir_fd)


def read_fourth_line(dir_fd: int, name: str) -> Optional[str]:
    """dir_fd 아래 일반 파일의 4번째 줄을 반환한다."""
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    file_descriptor = os.open(name, flags, dir_fd=dir_fd)
    try:
        if not stat.S_ISREG(os.fstat(file_descriptor).st_mode):
            return None

        log_file = os.fdopen(
            file_descriptor,
            "r",
            encoding="utf-8",
            errors="replace",
        )
        file_descriptor = -1  # 소유권이 log_file로 이전됨
        with log_file:
            for line_number, line in enumerate(log_file, start=1):
                if line_number == 4:
                    return line.rstrip("\r\n")
        return None
    finally:
        if file_descriptor >= 0:
            os.close(file_descriptor)


def _open_child_directory(parent_fd: int, name: str) -> Optional[int]:
    try:
        return open_directory(name, dir_fd=parent_fd)
    except OSError:
        # 심볼릭 링크, 일반 파일, 삭제 경쟁 등은 스캔 대상이 아니다.
        return None


def analyze_logs(
    root: Union[str, Path],
    start_date: str,
    end_date: str,
    top_limit: int = 10,
):
    """SALT 사용 여부 × Python/Batch/없음의 6개 분석 결과를 반환한다.

    예상 경로는
    ``root/서버명/YYYYMMDD/cmd_YYYYMMDD_HHMMSS_유저_PID.log``이다.
    날짜 범위는 양 끝을 포함한다. Python과 Batch가 모두 있으면 같은
    SALT 그룹의 Python과 Batch에 각각 한 건씩 집계한다.
    """
    start = parse_date(start_date)
    end = parse_date(end_date)
    if start > end:
        raise ValueError("시작일은 종료일보다 늦을 수 없습니다.")

    if top_limit < 1:
        raise ValueError("상위 유저 수는 1 이상이어야 합니다.")

    user_counts = {category: Counter() for category in CATEGORY_LABELS}

    root_path = Path(root)
    try:
        root_fd = open_directory(root_path)
    except FileNotFoundError as exc:
        raise FileNotFoundError(f"기준 폴더를 찾을 수 없습니다: {root_path}") from exc

    try:
        for server_name in os.listdir(root_fd):
            server_fd = _open_child_directory(root_fd, server_name)
            if server_fd is None:
                continue
            try:
                for date_name in os.listdir(server_fd):
                    try:
                        log_date = parse_date(date_name)
                    except ValueError:
                        continue
                    if not start <= log_date <= end:
                        continue

                    date_fd = _open_child_directory(server_fd, date_name)
                    if date_fd is None:
                        continue
                    try:
                        for log_name in os.listdir(date_fd):
                            user_name = parse_log_filename(log_name)
                            if user_name is None:
                                continue
                            try:
                                fourth_line = read_fourth_line(date_fd, log_name)
                            except OSError:
                                # 링크·특수 파일·스캔 중 삭제된 파일은 제외한다.
                                continue
                            if fourth_line is None:
                                continue

                            salt_case = (
                                "SALT-Workbench 있음"
                                if SALT_KEYWORD in fourth_line
                                else "SALT-Workbench 없음"
                            )
                            has_python = PYTHON_KEYWORD in fourth_line
                            has_batch = BATCH_KEYWORD in fourth_line

                            if has_python:
                                user_counts[f"{salt_case} / Python"][user_name] += 1
                            if has_batch:
                                user_counts[f"{salt_case} / Batch"][user_name] += 1
                            if not has_python and not has_batch:
                                user_counts[f"{salt_case} / 없음"][user_name] += 1
                    finally:
                        os.close(date_fd)
            finally:
                os.close(server_fd)
    finally:
        os.close(root_fd)

    result = {}
    for category, counts in user_counts.items():
        top_users = sorted(counts.items(), key=lambda item: (-item[1], item[0]))[
            :top_limit
        ]
        result[category] = {
            "total_jobs": sum(counts.values()),
            "top_users": top_users,
        }
    return result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="로그 4번째 줄의 명령 옵션별 작업 수와 상위 유저 10명을 집계합니다."
    )
    parser.add_argument("root", type=Path, help="서버명 폴더들이 들어 있는 기준 폴더")
    parser.add_argument("start_date", help="시작일(YYYYMMDD, 포함)")
    parser.add_argument("end_date", help="종료일(YYYYMMDD, 포함)")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        result = analyze_logs(args.root, args.start_date, args.end_date)
    except (FileNotFoundError, ValueError, OSError) as exc:
        parser.error(str(exc))

    for category, statistics in result.items():
        print(f"\n[{category}] 전체 작업: {statistics['total_jobs']}건")
        top_users = statistics["top_users"]
        if not top_users:
            print("  해당 유저 없음")
            continue
        for rank, (user_name, job_count) in enumerate(top_users, start=1):
            print(f"  {rank:2d}. {user_name}: {job_count}건")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
