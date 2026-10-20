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
            if include in ["core.h", "partybox.h", "indicator.h", "target.h"]:
                continue
            stub = tmp / include
            stub.parent.mkdir(parents=True, exist_ok=True)
            contents = '#include "host.h"\n'
            if include == "esphome/core/component.h":
                contents += "#ifdef TEST_LIGHT\n#define USE_LIGHT\n#endif\n"
            if include == "esphome/components/light/light_state.h":
                contents += """
namespace esphome::light {
class LightCall {
 public:
  LightCall& set_state(bool) { return *this; }
  LightCall& set_transition_length(unsigned) { return *this; }
  LightCall& set_brightness(float) { return *this; }
  LightCall& set_rgb(float, float, float) { return *this; }
  LightCall& set_publish(bool) { return *this; }
  LightCall& set_save(bool) { return *this; }
  void perform() {}
};
class LightState { public: int calls{0}; LightCall make_call() { ++calls; return {}; } };
}
"""
            stub.write_text(contents)
    exe = tmp / "runtime"
    # AUTO_LOAD exposes the entity classes in every configuration, but the
    # registry methods exist only when an entity of that domain is configured.
    # Cover absent domains independently and retain the registry-free build.
    for name, features in [
        ("optional_light_header_order", ["TEST_LIGHT"]),
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
        if name == "optional_light_header_order":
            subprocess.run([str(exe), "indicator_setup_failure"], check=True)
    failures = []
    for case in [
        "target_restore",
        "pairing_window",
        "pairing_ambiguity",
        "pairing_lifecycle",
        "target_restart_owner",
        "indicator_activity",
        "indicator_spp_activity",
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
        "input_signal_flags",
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
        if subprocess.run([str(exe), case], check=False).returncode:
            failures.append(case)
    if failures:
        raise SystemExit("FAILED runtime regressions: " + ", ".join(failures))
