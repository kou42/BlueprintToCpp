import pytest

from app.ai_provider import GeneratedFile
from app.generated_output_validator import (
    GeneratedOutputError, validate_generated_files, validate_generation_result,
)


def test_valid_cpp_pair():
    files = [{"path": "Example.h", "content": "#pragma once\n"},
             {"path": "Example.cpp", "content": "#include \"Example.h\"\n"}]
    assert len(validate_generation_result("generated", files)) == 2


def test_provider_dataclass_supported():
    files = (GeneratedFile("Example.cpp", "int value = 1;"),)
    assert validate_generation_result("generated", files)[0]["path"] == "Example.cpp"


@pytest.mark.parametrize("path", [
    "../escape.cpp", "/tmp/file.cpp", "C:/Windows/file.cpp",
    "folder\\escape.cpp", "//server/share.cpp", "dir//file.cpp",
    "dir/./file.cpp", "dir/../file.cpp", "file.txt", "~/.cpp",
    "file.cpp:stream", "file name.cpp",
])
def test_unsafe_paths_rejected(path):
    with pytest.raises(GeneratedOutputError):
        validate_generated_files([{"path": path, "content": "int x;"}])


def test_duplicate_paths_case_insensitive():
    with pytest.raises(GeneratedOutputError):
        validate_generated_files([
            {"path": "A.cpp", "content": "int a;"},
            {"path": "a.cpp", "content": "int b;"},
        ])


@pytest.mark.parametrize("files", [
    [{"path": "A.cpp", "content": ""}],
    [{"path": "A.cpp", "content": "\x00"}],
    [{"path": "A.cpp", "content": "x", "extra": 1}],
    [{"path": "A.cpp", "content": "x" * 2_000_001}],
])
def test_invalid_content_or_structure(files):
    with pytest.raises(GeneratedOutputError):
        validate_generated_files(files)


@pytest.mark.parametrize("status,files", [
    ("generated", []),
    ("mocked", [{"path": "A.cpp", "content": "int x;"}]),
    ("rejected", [{"path": "A.cpp", "content": "int x;"}]),
    ("unknown", []),
])
def test_status_file_consistency(status, files):
    with pytest.raises(GeneratedOutputError):
        validate_generation_result(status, files)


def test_mock_and_rejected_are_empty():
    assert validate_generation_result("mocked", ()) == ()
    assert validate_generation_result("rejected", ()) == ()
