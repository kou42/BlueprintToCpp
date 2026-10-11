from app.cpp_signature_diff import compare_signatures, extract_signatures
from app.output_comparison import normalize_artifact


def artifact(origin, header):
    return normalize_artifact(origin, {
        "status": "generated",
        "files": [{"path": "Example.h", "content": header}],
        "diagnostics": [],
    })


def test_return_type_and_argument_change():
    direct = artifact("direct", "class AExample {\n int Compute(float Value);\n};")
    ai = artifact("ai", "class AExample {\n float Compute(int Value);\n};")
    result = compare_signatures(direct, ai)
    assert result["status"] == "different"
    assert {item["field"] for item in result["differences"]} == {"return_type", "arguments"}


def test_const_and_override_change():
    direct = artifact("direct", "class AExample {\n void Tick(float Delta) const override;\n};")
    ai = artifact("ai", "class AExample {\n void Tick(float Delta);\n};")
    result = compare_signatures(direct, ai)
    assert {item["field"] for item in result["differences"]} == {"const", "override"}


def test_equal_signatures_not_semantic_equivalence():
    header = "class AExample {\n void BeginPlay();\n};"
    result = compare_signatures(artifact("direct", header), artifact("ai", header))
    assert result["status"] == "no_detected_difference"
    assert result["diagnostics"]


def test_overloads_are_incomplete():
    header = "class AExample {\n void Run(int X);\n void Run(float X);\n};"
    result = compare_signatures(artifact("direct", header), artifact("ai", header))
    assert result["status"] == "incomplete"


def test_multiline_declaration_is_incomplete():
    header = "class AExample {\n void Run(\n int X);\n};"
    result = extract_signatures(header)
    assert result["diagnostics"]


def test_mock_is_unavailable():
    direct = artifact("direct", "class AExample {\n void BeginPlay();\n};")
    ai = normalize_artifact("ai", {"status": "mocked", "files": [], "diagnostics": []})
    assert compare_signatures(direct, ai)["status"] == "unavailable"
