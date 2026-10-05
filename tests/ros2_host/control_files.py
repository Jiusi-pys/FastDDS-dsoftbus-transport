"""Publish Host-only scheduling records; never transports business payload."""
from pathlib import Path


def publish_control(path: Path, text: str) -> None:
    # Each run owns a fresh directory and publishes each control once. Strict
    # readers must never observe the destination between open and close.
    pending = path.with_name(path.name + '.pending')
    pending.write_text(text, encoding='utf-8')
    pending.replace(path)
