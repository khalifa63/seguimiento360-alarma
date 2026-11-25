"""Validate public engineering manifests; never access devices or private releases."""
import re
from pathlib import Path

import yaml

SHA = re.compile(r"[0-9a-f]{40}")
HASH = re.compile(r"[0-9a-f]{64}")
VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
IDENT = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}")
FIRMWARE = {"hub", "ultrasonico", "camara", "buzzer", "reed", "panico"}
SERVICES = {"backend", "frontend", "infrastructure"}


def validate(system, matrix, root):
    def require(condition, label):
        if not condition:
            raise ValueError(label)

    def optional(value, pattern, label):
        require(value is None or (isinstance(value, str)
                and pattern.fullmatch(value)), label)

    require(system["schema_version"] == matrix["schema_version"] == 2,
            "schema_version")
    require(system["project"] == "seguimiento360.com", "project")
    require(system["status"] in {"draft", "released"}, "system status")
    require(matrix["status"] in {"draft", "reviewed"}, "matrix status")
    optional(system["system_version"], VERSION, "system version")
    optional(system["protocol_version"], IDENT, "system protocol")
    components = system["components"]
    require(set(components) == FIRMWARE | SERVICES, "component coverage")
    by_id = {}
    for name, item in components.items():
        require(isinstance(item["id"], str)
                and re.fullmatch(r"S360-[A-Z0-9-]+", item["id"])
                and item["id"] not in by_id, "component id")
        by_id[item["id"]] = item
        source = Path(item["source"])
        resolved = (root / source).resolve()
        require(not source.is_absolute() and resolved.is_relative_to(root.resolve())
                and resolved.is_dir(), "component source")
        optional(item["source_commit"], SHA, "source commit")
        if name in FIRMWARE:
            for field in ("firmware_version", "manual_version"):
                optional(item[field], VERSION, field)
            for field in ("build", "hardware_revision", "mechanical_revision",
                          "protocol_version"):
                optional(item[field], IDENT, field)
            optional(item["artifact_sha256"], HASH, "artifact hash")
            if item["build"] is not None:
                require(all(item[x] is not None for x in
                            ("source_commit", "firmware_version", "artifact_sha256")),
                        "build provenance")
        else:
            optional(item["version"], VERSION, "service version")
    hub_id = components["hub"]["id"]
    expected = {components[n]["id"] for n in FIRMWARE - {"hub"}}
    seen = set()
    compatible = set()
    for row in matrix["combinations"]:
        node = row["node"]
        require(row["hub"] == hub_id and node in expected and node not in seen,
                "compatibility coverage")
        seen.add(node)
        require(row["interface"] in {"esp-now", "system-integration"}, "interface")
        require(row["status"] in {"not_tested", "compatible", "incompatible"},
                "compatibility status")
        for field in ("hub_build", "node_build", "protocol_version", "evidence"):
            optional(row[field], IDENT, field)
        if row["status"] != "not_tested":
            require(all(row[x] is not None for x in
                        ("hub_build", "node_build", "protocol_version", "evidence")),
                    "compatibility evidence")
            require(row["hub_build"] == by_id[hub_id]["build"]
                    and row["node_build"] == by_id[node]["build"],
                    "compatibility build mismatch")
        if row["status"] == "compatible":
            compatible.add(node)
    require(seen == expected, "missing compatibility row")
    validation = system["validation"]
    require(type(validation["approved"]) is bool, "approval type")
    require(validation["integration"] in {"pending", "passed", "failed"},
            "integration status")
    optional(validation["evidence"], IDENT, "integration evidence")
    if validation["integration"] == "passed":
        require(validation["evidence"] is not None, "integration evidence required")
    if system["status"] == "released" or validation["approved"]:
        require(system["status"] == "released" and validation["approved"]
                and validation["integration"] == "passed"
                and matrix["status"] == "reviewed"
                and system["system_version"] is not None
                and system["protocol_version"] is not None
                and compatible == expected, "release approval gate")
        for name, item in components.items():
            fields = (("firmware_version", "source_commit", "build", "artifact_sha256",
                       "hardware_revision", "mechanical_revision", "manual_version",
                       "protocol_version") if name in FIRMWARE
                      else ("version", "source_commit"))
            require(all(item[x] is not None for x in fields), "release completeness")


def main():
    root = Path(__file__).resolve().parents[1]
    try:
        system = yaml.safe_load((root / "SYSTEM_MANIFEST.yaml").read_text())
        matrix = yaml.safe_load((root / "COMPATIBILITY.yaml").read_text())
        validate(system, matrix, root)
    except (ValueError, KeyError, TypeError, AttributeError, OSError, yaml.YAMLError):
        print("Invalid manifest: review schema and provenance locally.")
        return 1
    print("Manifest structure valid; no physical acceptance implied.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
