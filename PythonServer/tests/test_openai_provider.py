import json
from unittest.mock import patch

import pytest

from app.ai_provider import AIProviderError
from app.openai_provider import OpenAIProvider


def semantic(status="partial"):
    return {
        "schema_version": 2,
        "format": "blueprint_semantic_ir",
        "producer": {"name": "BlueprintAIConverter", "engine_version": "5.7.4"},
        "source": {"asset_path": "/Game/Test.Test", "parent_class_path": "/Script/Engine.Actor",
                   "snapshot_schema_version": 1},
        "semantic_status": status, "functions": [], "diagnostics": [],
    }


class FakeResponse:
    def __init__(self, output, finish_reason="stop"):
        content = json.dumps(output)
        self.data = json.dumps({"choices": [{
            "finish_reason": finish_reason,
            "message": {"content": content},
        }]}).encode()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False

    def read(self, limit):
        return self.data[:limit]


def provider():
    return OpenAIProvider(api_key="test-secret", model="test-model", timeout=2)


def test_generated_result_is_validated():
    output = {"files": [{"path": "Test.cpp", "content": "int x = 1;"}],
              "diagnostics": []}
    with patch("app.openai_provider.urlopen", return_value=FakeResponse(output)) as request:
        result = provider().generate(semantic())
    assert result.status == "generated"
    assert result.files[0].path == "Test.cpp"
    assert "uncompiled" in result.diagnostics[-1]
    assert request.call_args.kwargs["timeout"] == 2


def test_unsupported_skips_network():
    with patch("app.openai_provider.urlopen") as request:
        result = provider().generate(semantic("unsupported"))
    assert result.status == "rejected"
    request.assert_not_called()


@pytest.mark.parametrize("output", [
    {"files": [{"path": "../escape.cpp", "content": "int x;"}], "diagnostics": []},
    {"files": [], "diagnostics": []},
    {"files": [{"path": "Test.cpp", "content": ""}], "diagnostics": []},
    {"files": [{"path": "Test.cpp", "content": "int x;"}], "diagnostics": "none"},
])
def test_invalid_model_output_rejected(output):
    with patch("app.openai_provider.urlopen", return_value=FakeResponse(output)):
        with pytest.raises(AIProviderError):
            provider().generate(semantic())


def test_truncated_model_output_rejected():
    output = {"files": [{"path": "Test.cpp", "content": "int x;"}], "diagnostics": []}
    with patch("app.openai_provider.urlopen", return_value=FakeResponse(output, "length")):
        with pytest.raises(AIProviderError):
            provider().generate(semantic())


def test_missing_config_rejected(monkeypatch):
    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    monkeypatch.delenv("BLUEPRINT_TO_CPP_OPENAI_MODEL", raising=False)
    with pytest.raises(AIProviderError):
        OpenAIProvider()
