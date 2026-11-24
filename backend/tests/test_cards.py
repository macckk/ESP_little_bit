import sys
import unittest
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app.cards import build_cards, today_card, week_card  # noqa: E402
from app.weather import Weather, describe  # noqa: E402


class CardTests(unittest.TestCase):
    def test_today(self):
        c = today_card({"prompts": 1, "edits": 12, "commands": 0, "projects": 1}, 3 * 3600 + 5 * 60)
        self.assertEqual([(i["v"], i["l"]) for i in c["items"]],
                         [("1", "prompt"), ("3h05", "ativo"), ("12", "edições"), ("0", "comandos")])

    def test_week(self):
        days = [(datetime(2026, 11, 18 + i), s) for i, s in enumerate([0, 600, 3600, 0, 5400, 120, 7260])]
        c = week_card(days)
        self.assertEqual([d["d"] for d in c["days"]], ["qua", "qui", "sex", "sáb", "dom", "seg", "ter"])
        self.assertEqual([d["t"] for d in c["days"]], ["", "10m", "1h00", "", "1h30", "2m", "2h01"])
        self.assertIn("4h43", c["title"])  # 16980 s

    def test_build_without_weather(self):
        cards = build_cards(None, {"prompts": 0, "edits": 0, "commands": 0, "projects": 0},
                            [(datetime(2026, 11, 24), 0)])
        self.assertEqual([c["type"] for c in cards], ["clock", "today", "week"])

    def test_weather_card(self):
        self.assertEqual(describe(95), ("Tempestade", "storm"))
        w = Weather("Campinas", "São Paulo")
        w.geo = {"name": "Campinas"}
        w.fetched_at = 1e18  # "agora"
        import time
        w.fetched_at = time.time()
        w.data = {"current": {"temperature_2m": 30.8, "relative_humidity_2m": 58, "weather_code": 3, "is_day": 1},
                  "daily": {"temperature_2m_max": [31.7, 30.7], "temperature_2m_min": [23.5, 22.9],
                            "weather_code": [53, 95], "precipitation_probability_max": [81, 88]}}
        c = w.card()
        self.assertEqual((c["temp"], c["desc"], c["icon"]), ("31°", "Nublado", "cloud"))
        self.assertEqual(c["lines"], ["Máx 32° · Mín 24°", "Chuva 81% · Umidade 58%",
                                      "Amanhã: tempestade · 23–31°"])


if __name__ == "__main__":
    unittest.main()
