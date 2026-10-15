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
        "diagnostics": [],
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

    def test_resolved_function_requires_identity(self):
        data = fixture()
        data["graphs"][0]["nodes"][0]["function_resolved"] = True
        self.assertTrue(any("missing identity" in e for e in validate(data)))

    def test_diagnostics_sorted(self):
        data = fixture()
        data["diagnostics"] = ["z", "a"]
        self.assertIn("diagnostics are not sorted", validate(data))

    def test_target_self_pin_is_valid(self):
        data = fixture()
        node = data["graphs"][0]["nodes"][0]
        node["function_target_kind"] = "self"
        node["function_target_pin_id"] = B
        node["function_target_linked_pin_ids"] = []
        self.assertEqual(validate(data), [])

    def test_target_pin_must_belong_to_node(self):
        data = fixture()
        node = data["graphs"][0]["nodes"][0]
        node["function_target_kind"] = "explicit_target"
        node["function_target_pin_id"] = D
        node["function_target_linked_pin_ids"] = [D]
        self.assertTrue(any("invalid function_target_pin_id" in e for e in validate(data)))

    def test_invalid_target_kind(self):
        data = fixture()
        node = data["graphs"][0]["nodes"][0]
        node["function_target_kind"] = "guessed"
        node["function_target_pin_id"] = None
        node["function_target_linked_pin_ids"] = []
        self.assertTrue(any("invalid function_target_kind" in e for e in validate(data)))

    def test_dispatch_and_latent(self):
        data = fixture()
        node = data["graphs"][0]["nodes"][0]
        node["function_dispatch_kind"] = "regular"
        node["function_latent"] = True
        self.assertEqual(validate(data), [])
        node["function_dispatch_kind"] = "unknown"
        self.assertTrue(any("invalid function_dispatch_kind" in e for e in validate(data)))
        node["function_dispatch_kind"] = "regular"
        node["function_latent"] = "true"
        self.assertTrue(any("invalid function_latent" in e for e in validate(data)))

    def test_invalid_version(self):
        data = fixture()
        data["schema_version"] = 2
        self.assertIn("unsupported schema_version", validate(data))


if __name__ == "__main__":
    unittest.main()
