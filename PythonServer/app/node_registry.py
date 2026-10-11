"""Explicit node converter registry; unsupported nodes fail closed."""
from dataclasses import dataclass
from typing import Callable


class UnsupportedNodeError(ValueError):
    pass


@dataclass(frozen=True)
class ConversionContext:
    node: dict
    graph: dict


class NodeConverterRegistry:
    def __init__(self) -> None:
        self._handlers: dict[str, Callable[[ConversionContext], str]] = {}

    def register(self, node_class: str, handler: Callable[[ConversionContext], str]) -> None:
        if node_class in self._handlers:
            raise ValueError(f"duplicate converter: {node_class}")
        self._handlers[node_class] = handler

    def convert(self, context: ConversionContext) -> str:
        cls = context.node["class"]
        handler = self._handlers.get(cls)
        if handler is None:
            raise UnsupportedNodeError(f"unsupported node class: {cls}")
        return handler(context)

    @property
    def supported_classes(self) -> tuple[str, ...]:
        return tuple(sorted(self._handlers))
