import unittest

from app.validate_copilot_graph_json import validate


A = "00000001-0000-0000-0000-000000000001"
B = "00000002-0000-0000-0000-000000000002"
C = "00000003-0000-0000-0000-000000000003"
D = "00000004-0000-0000-0000-000000000004"


def fixture():
    return {
        "schema_version": 1, "format": "blueprint_copilot_graph",
        "semantic_status": "partial", "asset_path": "/Game/BP_Test",
        "graphs": [{
            "path": "EventGraph",
            "nodes": [
                {"id": A, "pins": [{"id": B, "direction": "output"}]},
                {"id": C, "pins": [{"id": D, "direction": "input"}]},
            ],
            "exec_edges": [{"source_node_id": A, "source_pin_id": B,
                            "target_node_id": C, "target_pin_id": D}],
            "data_edges": [],
        }],
    }


class CopilotGraphValidatorTests(unittest.TestCase):
    def test_valid_graph(self):
        self.assertEqual(validate(fixture()), [])

    def test_missing_endpoint(self):
        data = fixture()
        data["graphs"][0]["exec_edges"][0]["target_pin_id"] = B
        self.assertTrue(any("invalid target endpoint" in e for e in validate(data)))

    def test_duplicate_pin(self):
        data = fixture()
        data["graphs"][0]["nodes"][1]["pins"][0]["id"] = B
        self.assertTrue(any("duplicate pin GUID" in e for e in validate(data)))

    def test_invalid_version(self):
        data = fixture()
        data["schema_version"] = 2
        self.assertIn("unsupported schema_version", validate(data))


if __name__ == "__main__":
    unittest.main()
