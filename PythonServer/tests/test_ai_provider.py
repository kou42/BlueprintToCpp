import pytest

from app.ai_provider import AIProviderError, MockAIProvider, get_ai_provider
from app.semantic_json import SemanticJsonError


def semantic(status="partial"):
    return {
        "schema_version": 2,
        "format": "blueprint_semantic_ir",
        "producer": {"name": "BlueprintAIConverter", "engine_version": "5.7.4"},
        "source": {
            "asset_path": "/Game/Test.Test",
            "parent_class_path": "/Script/Engine.Actor",
            "snapshot_schema_version": 1,
        },
        "semantic_status": status,
        "functions": [],
        "diagnostics": [],
    }


def test_mock_never_generates_cpp():
    result = get_ai_provider("mock").generate(semantic())
    assert result.provider == "mock"
    assert result.status == "mocked"
    assert result.files == ()
    assert result.diagnostics


def test_unsupported_is_rejected():
    result = MockAIProvider().generate(semantic("unsupported"))
    assert result.status == "rejected"
    assert result.files == ()


def test_unknown_provider_is_rejected():
    with pytest.raises(AIProviderError):
        get_ai_provider("remote")


def test_invalid_semantic_json_is_rejected():
    with pytest.raises(SemanticJsonError):
        MockAIProvider().generate({"schema_version": 1})


def test_mock_does_not_claim_complete_conversion():
    result = MockAIProvider().generate(semantic("complete"))
    assert result.status == "mocked"
    assert result.files == ()
