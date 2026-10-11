from app.cpp_effect_diff import compare_effects, extract_effects
from app.output_comparison import normalize_artifact


def artifact(origin, source):
    return normalize_artifact(origin, {
        "status": "generated",
        "files": [{"path": "Example.cpp", "content": source}],
        "diagnostics": [],
    })


def test_reordered_calls_are_reported():
    direct = artifact("direct", "void AExample::Run() {\n First();\n Second();\n}")
    ai = artifact("ai", "void AExample::Run() {\n Second();\n First();\n}")
    result = compare_effects(direct, ai)
    assert result["status"] == "different"
    assert result["differences"][0]["kind"] == "statement_sequence_changed"


def test_changed_assignment_is_reported():
    direct = artifact("direct", "void AExample::Run() {\n Health = 10;\n}")
    ai = artifact("ai", "void AExample::Run() {\n Health = 20;\n}")
    assert compare_effects(direct, ai)["status"] == "different"


def test_identical_sequence_does_not_prove_equivalence():
    source = "void AExample::Run() {\n First();\n}"
    result = compare_effects(artifact("direct", source), artifact("ai", source))
    assert result["status"] == "no_detected_difference"
    assert result["diagnostics"]


def test_branch_is_incomplete():
    source = "void AExample::Run() {\n if (Enabled) {\n First();\n }\n}"
    result = compare_effects(artifact("direct", source), artifact("ai", source))
    assert result["status"] == "incomplete"


def test_unsupported_statement_is_incomplete():
    parsed = extract_effects("void AExample::Run() {\n return;\n}")
    assert parsed["diagnostics"]


def test_mock_unavailable():
    direct = artifact("direct", "void AExample::Run() {\n First();\n}")
    ai = normalize_artifact("ai", {"status": "mocked", "files": [], "diagnostics": []})
    assert compare_effects(direct, ai)["status"] == "unavailable"
