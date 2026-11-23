"""Run the development app in a disposable iPhone simulator on a macOS CI host."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
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


def preferred_iphone():
    """Use the same simulator model for early boot and the smoke runner."""
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
    candidates.sort(key=lambda item: ("iPhone SE" not in item[1]["name"], item[1]["name"]))
    return candidates[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prebooted-udid", type=Path,
                        help="Simulator ID already booting while Xcode builds; optional")
    args = parser.parse_args()
    if not args.app.is_dir():
        raise SystemExit(f"App bundle is missing: {args.app}")
    args.output.mkdir(parents=True, exist_ok=True)

    runtime, template = preferred_iphone()
    if args.prebooted_udid is not None:
        device_id = args.prebooted_udid.read_text(encoding="utf-8").strip()
        if not device_id:
            raise RuntimeError("Prebooted iPhone device ID is empty")
    else:
        device_id = simctl(
            "create", "BO1 CI", template["deviceTypeIdentifier"], runtime
        ).stdout.strip()
    bundle_id = "com.antipublico.bo1zombies.dev"
    (args.output / "device.json").write_text(
        json.dumps({"name": template["name"], "runtime": runtime, "udid": device_id}, indent=2)
    )
    print(f"Testing {template['name']} with {runtime}", flush=True)
    try:
        if args.prebooted_udid is None:
            simctl("boot", device_id)
        simctl("bootstatus", device_id, "-b", timeout=240)
        simctl("install", device_id, str(args.app.resolve()))
        data_container = Path(simctl(
            "get_app_container", device_id, bundle_id, "data"
        ).stdout.strip())
        marker = data_container / "tmp" / "bo1-native-self-test.txt"
        marker.unlink(missing_ok=True)
        result = simctl(
            "launch", "--console", "--terminate-running-process",
            device_id, bundle_id, "--self-test", timeout=90, check=False,
        )
        # Xcode 26's simctl --console sometimes returns only 'bundle: PID'
        # when the app exits quickly. Prefer the explicit sandbox marker.
        status = ""
        for _ in range(40):
            if marker.is_file():
                status = marker.read_text(encoding="utf-8")
                if status:
                    break
            time.sleep(0.5)
        (args.output / "self-test.log").write_text(
            result.stdout + "\nSaved self-test marker: " + status + "\n"
        )
        print(result.stdout, flush=True)
        print("Saved self-test marker:", status, flush=True)
        if "BO1_IOS_SELF_TEST_PASS" not in status:
            raise RuntimeError(
                "The iOS app did not pass its native component self-test: "
                + (status or f"no marker (simctl exit {result.returncode})")
            )

        simctl("launch", device_id, bundle_id)
        time.sleep(3)
        simctl("io", device_id, "screenshot", str(args.output / "startup.png"))
        simctl("launch", "--terminate-running-process", device_id, bundle_id, "--fixture-preview")
        time.sleep(3)
        simctl("io", device_id, "screenshot", str(args.output / "native-assets.png"))

        # Decode a synthetic IWI through the document callback, wait for its
        # completed layout, then independently check the actual screen pixels.
        # A successful UIImage construction or a stale marker is insufficient.
        iwi_marker = data_container / "tmp" / "bo1-iwi-preview.json"
        iwi_marker.unlink(missing_ok=True)
        simctl("launch", "--terminate-running-process", device_id, bundle_id, "--fixture-iwi-preview")
        deadline = time.monotonic() + 60
        report = None
        while time.monotonic() < deadline:
            if iwi_marker.is_file():
                report = json.loads(iwi_marker.read_text(encoding="utf-8"))
                break
            time.sleep(0.25)
        screenshot = args.output / "iwi-preview.png"
        simctl("io", device_id, "screenshot", str(screenshot))
        if report is None:
            raise RuntimeError("No IWI preview layout marker was produced")
        report_path = args.output / "iwi-preview.json"
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        if report.get("status") != "pass":
            raise RuntimeError(f"IWI preview failed: {report}")

        checker_source = Path(__file__).with_name("check_iwi_screenshot.swift")
        with tempfile.TemporaryDirectory(prefix="bo1-screenshot-") as temporary:
            checker = str(Path(temporary) / "check-iwi")
            subprocess.run(["xcrun", "swiftc", str(checker_source), "-o", checker],
                           check=True, timeout=120)
            negative_checks = subprocess.run([checker, "--self-test"],
                capture_output=True, text=True, timeout=30)
            observations = [negative_checks.stdout, negative_checks.stderr]
            (args.output / "iwi-preview.log").write_text("\n".join(observations), encoding="utf-8")
            if negative_checks.returncode:
                raise RuntimeError("Screenshot checker self-test failed: " + "\n".join(observations))
            deadline = time.monotonic() + 20
            while True:
                simctl("io", device_id, "screenshot", str(screenshot))
                verified = subprocess.run([checker, str(screenshot), str(report_path)],
                    capture_output=True, text=True, timeout=10)
                observations.extend([verified.stdout, verified.stderr])
                (args.output / "iwi-preview.log").write_text("\n".join(observations), encoding="utf-8")
                if verified.returncode == 0:
                    print(verified.stdout, flush=True)
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError("IWI preview is not visibly correct in the simulator screenshot: "
                                       + verified.stdout + verified.stderr)
                time.sleep(0.5)
        # Separate 3D UI smoke: synthetic crossed planes textured by the same
        # portable IWI decode path. Real-game meshes are never checked into CI.
        model_marker = data_container / "tmp" / "bo1-model-preview.json"
        model_marker.unlink(missing_ok=True)
        simctl("launch", "--terminate-running-process", device_id, bundle_id,
               "--fixture-model-preview")
        deadline = time.monotonic() + 60
        model_report = None
        while time.monotonic() < deadline:
            if model_marker.is_file():
                model_report = json.loads(model_marker.read_text(encoding="utf-8"))
                break
            time.sleep(0.25)
        if model_report is None or model_report.get("status") != "pass":
            raise RuntimeError(f"3D textured model UI preview did not initialize: {model_report}")
        (args.output / "model-preview.json").write_text(
            json.dumps(model_report, indent=2), encoding="utf-8")
        time.sleep(3)  # Let the SceneKit GPU render at least a few frames.
        model_png = args.output / "model-preview.png"
        simctl("io", device_id, "screenshot", str(model_png))
        if not model_png.is_file() or model_png.stat().st_size < 5000:
            raise RuntimeError("3D model screenshot was not captured")
        # Nine instances (two synthetic source meshes) on a staged floor.
        # This checks the scene graph on the simulator without any BO1 assets.
        scene_marker = data_container / "tmp" / "bo1-scene-preview.json"
        scene_marker.unlink(missing_ok=True)
        simctl("launch", "--terminate-running-process", device_id, bundle_id,
               "--fixture-scene-preview")
        deadline = time.monotonic() + 60
        scene_report = None
        while time.monotonic() < deadline:
            if scene_marker.is_file():
                scene_report = json.loads(scene_marker.read_text(encoding="utf-8"))
                break
            time.sleep(0.25)
        if (scene_report is None or scene_report.get("status") != "pass"
                or scene_report.get("instances") != 9
                or scene_report.get("source_meshes") != 2
                or scene_report.get("has_ground") is not True):
            raise RuntimeError(f"Staged multi-model scene failed: {scene_report}")
        (args.output / "scene-preview.json").write_text(
            json.dumps(scene_report, indent=2), encoding="utf-8")
        time.sleep(3)
        scene_png = args.output / "scene-preview.png"
        simctl("io", device_id, "screenshot", str(scene_png))
        if not scene_png.is_file() or scene_png.stat().st_size < 5000:
            raise RuntimeError("No simulator screenshot for multi-model scene")
        # Full-screen first-person HUD, still using only synthetic meshes.
        # Check actual UIKit presentation and four touch controls; screenshot
        # is saved for human inspection without original game resources.
        explore_marker = data_container / "tmp" / "bo1-explore-preview.json"
        explore_marker.unlink(missing_ok=True)
        simctl("launch", "--terminate-running-process", device_id, bundle_id,
               "--fixture-explore-preview")
        deadline = time.monotonic() + 60
        explore_report = None
        while time.monotonic() < deadline:
            if explore_marker.is_file():
                explore_report = json.loads(explore_marker.read_text(encoding="utf-8"))
                break
            time.sleep(0.25)
        if (explore_report is None or explore_report.get("status") != "pass"
                or explore_report.get("mode") != "first-person"
                or explore_report.get("control_count") != 4
                or explore_report.get("scene_sources") != 2
                or explore_report.get("camera_z", -1) <= 0):
            raise RuntimeError(f"First-person HUD/camera failed: {explore_report}")
        (args.output / "explore-preview.json").write_text(
            json.dumps(explore_report, indent=2), encoding="utf-8")
        time.sleep(2)
        explore_png = args.output / "explore-preview.png"
        simctl("io", device_id, "screenshot", str(explore_png))
        if not explore_png.is_file() or explore_png.stat().st_size < 5000:
            raise RuntimeError("No simulator screenshot for first-person controls")
        print("iOS native, IWI, 3D model, staged scene and walking HUD passed; screenshots saved.",
              flush=True)
    finally:
        simctl("shutdown", device_id, check=False)
        simctl("delete", device_id, check=False)


if __name__ == "__main__":
    main()
