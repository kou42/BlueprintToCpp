"""Semantic JSON v2からのAI変換を隔離するProvider境界。

Mockはテスト専用。外部APIへ接続せず、C++生成成功を偽装しない。
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol

from .semantic_json import parse_semantic_json


class AIProviderError(ValueError):
    """Providerの入力または出力に関するエラー。"""


@dataclass(frozen=True)
class GeneratedFile:
    path: str
    content: str


@dataclass(frozen=True)
class AIConversionResult:
    provider: str
    status: str
    files: tuple[GeneratedFile, ...]
    diagnostics: tuple[str, ...]


class AIProvider(Protocol):
    """Providerを差し替えるための最小契約。"""

    @property
    def name(self) -> str: ...

    def generate(self, semantic: dict) -> AIConversionResult: ...


class MockAIProvider:
    """実際のAI呼び出しなしで経路を検証する安全なMock。"""

    name = "mock"

    def generate(self, semantic: dict) -> AIConversionResult:
        # v2以外の入力は通さない。構造検証と変換可能性の判定は別責務。
        parsed = parse_semantic_json(semantic)
        status = parsed["semantic_status"]
        if status == "unsupported":
            return AIConversionResult(
                provider=self.name,
                status="rejected",
                files=(),
                diagnostics=("Semantic IR contains unsupported lowering diagnostics.",),
            )
        # 現段階ではcompleteでもMockがC++を生成したとは主張しない。
        return AIConversionResult(
            provider=self.name,
            status="mocked",
            files=(),
            diagnostics=("Mock provider performed validation only; no C++ generated.",),
        )


def get_ai_provider(name: str) -> AIProvider:
    if name == "mock":
        return MockAIProvider()
    if name == "openai":
        # 循環importを避けるため、実Providerを選択した場合のみ読み込む。
        from .openai_provider import OpenAIProvider
        return OpenAIProvider()
    raise AIProviderError(f"Unknown or unconfigured AI provider: {name}")
