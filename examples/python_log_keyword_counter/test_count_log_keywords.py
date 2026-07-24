import shutil
import tempfile
import unittest
from pathlib import Path

from count_log_keywords import DEFAULT_KEYWORDS, NO_KEYWORD_LABEL, analyze_logs


class AnalyzeLogsTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def write_log(self, server, date, user, time, fourth_line):
        directory = self.root / server / date
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / f"cmd_{user}_{time}.log"
        path.write_text(f"line 1\nline 2\nline 3\n{fourth_line}\n", encoding="utf-8")
        return path

    def test_counts_jobs_and_users_for_each_requested_keyword(self):
        self.write_log(
            "server-a",
            "20260101",
            "kim",
            "090000",
            "run -laypop_title SALT-Workbench -batch",
        )
        self.write_log("server-b", "20260102", "kim", "100000", "run -batch")
        self.write_log("server-a", "20260102", "lee", "110000", "run -python job.py")
        self.write_log("server-a", "20260102", "park", "120000", "run normally")

        result = analyze_logs(self.root, "20260101", "20260102")

        self.assertEqual(
            result,
            {
                "-laypop_title SALT-Workbench": {
                    "total_jobs": 1,
                    "top_users": [("kim", 1)],
                },
                "-batch": {
                    "total_jobs": 2,
                    "top_users": [("kim", 2)],
                },
                "-python": {
                    "total_jobs": 1,
                    "top_users": [("lee", 1)],
                },
                NO_KEYWORD_LABEL: {
                    "total_jobs": 1,
                    "top_users": [("park", 1)],
                },
            },
        )

    def test_uses_only_fourth_line_and_inclusive_date_range(self):
        directory = self.root / "server-a" / "20260102"
        directory.mkdir(parents=True)
        (directory / "cmd_kim_090000.log").write_text(
            "-batch\n-python\n-laypop_title SALT-Workbench\nno match\n-batch\n",
            encoding="utf-8",
        )
        self.write_log("server-a", "20260101", "lee", "080000", "run -batch")
        self.write_log("server-a", "20260103", "park", "100000", "run -python")
        self.write_log("server-a", "20260104", "choi", "110000", "run -batch")

        result = analyze_logs(self.root, "20260101", "20260103")

        self.assertEqual(result["-batch"]["total_jobs"], 1)
        self.assertEqual(result["-python"]["total_jobs"], 1)
        self.assertEqual(result[NO_KEYWORD_LABEL]["total_jobs"], 1)

    def test_limits_each_category_to_top_ten_users(self):
        for index in range(12):
            user = f"user{index:02d}"
            repeat = 12 - index
            for job in range(repeat):
                self.write_log(
                    "server-a",
                    "20260101",
                    user,
                    f"{job:06d}",
                    "run -batch",
                )

        result = analyze_logs(self.root, "20260101", "20260101")

        self.assertEqual(result["-batch"]["total_jobs"], 78)
        self.assertEqual(len(result["-batch"]["top_users"]), 10)
        self.assertEqual(result["-batch"]["top_users"][0], ("user00", 12))
        self.assertEqual(result["-batch"]["top_users"][-1], ("user09", 3))

    def test_ignores_malformed_names_short_files_links_and_non_files(self):
        date_dir = self.root / "server-a" / "20260101"
        date_dir.mkdir(parents=True)
        (date_dir / "cmd__090000.log").write_text("1\n2\n3\n-batch\n", encoding="utf-8")
        (date_dir / "cmd_kim_250000.log").write_text("1\n2\n3\n-batch\n", encoding="utf-8")
        (date_dir / "cmd_dir_110000.log").mkdir()
        (date_dir / "cmd_short_120000.log").write_text("1\n2\n3\n", encoding="utf-8")

        outside = self.root.parent / f"{self.root.name}-outside"
        outside.mkdir()
        self.addCleanup(shutil.rmtree, outside, True)
        outside_log = outside / "cmd_link_130000.log"
        outside_log.write_text("1\n2\n3\n-batch\n", encoding="utf-8")
        (date_dir / "cmd_link_130000.log").symlink_to(outside_log)
        (self.root / "linked-server").symlink_to(outside, target_is_directory=True)
        (self.root / "server-b").mkdir()
        (self.root / "server-b" / "20260101").symlink_to(outside, target_is_directory=True)

        result = analyze_logs(self.root, "20260101", "20260101")

        for category in (*DEFAULT_KEYWORDS, NO_KEYWORD_LABEL):
            self.assertEqual(result[category]["total_jobs"], 0)
            self.assertEqual(result[category]["top_users"], [])

    def test_validates_root_dates_keywords_and_top_limit(self):
        with self.assertRaisesRegex(FileNotFoundError, "기준 폴더"):
            analyze_logs(self.root / "missing", "20260101", "20260102")
        with self.assertRaisesRegex(ValueError, "시작일"):
            analyze_logs(self.root, "20260102", "20260101")
        with self.assertRaisesRegex(ValueError, "3개"):
            analyze_logs(self.root, "20260101", "20260102", ["A", "B"])
        with self.assertRaisesRegex(ValueError, "1 이상"):
            analyze_logs(self.root, "20260101", "20260102", top_limit=0)


if __name__ == "__main__":
    unittest.main()
