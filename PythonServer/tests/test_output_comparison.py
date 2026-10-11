from app.output_comparison import compare_artifacts, normalize_artifact


def artifact(origin, files, status="generated", diagnostics=()):
    return normalize_artifact(origin, {
        "status": status, "files": files, "diagnostics": list(diagnostics),
    })


def test_identical_text_requires_further_verification():
    files = [{"path": "Actor.cpp", "content": "int a = 1;\n"}]
    result = compare_artifacts(artifact("direct", files), artifact("ai", files))
    assert result["status"] == "identical"
    assert result["equivalent"] is None
    assert result["files"][0]["kind"] == "identical"


def test_modified_file_produces_unified_diff():
    left = artifact("direct", [{"path": "Actor.cpp", "content": "int a = 1;\n"}])
    right = artifact("ai", [{"path": "Actor.cpp", "content": "int a = 2;\n"}])
    result = compare_artifacts(left, right)
    assert result["status"] == "different"
    assert result["files"][0]["kind"] == "modified"
    assert "-int a = 1;" in result["files"][0]["diff"]
    assert "+int a = 2;" in result["files"][0]["diff"]


def test_missing_file_is_reported():
    left = artifact("direct", [{"path": "Actor.h", "content": "#pragma once"}])
    right = artifact("ai", [{"path": "Actor.cpp", "content": "int a;"}])
    result = compare_artifacts(left, right)
    assert [entry["kind"] for entry in result["files"]] == ["ai_only", "direct_only"]


def test_mock_is_unavailable_not_equal():
    left = artifact("direct", [{"path": "Actor.cpp", "content": "int a;"}])
    right = artifact("ai", [], status="mocked", diagnostics=("No generation.",))
    result = compare_artifacts(left, right)
    assert result["status"] == "unavailable"
    assert result["equivalent"] is None


def test_fingerprint_is_independent_of_file_order():
    files = [{"path": "A.h", "content": "#pragma once"},
             {"path": "A.cpp", "content": "int a;"}]
    assert artifact("direct", files).fingerprint == artifact("direct", list(reversed(files))).fingerprint


def test_diagnostics_are_preserved():
    item = artifact("ai", [], status="rejected", diagnostics=("Unsupported.",))
    assert item.diagnostics == ("Unsupported.",)
