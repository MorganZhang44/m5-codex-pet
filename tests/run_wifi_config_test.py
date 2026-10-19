"""Test real provisioning code with fake hardware and no private configuration."""
import pathlib
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="pet-wifi-test-") as directory:
    stage = pathlib.Path(directory)
    # Quote includes resolve here first: never load pet_config.local.h.
    shutil.copyfile(root / "src/wifi_config.cpp", stage / "wifi_config.cpp")
    for legacy in (True, False):
        (stage / "pet_config.h").write_text(
            '#define PET_WIFI_SSID "test-legacy-network"\n'
            '#define PET_WIFI_PASSWORD "test-legacy-pass"\n' if legacy else
            '#define PET_WIFI_SSID ""\n#define PET_WIFI_PASSWORD ""\n'
        )
        command = ["c++", "-std=c++11", "-Wall", "-Wextra", "-pedantic",
                   "-I" + str(root / "tests/wifi_stubs"), "-I" + str(root / "src"),
                   str(root / "tests/wifi_config_test.cpp"), str(stage / "wifi_config.cpp"), str(root / "src/bridge_config.cpp"),
                   "-o", str(stage / "wifi-test")]
        if not legacy:
            command.append("-DTEST_NO_LEGACY")
        subprocess.run(command, check=True)
        for scenario in (("legacy",) if legacy else ()) + ("saved", "absent", "corrupt", "cleared", "setup", "online", "paired", "pair-corrupt", "pair-cleared"):
            subprocess.run([str(stage / "wifi-test"), scenario], check=True)
print("Wi-Fi provisioning/recovery tests passed (synthetic credentials only).")
