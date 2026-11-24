"""Testes da logica do pet (so stdlib):  python -m unittest discover -s tests  (dentro de backend/)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app import config  # noqa: E402
from app.pet import PetState, describe_tool, translate_notification  # noqa: E402
from hooks.claude_hook import slim  # noqa: E402


class Clock:
    def __init__(self):
        self.t = 1_000_000.0

    def __call__(self):
        return self.t


def ev(name, sid="s1", **kw):
    return {"hook_event_name": name, "session_id": sid, "cwd": "F:\\ESP32_tela", **kw}


class PetTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.pet = PetState(now=self.clock)

    def test_starts_sleeping(self):
        self.assertEqual(self.pet.screen()["mood"], "sleeping")

    def test_prompt_tool_stop_flow(self):
        self.pet.handle(ev("UserPromptSubmit", prompt="segredo"))
        s = self.pet.screen()
        self.assertEqual((s["mood"], s["bubble"], s["sub"], s["title"]), ("working", "Pensando…", "", "Little Bit Pixel"))

        self.pet.handle(ev("PreToolUse", tool_name="Edit", tool_input={"file_path": "F:\\ESP32_tela\\src\\ui.cpp"}))
        self.assertEqual(self.pet.screen()["bubble"], "Editando ui.cpp")

        self.pet.handle(ev("Stop"))
        self.assertEqual(self.pet.screen()["mood"], "done")

        self.clock.t += config.DONE_SECONDS + 1
        self.assertEqual(self.pet.screen()["mood"], "idle")

        self.clock.t += config.SLEEP_SECONDS + 1
        self.assertEqual(self.pet.screen()["mood"], "sleeping")

    def test_attention_wins_over_working(self):
        self.pet.handle(ev("PreToolUse", sid="a", tool_name="Bash", tool_input={"command": "pio run"}))
        self.pet.handle(ev("Notification", sid="b", message="Claude needs your permission to use Bash"))
        s = self.pet.screen()
        self.assertEqual((s["mood"], s["bubble"], s["busy"]), ("attention", "Preciso da sua permissão!", 2))
        self.assertIn("2 sessões", s["sub"])

    def test_permission_granted_returns_to_working(self):
        self.pet.handle(ev("Notification", message="Claude needs your permission to use Bash"))
        self.pet.handle(ev("PostToolUse", tool_name="Bash"))
        self.assertEqual(self.pet.screen()["mood"], "working")

    def test_question_tool_asks_for_attention(self):
        self.pet.handle(ev("PreToolUse", tool_name="AskUserQuestion", tool_input={}))
        s = self.pet.screen()
        self.assertEqual((s["mood"], s["bubble"]), ("attention", "Tenho uma pergunta pra você"))
        self.pet.handle(ev("PostToolUse", tool_name="AskUserQuestion"))
        self.assertEqual(self.pet.screen()["mood"], "working")

    def test_session_end_removes(self):
        self.pet.handle(ev("UserPromptSubmit"))
        self.pet.handle(ev("SessionEnd", reason="exit"))
        self.assertEqual(self.pet.screen()["mood"], "sleeping")

    def test_out_of_order_event_ignored(self):
        self.pet.handle(ev("Stop", ts=200.0))
        self.assertFalse(self.pet.handle(ev("PostToolUse", tool_name="Bash", ts=199.0)))
        self.assertEqual(self.pet.screen()["mood"], "done")

    def test_unknown_event_ignored(self):
        self.assertFalse(self.pet.handle(ev("Whatever")))

    def test_describe_tool(self):
        self.assertEqual(describe_tool("Bash", {"command": "pio run -e cyd2usb\nmais"}), "Rodando pio run -e cyd2usb")
        self.assertEqual(describe_tool("mcp__srv__do_thing", {}), "Usando do_thing")
        self.assertEqual(describe_tool("PowerShell", {"command": "Set-Location F:\\ESP32_tela; & pio run"}),
                         "Rodando pio run")
        self.assertEqual(describe_tool("PowerShell", {"command": "Start-Sleep 3; & \"$env:USERPROFILE\\x\\python.exe\" a.py"}),
                         "Rodando Start-Sleep 3")
        self.assertEqual(describe_tool("PowerShell", {"command": "& \"C:\\pio\\pio.exe\" run -e cyd2usb | Select-String x"}),
                         "Rodando pio run -e cyd2usb")
        self.assertEqual(describe_tool("Bash", {"command": 'cd "/f/a b" && cd x && make'}), "Rodando make")
        self.assertEqual(translate_notification("Claude is waiting for your input"), "Estou esperando você")

    def test_hook_slim_drops_private_fields(self):
        out = slim({"hook_event_name": "PreToolUse", "prompt": "segredo", "transcript_path": "x",
                    "tool_input": {"command": "echo a\necho b", "content": "arquivo inteiro"}})
        self.assertNotIn("prompt", out)
        self.assertNotIn("transcript_path", out)
        self.assertEqual(out["tool_input"], {"command": "echo a"})


if __name__ == "__main__":
    unittest.main()
