"""Compile actual PartyBox event/loop code with a minimal host SDK/framework fixture."""

import re
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="partybox-runtime-") as folder:
    tmp = Path(folder)
    for source in [
        root / "components/partybox/partybox.h",
        root / "components/partybox/partybox.cpp",
    ]:
        for include in re.findall(r'#include "([^"]+)"', source.read_text()):
            if include in ["core.h", "partybox.h"]:
                continue
            stub = tmp / include
            stub.parent.mkdir(parents=True, exist_ok=True)
            stub.write_text('#include "host.h"\n')
    exe = tmp / "runtime"
    # AUTO_LOAD exposes the entity classes in every configuration, but the
    # registry methods exist only when an entity of that domain is configured.
    # Cover absent domains independently and retain the registry-free build.
    for name, features in [
        ("no_registry", []),
        ("registry_without_optional_domains", ["USE_CONTROLLER_REGISTRY"]),
        ("registry_text_only", ["USE_CONTROLLER_REGISTRY", "USE_TEXT_SENSOR"]),
        ("registry_select_only", ["USE_CONTROLLER_REGISTRY", "USE_SELECT"]),
        ("registry_full", ["USE_CONTROLLER_REGISTRY", "USE_TEXT_SENSOR", "USE_SELECT"]),
    ]:
        subprocess.run(
            [
                shutil.which("clang++") or "g++",
                "-std=c++17",
                *("-D" + feature for feature in features),
                "-I",
                str(tmp),
                "-I",
                str(root / "tests/partybox"),
                "-I",
                str(root / "components/partybox"),
                str(root / "components/partybox/partybox.cpp"),
                str(root / "tests/partybox/runtime_test.cpp"),
                "-o",
                str(exe),
            ],
            check=True,
        )
        print("PASS: runtime compile " + name, flush=True)
    failures = []
    for case in [
        "source",
        "real_spp_source_ack",
        "overflow_handle",
        "overflow_fence",
        "connect_timeout",
        "power_guard",
        "spp_mailbox_close",
        "spp_failure_active",
        "physical_report_wins",
        "ha_unknown_state_compatibility",
        "protocol_entity_invalidation",
        "radio_eir_summary",
        "a2dp_telemetry",
        "spp_disabled",
        "spp_default_enabled",
        "rssi_worker",
        "rssi_worker_no_queue",
        "rssi_worker_no_task",
        "gap_telemetry",
        "diagnostic_burst",
        "native_tx_failure",
        "explicit_connect_failure",
    ]:
        if subprocess.run([str(exe), case]).returncode:
            failures.append(case)
    if failures:
        raise SystemExit("FAILED runtime regressions: " + ", ".join(failures))
