from app.output_comparison import normalize_artifact
from app.ue_reflection_diff import compare_reflection, extract_reflection


def artifact(origin, header):
    return normalize_artifact(origin, {
        "status": "generated",
        "files": [{"path": "Example.h", "content": header}],
        "diagnostics": [],
    })


def test_uproperty_specifier_difference():
    left = artifact("direct", "UPROPERTY(EditAnywhere)\n int Health;")
    right = artifact("ai", "UPROPERTY(BlueprintReadWrite)\n int Health;")
    result = compare_reflection(left, right)
    assert result["status"] == "different"
    assert result["differences"][0]["kind"] == "specifier_changed"


def test_ufunction_missing_on_one_side():
    left = artifact("direct", "UFUNCTION(BlueprintCallable)\n void Jump();")
    right = artifact("ai", "void Jump();")
    result = compare_reflection(left, right)
    assert result["differences"][0]["kind"] == "direct_only"


def test_uclass_and_ustruct():
    parsed = extract_reflection("UCLASS(BlueprintType)\nclass AExample : public AActor {};\nUSTRUCT(BlueprintType)\nstruct FData {};")
    assert "UCLASS:AExample" in parsed["declarations"]
    assert "USTRUCT:FData" in parsed["declarations"]


def test_unparsed_multiline_declaration_reports_incomplete():
    left = artifact("direct", "UFUNCTION(BlueprintCallable)\n void\n Jump();")
    right = artifact("ai", "UFUNCTION(BlueprintCallable)\n void Jump();")
    result = compare_reflection(left, right)
    assert result["status"] == "incomplete"
    assert result["diagnostics"]


def test_mock_is_unavailable():
    direct = artifact("direct", "UPROPERTY()\n int Health;")
    ai = normalize_artifact("ai", {"status": "mocked", "files": [], "diagnostics": []})
    assert compare_reflection(direct, ai)["status"] == "unavailable"
