import json
from pathlib import Path
import subprocess

import pytest

from app.ubt_verification import BuildVerificationError, verify_generated_plugin


def artifact(status="generated"):
    files = [] if status != "generated" else [{
        "path": "VerificationActor.h",
        "content": "#pragma once\n#include \"CoreMinimal.h\"\n#include \"GameFramework/Actor.h\"\n#include \"VerificationActor.generated.h\"\nUCLASS()\nclass AVerificationActor : public AActor { GENERATED_BODY() };\n",
    }, {
        "path": "VerificationActor.cpp",
        "content": "#include \"VerificationActor.h\"\n",
    }]
    return {"status": status, "files": files, "diagnostics": []}


def engine(tmp_path: Path) -> Path:
    root = tmp_path / "UE"
    script = root / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"
    script.parent.mkdir(parents=True)
    script.write_text("@echo off\n", encoding="utf-8")
    return root


def completed(return_code=0):
    def run(command, **kwargs):
        assert kwargs["shell"] is False
        assert command[1] == "BuildPlugin"
        return subprocess.CompletedProcess(command, return_code, "build output", "")
    return run


def test_success_creates_isolated_plugin_and_review_evidence(tmp_path):
    result = verify_generated_plugin(
        artifact(), origin="direct", engine_root=engine(tmp_path),
        output_root=tmp_path / "out", runner=completed(),
    )
    workspace = Path(result["workspace"])
    assert result["status"] == "passed"
    assert result["executed_by_harness"] is True
    assert {item["tool"] for item in result["review_evidence"]} == {"uht", "ubt"}
    assert all(item["expected_revised_fingerprint"] == result["artifact_fingerprint"]
               for item in result["review_evidence"])
    assert (workspace / "B2CVerify" / "B2CVerify.uplugin").is_file()
    assert (workspace / "B2CVerify" / "Source" /
            "B2CVerify" / "Public" / "VerificationActor.h").is_file()
    assert json.loads((workspace / "result.json").read_text(encoding="utf-8"))["status"] == "passed"


def test_failure_does_not_infer_uht_outcome(tmp_path):
    result = verify_generated_plugin(
        artifact(), origin="ai", engine_root=engine(tmp_path),
        output_root=tmp_path / "out", runner=completed(6),
    )
    assert result["status"] == "failed"
    assert result["return_code"] == 6
    assert [item["tool"] for item in result["review_evidence"]] == ["ubt"]
    assert result["review_evidence"][0]["outcome"] == "failed"


def test_timeout_preserves_logs_without_inferred_evidence(tmp_path):
    def timeout(command, **kwargs):
        raise subprocess.TimeoutExpired(command, kwargs["timeout"], output=b"partial output")

    result = verify_generated_plugin(
        artifact(), origin="direct", engine_root=engine(tmp_path),
        output_root=tmp_path / "out", timeout_seconds=30, runner=timeout,
    )
    workspace = Path(result["workspace"])
    assert result["status"] == "timed_out"
    assert result["return_code"] is None
    assert result["review_evidence"] == []
    assert (workspace / "stdout.log").read_text(encoding="utf-8") == "partial output"
    assert json.loads((workspace / "result.json").read_text(encoding="utf-8"))["status"] == "timed_out"


def test_execution_error_is_recorded_without_inferred_evidence(tmp_path):
    def cannot_start(command, **kwargs):
        raise OSError("launcher unavailable")

    result = verify_generated_plugin(
        artifact(), origin="direct", engine_root=engine(tmp_path),
        output_root=tmp_path / "out", runner=cannot_start,
    )
    workspace = Path(result["workspace"])
    assert result["status"] == "execution_error"
    assert result["review_evidence"] == []
    assert "launcher unavailable" in (workspace / "stderr.log").read_text(encoding="utf-8")


@pytest.mark.parametrize("value", [
    {"status": "mocked", "files": [], "diagnostics": []},
    {"status": "generated", "files": [{"path": "nested/A.h", "content": "x"},
                                       {"path": "A.cpp", "content": "x"}], "diagnostics": []},
    {"status": "generated", "files": [{"path": "A.txt", "content": "x"},
                                       {"path": "A.cpp", "content": "x"}], "diagnostics": []},
])
def test_rejects_unsupported_artifacts(tmp_path, value):
    with pytest.raises(BuildVerificationError):
        verify_generated_plugin(value, origin="direct", engine_root=engine(tmp_path),
                                output_root=tmp_path / "out", runner=completed())


def test_rejects_missing_engine_and_invalid_timeout(tmp_path):
    with pytest.raises(BuildVerificationError):
        verify_generated_plugin(artifact(), origin="direct", engine_root=tmp_path / "missing",
                                output_root=tmp_path / "out", runner=completed())
    with pytest.raises(BuildVerificationError):
        verify_generated_plugin(artifact(), origin="direct", engine_root=engine(tmp_path),
                                output_root=tmp_path / "out", timeout_seconds=0, runner=completed())
