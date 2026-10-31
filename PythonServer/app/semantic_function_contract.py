"""Shared C++ name mapping for the supported Semantic generator contract."""


def generated_function_name(function: dict) -> str:
    name = function["name"]
    if name != "BeginPlay" and (function.get("is_event_graph", False) or
                                function.get("is_interface_implementation", False) or
                                function.get("is_override", False)):
        return name + "_Implementation"
    return name
