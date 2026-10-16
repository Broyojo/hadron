#!/usr/bin/env python3
"""Check the real resolver against main launches, helpers and invalid selections."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class EntryPointTests(unittest.TestCase):
    def test_routes(self):
        with tempfile.TemporaryDirectory(prefix="hadron entry ") as folder:
            root = Path(folder)
            (root / "Game.exe").touch()
            (root / "Game with spaces.exe").touch()
            (root / "nested").mkdir()
            (root / "link.exe").symlink_to("Game.exe")
            original = str(root / "launcher.exe")
            cases = [
                ("Game.exe", "waitforexitandrun", original, str(root / "Game.exe")),
                ("Game with spaces.exe", "waitforexitandrun", original, str(root / "Game with spaces.exe")),
                ("", "waitforexitandrun", original, original),
                ("Game.exe", "run", original, original),
                ("Game.exe", "waitforexitandrun", str(root / "nested/helper.exe"), str(root / "nested/helper.exe")),
                ("missing.exe", "waitforexitandrun", original, None),
                ("../outside.exe", "waitforexitandrun", original, None),
                ("nested/helper.exe", "waitforexitandrun", original, None),
                ("nested\\helper.exe", "waitforexitandrun", original, None),
                ("link.exe", "waitforexitandrun", original, None),
                ("nested", "waitforexitandrun", original, None),
                (".", "waitforexitandrun", original, None),
                ("..", "waitforexitandrun", original, None),
            ]
            # Nested launchers are redirected only when explicitly named, preserving helpers.
            for launcher, program, expected in [
                ("nested/launcher.exe", str(root / "nested/launcher.exe"), str(root / "Game.exe")),
                ("nested/launcher.exe", str(root / "nested/helper.exe"), str(root / "nested/helper.exe")),
                ("nested/launcher.exe", original, original),
                ("../launcher.exe", original, None),
                ("/launcher.exe", original, None),
                ("nested/./launcher.exe", original, None),
                ("nested/../launcher.exe", original, None),
                ("nested\\launcher.exe", original, None),
            ]:
                with self.subTest(launcher=launcher):
                    result = subprocess.run(["sh", "-c", '. "$1"; steam_entrypoint "$2" "$3" "$4" "$5" "$6"',
                        "test", str(ROOT / "scripts/steam-entrypoint.sh"), "waitforexitandrun", folder,
                        program, "Game.exe", launcher], text=True, capture_output=True)
                    if expected is None:
                        self.assertNotEqual(result.returncode, 0)
                    else:
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(result.stdout.strip(), expected)
            for choice, verb, program, expected in cases:
                with self.subTest(choice=choice, verb=verb, program=program):
                    result = subprocess.run(["sh", "-c", '. "$1"; steam_entrypoint "$2" "$3" "$4" "$5"',
                        "test", str(ROOT / "scripts/steam-entrypoint.sh"), verb, folder, program, choice],
                        text=True, capture_output=True, env=os.environ)
                    if expected is None:
                        self.assertNotEqual(result.returncode, 0)
                    else:
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(result.stdout.strip(), expected)

if __name__ == "__main__":
    unittest.main()
