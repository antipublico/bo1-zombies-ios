"""Run the development app in a disposable iPhone simulator on a macOS CI host."""

import argparse
import json
from pathlib import Path
import subprocess
import time


def simctl(*arguments, timeout=120, check=True):
    result = subprocess.run(
        ["xcrun", "simctl", *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )
    if check and result.returncode:
        raise RuntimeError(f"simctl {arguments[0]} failed:\n{result.stdout}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.app.is_dir():
        raise SystemExit(f"App bundle is missing: {args.app}")
    args.output.mkdir(parents=True, exist_ok=True)

    inventory = json.loads(simctl("list", "devices", "available", "--json").stdout)
    candidates = [
        (runtime, device)
        for runtime, devices in inventory["devices"].items()
        if ".iOS-" in runtime
        for device in devices
        if device.get("isAvailable")
        and device["name"].startswith("iPhone")
        and device.get("deviceTypeIdentifier")
    ]
    if not candidates:
        raise RuntimeError("No available iPhone runtime is installed on this runner")
    # Prefer the SE screen size when the installed runtime supports it.
    candidates.sort(key=lambda item: ("iPhone SE" not in item[1]["name"], item[1]["name"]))
    runtime, template = candidates[0]
    device_id = simctl(
        "create", "BO1 CI", template["deviceTypeIdentifier"], runtime
    ).stdout.strip()
    bundle_id = "com.antipublico.bo1zombies.dev"
    (args.output / "device.json").write_text(
        json.dumps({"name": template["name"], "runtime": runtime, "udid": device_id}, indent=2)
    )
    print(f"Testing {template['name']} with {runtime}", flush=True)
    try:
        simctl("boot", device_id)
        simctl("bootstatus", device_id, "-b", timeout=240)
        simctl("install", device_id, str(args.app.resolve()))
        result = simctl(
            "launch", "--console", "--terminate-running-process",
            device_id, bundle_id, "--self-test", timeout=90, check=False,
        )
        (args.output / "self-test.log").write_text(result.stdout)
        print(result.stdout, flush=True)
        if result.returncode or "BO1_IOS_SELF_TEST_PASS" not in result.stdout:
            raise RuntimeError("The iOS app did not pass its native component self-test")

        simctl("launch", device_id, bundle_id)
        time.sleep(3)
        simctl("io", device_id, "screenshot", str(args.output / "startup.png"))
        simctl("launch", "--terminate-running-process", device_id, bundle_id, "--fixture-preview")
        time.sleep(3)
        simctl("io", device_id, "screenshot", str(args.output / "native-assets.png"))
        print("iOS simulator self-test passed; startup screenshot saved.", flush=True)
    finally:
        simctl("shutdown", device_id, check=False)
        simctl("delete", device_id, check=False)


if __name__ == "__main__":
    main()
