"""Host regression tests for experimental artifact isolation and upload guards."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


exporter = load_module("nooir_experimental_exporter", ROOT / "scripts" / "export_experimental_firmware.py")
upload_guard = load_module("nooir_upload_guard", ROOT / "scripts" / "guard_nooir_upload.py")


class FirmwareArtifactTests(unittest.TestCase):
    def test_both_targets_use_original_unchanged_app_slot_bound(self):
        before = (ROOT / "partitions.csv").read_bytes()
        for device in ("X4PRO", "X4CLASSIC"):
            metadata = exporter.partition_metadata(ROOT, device)
            self.assertEqual(metadata["build_partition_table"], "partitions.csv")
            self.assertEqual(metadata["build_app_slot_size_bytes"], 0x640000)
            self.assertEqual(metadata["partition_table_sha256"], hashlib.sha256(before).hexdigest())
        self.assertEqual((ROOT / "partitions.csv").read_bytes(), before)

    def test_export_is_target_marked_app_only_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "firmware.bin"
            source.write_bytes(b"image NOOIR_FIRMWARE_TARGET=X4PRO\0 payload")
            requested = root / "output" / "folio-nooir-x4pro.bin"
            requested.parent.mkdir()
            requested.write_bytes(b"previous artifact")
            metadata = {
                "device": "X4PRO",
                "experimental": True,
                "flash_performed": False,
                "build_partition_table": "partitions.csv",
                "build_app_slot_size_bytes": 1024,
            }

            output, digest = exporter.export_experimental_firmware(source, requested, metadata)

            self.assertEqual(requested.read_bytes(), b"previous artifact")
            self.assertEqual(output.name, "folio-nooir-x4pro-2.bin")
            self.assertEqual(output.read_bytes(), source.read_bytes())
            self.assertEqual(digest, hashlib.sha256(source.read_bytes()).hexdigest())
            manifest = json.loads(output.with_suffix(".bin.manifest.json").read_text())
            self.assertEqual(manifest["sha256"], digest)
            self.assertEqual(manifest["image_role"], "ESP32-S3 application image only")
            self.assertEqual(manifest["release_version"], "1.6.7")
            self.assertEqual(manifest["physical_validation"], "pending")
            self.assertTrue(manifest["experimental"])
            self.assertFalse(manifest["flash_performed"])
            self.assertFalse(manifest["is_for_flashing"])

    def test_wrong_target_or_unsafe_artifact_metadata_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "firmware.bin"
            source.write_bytes(b"image NOOIR_FIRMWARE_TARGET=X4PRO\0")
            destination = root / "out.bin"
            valid = {"device": "X4PRO", "experimental": True, "flash_performed": False,
                     "build_app_slot_size_bytes": 1024}
            with self.assertRaisesRegex(ValueError, "target marker"):
                exporter.export_experimental_firmware(source, destination, {**valid, "device": "X4CLASSIC"})
            with self.assertRaisesRegex(ValueError, "flash_performed=false"):
                exporter.export_experimental_firmware(source, destination, {**valid, "flash_performed": True})
            with self.assertRaisesRegex(ValueError, "app-slot size"):
                exporter.export_experimental_firmware(source, destination, {**valid, "build_app_slot_size_bytes": 4})
            with self.assertRaisesRegex(ValueError, "bounded app-slot"):
                exporter.export_experimental_firmware(source, destination, {key: value for key, value in valid.items()
                                                                           if key != "build_app_slot_size_bytes"})
            self.assertFalse(destination.exists())

    def test_upload_guard_blocks_hardware_writes_for_experimental_targets(self):
        for device in ("X4PRO", "X4CLASSIC"):
            for target in ("upload", "uploadfs", "uploadbootloader", "uploadota", "erase", "erase_flash"):
                with self.subTest(device=device, target=target):
                    with self.assertRaises(RuntimeError):
                        upload_guard.validate_targets(device, [target])

    def test_upload_guard_allows_builds_and_preserves_c3_behavior(self):
        upload_guard.validate_targets("X4PRO", ["build"])
        upload_guard.validate_targets("X4CLASSIC", ["build"])
        upload_guard.validate_targets("X3X4", ["upload"])


if __name__ == "__main__":
    unittest.main()
