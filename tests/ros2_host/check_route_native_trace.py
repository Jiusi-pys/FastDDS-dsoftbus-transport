"""Check captured public node diagnostics; Host traces never prove SDK events."""
import argparse
import re
from pathlib import Path


def check(text):
    rows = []
    for line in text.splitlines():
        if line.startswith("ROS_STARTUP_EVENT "):
            row = dict(re.findall(r"(\w+)=([^ ]+)", line))
            row["name"] = row.get("operation")
            rows.append(row)
    names = [row.get("name") for row in rows]
    required = ("RegNode_enter", "RegNode_return", "OnNodeOffline_enter",
                "NodeOffline_processed", "OnNodeOnline_enter", "NodeOnline_processed",
                "GetAllNodes_enter", "GetAllNodes_return", "GetNodeUdid_enter",
                "GetNodeUdid_return", "NodeIdentity_match", "UnregNode_enter", "UnregNode_return")
    missing = [name for name in required if name not in names]
    assert not missing, "missing actual public boundary records: " + ",".join(missing)
    sequence = [names.index(name) for name in required[:11]]
    assert sequence == sorted(sequence), "node callback/product/resolver order"
    for name in ("RegNode_return", "NodeOffline_processed", "NodeOnline_processed",
                 "GetAllNodes_return", "GetNodeUdid_return", "UnregNode_return"):
        assert any(row.get("name") == name and row.get("value") == "0" for row in rows), name
    assert any(row.get("name") == "NodeIdentity_match" and row.get("value") == "1" for row in rows)
    for row in rows:
        if row.get("name") in required:
            assert row.get("redacted") == "0" and row.get("truncated") == "0", "incomplete diagnostic"
        if row.get("name") in ("RegNode_return", "UnregNode_return", "GetAllNodes_return", "GetNodeUdid_return"):
            assert row.get("value_type") == "SDK_RC" and row.get("sdk_return_observed") == "1", "SDK rc mislabeled as product status"
    captures = [line for line in text.splitlines() if line.startswith("ROS_STARTUP_CAPTURE ")]
    assert captures and all("completeness=CAPTURED" in line and "overflow=0" in line and "output_errors=0" in line for line in captures), "incomplete bounded capture"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("logs", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.logs:
        check(path.read_text(errors="replace"))
    print("PASS public node diagnostic capture; platform origin requires independent device evidence")
