import sys
import unittest
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app.plan import PlanState, fmt_reset, plan_lines  # noqa: E402
from hooks.statusline import line, slim  # noqa: E402

NOW = datetime(2026, 11, 24, 10, 0).timestamp()  # terca-feira


class PlanTests(unittest.TestCase):
    def test_slim_keeps_only_numbers(self):
        raw = {"session_id": "s1", "cwd": "F:\\x", "transcript_path": "segredo",
               "model": {"id": "claude-opus-5-5", "display_name": "Opus 5.5"},
               "rate_limits": {"five_hour": {"used_percentage": 23.5, "resets_at": NOW + 3600},
                               "seven_day": {"used_percentage": 41.2, "resets_at": NOW + 4 * 86400}},
               "context_window": {"used_percentage": 60, "total_input_tokens": 100}}
        s = slim(raw)
        self.assertNotIn("cwd", s)
        self.assertNotIn("transcript_path", s)
        self.assertEqual(s["five_hour"]["used_percentage"], 23.5)
        self.assertEqual(s["context"]["used_percentage"], 60)
        self.assertEqual(s["model"], "Opus 5.5")
        self.assertEqual(line(s), "5h 24% · sem 41%")

    def test_lines(self):
        p = PlanState()
        p.update({"session_id": "s1",
                  "five_hour": {"used_percentage": 23.5, "resets_at": datetime(2026, 11, 24, 14, 10).timestamp()},
                  "seven_day": {"used_percentage": 56, "resets_at": datetime(2026, 11, 28, 15, 0).timestamp()},
                  "context": {"used_percentage": 60.2}}, now=NOW)
        self.assertTrue(p.has_data(NOW))
        self.assertEqual(plan_lines(p, "s1", NOW), ["5 h 24% · 14:10", "Semana 56% · sáb 15:00", "Contexto 60%"])
        self.assertEqual(plan_lines(p, "outra", NOW), ["5 h 24% · 14:10", "Semana 56% · sáb 15:00"])

    def test_window_after_reset_is_zero(self):
        p = PlanState()
        p.update({"five_hour": {"used_percentage": 90, "resets_at": NOW + 60}}, now=NOW)
        self.assertEqual(plan_lines(p, None, NOW + 120), ["5 h 0%"])

    def test_stale_and_empty(self):
        p = PlanState()
        self.assertFalse(p.has_data(NOW))
        p.update({"session_id": "s", "context": {"used_percentage": 10}}, now=NOW)
        self.assertFalse(p.has_data(NOW))  # so contexto, sem limites
        self.assertEqual(fmt_reset(None), "")


if __name__ == "__main__":
    unittest.main()
