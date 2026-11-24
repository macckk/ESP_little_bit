import json
import sys
import tempfile
import time
import unittest
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app.usage import UsageTracker, brl_number, local_lines, price_for, response_cost, usage_lines  # noqa: E402


def entry(msg_id, session, usage, ts=None, model="claude-opus-5-5"):
    ts = ts or datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    return json.dumps({"type": "assistant", "sessionId": session, "timestamp": ts,
                       "message": {"id": msg_id, "model": model, "usage": usage}}) + "\n"


class UsageTests(unittest.TestCase):
    def test_cost_matches_claude_code_total(self):
        # Totais reais de uma sessao; o Claude Code registrou totalCostUSD = 18.3608
        u = {"input_tokens": 8078, "output_tokens": 264176, "cache_read_input_tokens": 47623548,
             "cache_creation": {"ephemeral_5m_input_tokens": 0, "ephemeral_1h_input_tokens": 440030}}
        self.assertAlmostEqual(response_cost(u, "claude-opus-5-5"), 18.3608, places=3)

    def test_price_lookup(self):
        self.assertEqual(price_for("claude-sonnet-5-5")[0], 2.0)
        self.assertEqual(price_for("claude-opus-4-5-20251101"), None)
        self.assertEqual(price_for("claude-opus-5-5[1m]")[0], 4.0)
        self.assertEqual(response_cost({"output_tokens": 10}, "modelo-desconhecido"), 0.0)

    def test_scan_dedupes_and_is_incremental(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "proj" / "s1.jsonl"
            f.parent.mkdir()
            u = {"output_tokens": 1_000_000}  # US$ 20 no Opus 5.5
            f.write_text(entry("m1", "s1", u) + entry("m1", "s1", u), encoding="utf-8")
            t = UsageTracker(Path(d))
            t.scan()
            self.assertAlmostEqual(t.summary("s1")["today"], 20.0)
            with open(f, "a", encoding="utf-8") as fh:
                fh.write(entry("m2", "s2", u))
            t.scan()
            s = t.summary("s1")
            self.assertAlmostEqual(s["today"], 40.0)
            self.assertAlmostEqual(s["session"], 20.0)

    def test_old_entries_not_counted_today(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "p" / "x.jsonl"
            f.parent.mkdir()
            old = datetime.fromtimestamp(time.time() - 40 * 3600, timezone.utc).isoformat().replace("+00:00", "Z")
            f.write_text(entry("old", "x", {"output_tokens": 1_000_000}, ts=old), encoding="utf-8")
            t = UsageTracker(Path(d))
            t.scan()
            self.assertEqual(t.summary("x")["today"], 0)

    def test_context_and_active_time(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "p" / "s.jsonl"
            f.parent.mkdir()
            now = time.time()

            def iso(t):
                return datetime.fromtimestamp(t, timezone.utc).isoformat().replace("+00:00", "Z")

            f.write_text(
                entry("a", "s", {"input_tokens": 2, "cache_read_input_tokens": 400_000}, ts=iso(now - 120))
                + entry("b", "s", {"input_tokens": 2, "cache_read_input_tokens": 600_000,
                                   "cache_creation_input_tokens": 16_000}, ts=iso(now - 60))
                + entry("c", "s", {"input_tokens": 1}, ts=iso(now - 30)).replace('"type"', '"isSidechain": true, "type"', 1),
                encoding="utf-8")
            t = UsageTracker(Path(d))
            t.scan()
            self.assertAlmostEqual(t.context_pct("s"), 61.6, places=1)   # subagente nao conta
            self.assertAlmostEqual(t.context_pct(None, now), 61.6, places=1)  # sessao mais recente
            self.assertEqual(t.context_pct("outra"), None)
            self.assertGreaterEqual(t.active_seconds_today(now), 60)

    def test_local_lines(self):
        self.assertEqual(local_lines(61.6, 1, 31, 3 * 3600 + 20 * 60),
                         ["Contexto 62%", "5 h: 1 prompt", "Hoje: 31 prompts", "Ativo hoje: 3h20"])
        self.assertEqual(local_lines(None, 0, 0, 59)[-1], "Ativo hoje: 0 min")

    def test_lines_format(self):
        self.assertEqual(brl_number(1234.5), "1.234,50")
        lines = usage_lines({"today": 18.36, "last5h": 6.1, "session": 4.2}, has_session=True)
        self.assertEqual(lines, ["Hoje ≈ US$ 18,36", "5 h ≈ US$ 6,10", "Sessão ≈ US$ 4,20"])


if __name__ == "__main__":
    unittest.main()
