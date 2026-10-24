"""Run the UE Semantic JSON export test and validate its real artifact via Python CLI."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


ARTIFACT_PATTERN = re.compile(
    r"SEMANTIC_ROUNDTRIP_ARTIFACT=(?P<path>[^\r\n]+\.semantic\.json)",
)
TEST_SUCCESS_PATTERN = re.compile(
    r"Test Completed\. Result=\{Success\}.*"
    r"Path=\{BlueprintAIConverter\.SemanticJson\.EditorRoundtrip\}",
)
SUITE_SUCCESS_MARKER = "TEST COMPLETE. EXIT CODE: 0"


class SemanticEditorE2EError(RuntimeError):
    """Raised when the Editor export or Python validation boundary fails."""


def _run(command: list[str], *, cwd: Path | None, timeout: int) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise SemanticEditorE2EError(f"cannot run {command[0]}: {exc}") from exc


def run_e2e(
    editor: Path,
    project: Path,
    *,
    python_executable: Path = Path(sys.executable),
    python_server: Path | None = None,
    timeout: int = 300,
) -> dict:
    """Export from a real transient Blueprint, then invoke the public validation CLI."""
    editor = editor.resolve()
    project = project.resolve()
    python_executable = python_executable.resolve()
    python_server = (python_server or Path(__file__).resolve().parents[1]).resolve()
    for label, path in (
        ("UnrealEditor-Cmd", editor),
        ("uproject", project),
        ("Python executable", python_executable),
        ("PythonServer", python_server),
    ):
        if not path.exists():
            raise SemanticEditorE2EError(f"{label} does not exist: {path}")

    editor_result = _run([
        str(editor),
        str(project),
        "-ExecCmds=Automation RunTests BlueprintAIConverter.SemanticJson.EditorRoundtrip; Quit",
        "-unattended",
        "-nop4",
        "-nosplash",
        "-NullRHI",
        "-stdout",
        "-FullStdOutLogOutput",
        "-UTF8Output",
    ], cwd=project.parent, timeout=timeout)
    editor_output = f"{editor_result.stdout}\n{editor_result.stderr}"
    if editor_result.returncode != 0:
        raise SemanticEditorE2EError(
            f"Editor process failed with exit code {editor_result.returncode}")
    if not TEST_SUCCESS_PATTERN.search(editor_output) or SUITE_SUCCESS_MARKER not in editor_output:
        raise SemanticEditorE2EError("EditorRoundtrip did not report a successful Automation result")
    artifacts = ARTIFACT_PATTERN.findall(editor_output)
    if not artifacts:
        raise SemanticEditorE2EError("EditorRoundtrip did not report its exported artifact")
    artifact = Path(artifacts[-1].strip().strip('"')).resolve()
    if not artifact.is_file():
        raise SemanticEditorE2EError(f"reported Semantic JSON artifact does not exist: {artifact}")

    cli_result = _run([
        str(python_executable),
        "-m",
        "app.semantic_roundtrip",
        "--provider",
        "mock",
        str(artifact),
    ], cwd=python_server, timeout=timeout)
    if cli_result.returncode != 0:
        detail = cli_result.stderr.strip() or cli_result.stdout.strip()
        raise SemanticEditorE2EError(f"Semantic JSON CLI validation failed: {detail}")
    reports = []
    for line in cli_result.stdout.splitlines():
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            reports.append(value)
    if not reports or reports[-1].get("status") != "ok" or \
            reports[-1].get("provider_status") != "mocked":
        raise SemanticEditorE2EError("Semantic JSON CLI did not return a validated mock-provider report")

    return {
        "status": "ok",
        "artifact": str(artifact),
        "asset_path": reports[-1].get("asset_path"),
        "semantic_status": reports[-1].get("semantic_status"),
        "variables": reports[-1].get("variables"),
        "functions": reports[-1].get("functions"),
        "provider_status": reports[-1].get("provider_status"),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Export Semantic JSON through UE Editor and validate it with the Python CLI")
    parser.add_argument("--editor", required=True, type=Path, help="UnrealEditor-Cmd executable")
    parser.add_argument("--project", required=True, type=Path, help="Unreal .uproject file")
    parser.add_argument("--python", type=Path, default=Path(sys.executable))
    parser.add_argument("--python-server", type=Path)
    parser.add_argument("--timeout", type=int, default=300, help="Per-process timeout in seconds")
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        report = run_e2e(
            args.editor,
            args.project,
            python_executable=args.python,
            python_server=args.python_server,
            timeout=args.timeout,
        )
    except SemanticEditorE2EError as exc:
        print(json.dumps({"status": "error", "message": str(exc)}, ensure_ascii=False))
        return 1
    print(json.dumps(report, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
