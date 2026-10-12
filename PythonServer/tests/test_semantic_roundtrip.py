"""Golden JSONと実際のEditor出力に共通するファイル検証経路を固定する。"""
import copy
import json
from pathlib import Path

import pytest

from app.semantic_json import SemanticJsonError
from app.semantic_roundtrip import main, validate_file

GOLDEN = Path(__file__).parent / "golden" / "semantic_j2_roundtrip.json"


def test_golden_json_passes_validator_and_mock_provider():
    result = validate_file(GOLDEN, check_provider=True)
    assert result["semantic_status"] == "partial"
    assert result["variables"] == 2
    assert result["functions"] == 1
    assert result["graph_traces"] == 1
    assert result["provider_status"] == "mocked"


def test_golden_empty_and_zero_defaults_preserved():
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    assert data["variables"][0]["default_value"] == "0"
    assert data["variables"][1]["default_value"] == ""


@pytest.mark.parametrize("mutate", [
    lambda data: data.update(schema_version=1),
    lambda data: data["variables"][0].update(default_value_source="unavailable"),
    lambda data: data["functions"][0]["parameters"].append({"name": "broken"}),
    lambda data: data["graph_traces"][0]["exec_edges"][0].update(target_pin_id=None),
])
def test_golden_invalid_mutations_rejected(tmp_path, mutate):
    data = json.loads(GOLDEN.read_text(encoding="utf-8"))
    mutate(data)
    target = tmp_path / "invalid.json"
    target.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(SemanticJsonError):
        validate_file(target, check_provider=True)


def test_cli_exit_codes_and_multiple_files(tmp_path, capsys):
    bad = tmp_path / "bad.json"
    bad.write_text("{invalid", encoding="utf-8")
    assert main(["--provider", "mock", str(GOLDEN)]) == 0
    assert main([str(GOLDEN), str(bad)]) == 1
    output = capsys.readouterr().out
    assert '"status": "ok"' in output
    assert '"status": "error"' in output


def test_utf8_bom_and_missing_file(tmp_path):
    target = tmp_path / "export.json"
    target.write_bytes(bytes((0xEF, 0xBB, 0xBF)) + GOLDEN.read_bytes())
    assert validate_file(target)["variables"] == 2
    with pytest.raises(SemanticJsonError):
        validate_file(tmp_path / "missing.json")
