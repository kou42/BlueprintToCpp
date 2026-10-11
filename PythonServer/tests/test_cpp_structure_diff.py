from app.cpp_structure_diff import compare_declarations, extract_declarations
from app.output_comparison import normalize_artifact


def artifact(origin, header):
    return normalize_artifact(origin, {
        "status": "generated",
        "files": [{"path": "Example.h", "content": header}],
        "diagnostics": [],
    })


def test_class_and_function_difference():
    direct = artifact("direct", "class AExample {\npublic:\n void BeginPlay();\n int Health;\n};")
    ai = artifact("ai", "class AExample {\npublic:\n void Tick(float Delta);\n int Health;\n};")
    result = compare_declarations(direct, ai)
    assert result["status"] == "different"
    assert {"category": "functions", "declaration": "BeginPlay(): void", "kind": "direct_only"} in result["differences"]
    assert {"category": "functions", "declaration": "Tick(float Delta): void", "kind": "ai_only"} in result["differences"]


def test_equal_declarations_not_semantic_equivalence():
    header = "class AExample {\n void BeginPlay();\n};"
    result = compare_declarations(artifact("direct", header), artifact("ai", header))
    assert result["status"] == "no_detected_difference"
    assert result["diagnostics"]


def test_variables_and_class_difference():
    result = compare_declarations(
        artifact("direct", "class ADirect {\n float Speed;\n};"),
        artifact("ai", "class AAI {\n int Count;\n};"),
    )
    assert len(result["differences"]) == 4


def test_unsupported_header_construct_is_not_claimed_as_parsed():
    result = extract_declarations("UFUNCTION(BlueprintCallable)\n virtual void BeginPlay();")
    assert result["functions"] == ["BeginPlay(): void"]


def test_non_generated_is_unavailable():
    direct = artifact("direct", "class ADirect {};")
    ai = normalize_artifact("ai", {"status": "mocked", "files": [], "diagnostics": []})
    assert compare_declarations(direct, ai)["status"] == "unavailable"
