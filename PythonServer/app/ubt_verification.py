"""生成C++を隔離した一時プラグインとしてUHT/UBT検証するローカルCLI。"""
from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import subprocess
from typing import Callable
from uuid import uuid4

from .generated_output_validator import GeneratedOutputError
from .output_comparison import normalize_artifact


class BuildVerificationError(ValueError):
    """安全に検証を開始できない入力または環境。"""


def _load_artifact(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8-sig") as stream:
            value = json.load(stream)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise BuildVerificationError(f"cannot read artifact JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise BuildVerificationError("artifact JSON must be an object")
    return value


def _source_files(artifact) -> tuple[dict, dict]:
    headers: list[dict] = []
    sources: list[dict] = []
    for file in artifact.files:
        path = PurePosixPath(file["path"])
        # MVPではinclude探索と同名衝突を曖昧にしないため、平坦な1組だけを扱う。
        if len(path.parts) != 1:
            raise BuildVerificationError("verification MVP requires flat generated file paths")
        suffix = path.suffix.casefold()
        if suffix in (".h", ".hpp"):
            headers.append(file)
        elif suffix in (".cpp", ".cc", ".cxx"):
            sources.append(file)
        else:
            raise BuildVerificationError(f"unsupported generated source extension: {path.suffix}")
    if len(headers) != 1 or len(sources) != 1:
        raise BuildVerificationError("verification MVP requires exactly one header and one source file")
    return headers[0], sources[0]


def _write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def verify_generated_plugin(
    raw_artifact: dict,
    *,
    origin: str,
    engine_root: Path,
    output_root: Path,
    timeout_seconds: int = 900,
    runner: Callable = subprocess.run,
) -> dict:
    """既存Sourceを変更せず、検証専用プラグインを新規作成してBuildPluginを実行する。"""
    if origin not in ("direct", "ai"):
        raise BuildVerificationError("origin must be direct or ai")
    if timeout_seconds < 1 or timeout_seconds > 3600:
        raise BuildVerificationError("timeout_seconds must be 1..3600")
    try:
        artifact = normalize_artifact(origin, raw_artifact)
    except GeneratedOutputError as exc:
        raise BuildVerificationError(str(exc)) from exc
    if artifact.status != "generated":
        raise BuildVerificationError("artifact status must be generated")
    header, source = _source_files(artifact)

    engine = engine_root.resolve()
    run_uat = engine / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"
    if not run_uat.is_file():
        raise BuildVerificationError("engine_root does not contain Engine/Build/BatchFiles/RunUAT.bat")
    root = output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    # BuildPlugin は HostProject/Plugins 以下へ複製するため、Windows の従来の
    # MAX_PATH 環境でも Intermediate の応答ファイル名が収まる短い内部名を使う。
    workspace = root / f"v-{artifact.fingerprint[:8]}-{uuid4().hex[:4]}"
    workspace.mkdir(exist_ok=False)
    plugin = workspace / "B2CVerify"
    package = workspace / "Package"
    module = "B2CVerify"

    _write_text(plugin / f"{module}.uplugin", json.dumps({
        "FileVersion": 3,
        "Version": 1,
        "VersionName": "1.0",
        "FriendlyName": "BlueprintToCpp Verification",
        "CanContainContent": False,
        "Modules": [{"Name": module, "Type": "Runtime", "LoadingPhase": "Default"}],
    }, ensure_ascii=False, indent=2) + "\n")
    _write_text(plugin / "Source" / module / f"{module}.Build.cs", """using UnrealBuildTool;

public class B2CVerify : ModuleRules
{
    public B2CVerify(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
    }
}
""")
    _write_text(plugin / "Source" / module / "Public" / header["path"], header["content"])
    _write_text(plugin / "Source" / module / "Private" / source["path"], source["content"])

    manifest = {
        "origin": origin,
        "artifact_fingerprint": artifact.fingerprint,
        "files": [header["path"], source["path"]],
        "timeout_seconds": timeout_seconds,
    }
    _write_text(workspace / "request.json", json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    command = [str(run_uat), "BuildPlugin", f"-Plugin={plugin / (module + '.uplugin')}",
               f"-Package={package}", "-TargetPlatforms=Win64", "-Rocket"]
    try:
        completed = runner(
            command, cwd=workspace, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=timeout_seconds, shell=False,
        )
        stdout = completed.stdout or ""
        stderr = completed.stderr or ""
        return_code = completed.returncode
        status = "passed" if return_code == 0 else "failed"
        execution_detail = (
            "RunUAT BuildPlugin completed successfully."
            if status == "passed"
            else f"RunUAT BuildPlugin failed with exit code {return_code}."
        )
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode("utf-8", "replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode("utf-8", "replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        return_code = None
        status = "timed_out"
        execution_detail = f"RunUAT BuildPlugin exceeded the {timeout_seconds}-second timeout."
    except OSError as exc:
        stdout = ""
        stderr = str(exc)
        return_code = None
        status = "execution_error"
        execution_detail = f"RunUAT BuildPlugin could not be started: {exc}"

    _write_text(workspace / "stdout.log", stdout)
    _write_text(workspace / "stderr.log", stderr)
    evidence_reference = str(workspace)
    evidence = []
    if status == "passed":
        for tool in ("uht", "ubt"):
            evidence.append({
                "tool": tool, "outcome": "passed",
                "evidence_reference": evidence_reference,
                "summary": "RunUAT BuildPlugin completed successfully in an isolated workspace.",
                "expected_revised_fingerprint": artifact.fingerprint,
            })
    elif status == "failed":
        evidence.append({
            "tool": "ubt", "outcome": "failed",
            "evidence_reference": evidence_reference,
            "summary": f"RunUAT BuildPlugin failed with exit code {return_code}; UHT outcome was not inferred.",
            "expected_revised_fingerprint": artifact.fingerprint,
        })

    result = {
        "status": status,
        "origin": origin,
        "artifact_fingerprint": artifact.fingerprint,
        "workspace": evidence_reference,
        "return_code": return_code,
        "executed_by_harness": True,
        "review_evidence": evidence,
        "diagnostics": [
            execution_detail,
            "A successful BuildPlugin run establishes compilation only, not Blueprint runtime equivalence.",
        ],
    }
    _write_text(workspace / "result.json", json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Verify generated Unreal C++ in an isolated plugin")
    parser.add_argument("artifact", type=Path, help="Generation artifact JSON")
    parser.add_argument("--origin", choices=("direct", "ai"), required=True)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args(argv)
    try:
        result = verify_generated_plugin(
            _load_artifact(args.artifact), origin=args.origin,
            engine_root=args.engine_root, output_root=args.output_root,
            timeout_seconds=args.timeout,
        )
    except BuildVerificationError as exc:
        print(json.dumps({"status": "error", "message": str(exc)}, ensure_ascii=False))
        return 2
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
