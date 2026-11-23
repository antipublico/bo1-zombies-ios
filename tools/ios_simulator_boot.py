"""Start an ephemeral iPhone simulator before compiling native iOS targets.

GitHub-hosted macOS runners are disposable; booting the simulator can take
minutes. Overlap boot time with Xcode builds, then hand its UDID to the normal
smoke runner. No game files are needed.
"""

import argparse
from pathlib import Path
from ios_simulator_smoke import preferred_iphone, simctl


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    runtime, device = preferred_iphone()
    udid = simctl("create", "BO1 CI", device["deviceTypeIdentifier"], runtime).stdout.strip()
    if not udid:
        raise RuntimeError("Simulator creation did not provide a UDID")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(udid + "\n", encoding="utf-8")
    simctl("boot", udid)
    print(f"Simulator boot started while Xcode builds: {device['name']} ({udid})", flush=True)


if __name__ == "__main__":
    main()
