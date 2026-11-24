"""Contrato de um "app" da placa.

A placa e uma so; o que muda e o app ativo, que decide o que aparece na tela e o que os toques fazem.
Cada app devolve uma tela JSON (um dos layouts que o firmware conhece: status, number, qr, message, pet) e
recebe as acoes de toque. O backend cuida do resto: long-poll, ETag, troca de app, menu de apps na placa.

Para criar um app novo:
  1. arquivo em app/apps/ com uma subclasse de App (id, name, screen; refresh/action se precisar);
  2. registrar em app/apps/__init__.py (build_apps);
  3. (opcional) secao com o id dele em backend/apps.json, que chega em self.cfg.
"""
from __future__ import annotations


class App:
    id = ""
    name = ""

    def __init__(self, cfg: dict | None = None):
        self.cfg = cfg or {}

    def screen(self) -> dict:
        """Tela atual (rapido, sem rede/disco). Campos comuns: layout, title, refresh;
        "buttons": [{"id", "label"}] vira uma fileira de botoes embaixo (layouts genericos)."""
        raise NotImplementedError

    def refresh(self) -> None:
        """Trabalho lento (rede, arquivos). Roda numa thread a cada ~20 s; o app decide a propria cadencia."""

    def action(self, device_id: str, action: str, value: str) -> bool:
        """Toque na placa (botao ou item com "action"). Roda numa thread. True se a tela mudou."""
        return False

    def summary(self) -> dict:
        """Diagnostico para /health."""
        return {}
