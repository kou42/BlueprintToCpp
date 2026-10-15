import unittest

from app.compare_copilot_graph_golden import compare_golden
def fixture():
    a = "00000001-0000-0000-0000-000000000001"
    b = "00000002-0000-0000-0000-000000000002"
    c = "00000003-0000-0000-0000-000000000003"
    d = "00000004-0000-0000-0000-000000000004"
    return {
        "schema_version": 1, "format": "blueprint_copilot_graph",
        "semantic_status": "partial", "asset_path": "/Game/BP_Test",
        "diagnostics": [],
        "graphs": [{
            "path": "EventGraph",
            "nodes": [
                {"id": a, "pins": [{"id": b, "direction": "output"}]},
                {"id": c, "pins": [{"id": d, "direction": "input"}]},
            ],
            "exec_edges": [{
                "source_node_id": a, "source_pin_id": b,
                "target_node_id": c, "target_pin_id": d,
            }],
            "data_edges": [],
        }],
    }


class GoldenComparisonTests(unittest.TestCase):
    def test_identical_exports(self):
        self.assertEqual(compare_golden(fixture(), fixture()), [])

    def test_changed_default_value_is_reported(self):
        expected = fixture()
        actual = fixture()
        expected["graphs"][0]["nodes"][0]["pins"][0]["default_value"] = ""
        actual["graphs"][0]["nodes"][0]["pins"][0]["default_value"] = "42"
        self.assertTrue(any("default_value" in error for error in compare_golden(expected, actual)))

    def test_missing_pin_is_not_ignored(self):
        expected = fixture()
        actual = fixture()
        actual["graphs"][0]["nodes"][1]["pins"] = []
        self.assertNotEqual(compare_golden(expected, actual), [])

    def test_changed_guid_is_not_normalized(self):
        expected = fixture()
        actual = fixture()
        actual["graphs"][0]["nodes"][0]["id"] = "00000005-0000-0000-0000-000000000005"
        self.assertNotEqual(compare_golden(expected, actual), [])

    def test_extra_field_is_reported(self):
        expected = fixture()
        actual = fixture()
        actual["graphs"][0]["nodes"][0]["new_field"] = "value"
        self.assertTrue(any("new_field" in error for error in compare_golden(expected, actual)))


if __name__ == "__main__":
    unittest.main()
