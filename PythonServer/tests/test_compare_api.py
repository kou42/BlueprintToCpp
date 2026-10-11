from fastapi.testclient import TestClient

from app.main import app

client = TestClient(app)


def payload(direct_content="int x = 1;\n", ai_content="int x = 2;\n"):
    return {
        "direct": {"status": "generated", "files": [{"path": "Example.cpp", "content": direct_content}]},
        "ai": {"status": "generated", "files": [{"path": "Example.cpp", "content": ai_content}]},
    }


def test_compare_api_reports_diff():
    response = client.post("/compare", json=payload())
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "different"
    assert data["equivalent"] is None
    assert data["files"][0]["kind"] == "modified"
    assert len(data["direct_fingerprint"]) == 64


def test_compare_api_identical_not_approved():
    response = client.post("/compare", json=payload(ai_content="int x = 1;\n"))
    assert response.status_code == 200
    assert response.json()["status"] == "identical"
    assert response.json()["equivalent"] is None


def test_compare_api_mock_unavailable():
    request = payload()
    request["ai"] = {"status": "mocked", "files": [], "diagnostics": ["Mock only"]}
    response = client.post("/compare", json=request)
    assert response.status_code == 200
    assert response.json()["status"] == "unavailable"


def test_compare_api_rejects_unsafe_path():
    request = payload()
    request["ai"]["files"][0]["path"] = "../Example.cpp"
    response = client.post("/compare", json=request)
    assert response.status_code == 422


def test_compare_api_rejects_extra_input_fields():
    request = payload()
    request["ai"]["secret"] = "unexpected"
    response = client.post("/compare", json=request)
    assert response.status_code == 422
