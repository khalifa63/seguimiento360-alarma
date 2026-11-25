"""Tests use draft public fixtures; no hardware or network."""
import copy
import unittest
from pathlib import Path

import yaml

from scripts.check_manifests import validate

ROOT = Path(__file__).resolve().parents[1]


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.system = yaml.safe_load((ROOT / "SYSTEM_MANIFEST.yaml").read_text())
        self.matrix = yaml.safe_load((ROOT / "COMPATIBILITY.yaml").read_text())

    def check(self):
        validate(self.system, self.matrix, ROOT)

    def test_current_manifest(self):
        self.check()

    def test_duplicate_id(self):
        self.system["components"]["reed"]["id"] = self.system["components"]["hub"]["id"]
        with self.assertRaises(ValueError):
            self.check()

    def test_missing_component(self):
        del self.system["components"]["reed"]
        with self.assertRaises(ValueError):
            self.check()

    def test_source_escape(self):
        self.system["components"]["hub"]["source"] = "../"
        with self.assertRaises(ValueError):
            self.check()

    def test_build_without_provenance(self):
        self.system["components"]["hub"]["build"] = "test-1"
        with self.assertRaises(ValueError):
            self.check()

    def test_compatibility_without_evidence(self):
        self.matrix["combinations"][0]["status"] = "compatible"
        with self.assertRaises(ValueError):
            self.check()

    def test_duplicate_combination(self):
        self.matrix["combinations"].append(copy.deepcopy(self.matrix["combinations"][0]))
        with self.assertRaises(ValueError):
            self.check()

    def test_unapproved_release(self):
        self.system["status"] = "released"
        with self.assertRaises(ValueError):
            self.check()

    def test_approval_without_release(self):
        self.system["validation"]["approved"] = True
        with self.assertRaises(ValueError):
            self.check()
