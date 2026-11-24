import tempfile
import unittest
from pathlib import Path
from unittest import mock

from app.apps import CATALOG, build_apps
from app.apps.counter import CounterApp
from app.apps.monitor import MonitorApp


class CounterTest(unittest.TestCase):
    def test_botoes_e_persistencia(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "st.json"
            c = CounterApp({"title": "Vendas", "goal": 4}, state_path=path)
            self.assertTrue(c.action("d", "inc", ""))
            self.assertTrue(c.action("d", "inc", ""))
            self.assertTrue(c.action("d", "dec", ""))
            self.assertFalse(c.action("d", "outra", ""))
            s = c.screen()
            self.assertEqual((s["layout"], s["title"], s["value"], s["progress"]), ("number", "Vendas", "1", 0.25))
            self.assertEqual([b["id"] for b in s["buttons"]], ["dec", "reset", "inc"])
            # outro processo le o mesmo valor (mesmo dia)
            self.assertEqual(CounterApp({}, state_path=path).value, 1)
            c.action("d", "dec", "")
            c.action("d", "dec", "")
            self.assertEqual(c.value, 0)  # nao fica negativo


class MonitorTest(unittest.TestCase):
    def test_tela_e_atualizar(self):
        m = MonitorApp({"title": "Infra", "slow_ms": 100,
                        "checks": [{"name": "a", "url": "http://x"}, {"name": "b", "url": "http://y"}]})
        s = m.screen()
        self.assertEqual([i["state"] for i in s["items"]], ["off", "off"])  # antes da 1a checagem
        results = {"http://x": ("ok", "50 ms"), "http://y": ("ok", "900 ms")}
        with mock.patch("app.apps.monitor.check", side_effect=lambda url: results[url]):
            self.assertTrue(m.action("d", "refresh", ""))
        s = m.screen()
        self.assertEqual([(i["label"], i["state"]) for i in s["items"]], [("a", "ok"), ("b", "warn")])  # b lento
        self.assertTrue(s["title"].startswith("Infra · "))
        self.assertEqual(s["buttons"][0]["id"], "refresh")

    def test_sem_servicos(self):
        self.assertEqual(MonitorApp({}).screen()["layout"], "message")


class RegistryTest(unittest.TestCase):
    def test_ordem_e_filtro(self):
        with mock.patch.dict(CATALOG, {"pet": MonitorApp}):  # evita abrir banco/transcripts no teste
            apps = build_apps({"apps": ["contador", "nao-existe", "monitor"]})
        self.assertEqual(list(apps), ["contador", "monitor"])


if __name__ == "__main__":
    unittest.main()
