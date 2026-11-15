import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "audit_mt8163_vdec_source.py"


class VdecSourceAuditTests(unittest.TestCase):
    def run_audit(self, files: dict[str, str]) -> dict[str, object]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative, contents in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            completed = subprocess.run(
                ["python3", str(SCRIPT), str(root)],
                check=True,
                capture_output=True,
                text=True,
            )
            return json.loads(completed.stdout)

    @staticmethod
    def loader_files() -> dict[str, str]:
        return {
            "omx/core/src/MtkOmxCore.cpp": (
                'void *module = dlopen("libMtkOmxVdec.so", 1);\n'
                'dlsym(module, "MtkOmxComponentCreate");\n'
            ),
            "omx/plugin/libstagefrighthw/MtkOMXPlugin.cpp": (
                'dlopen("libMtkOmxCore.so", 1);\n'
            ),
            "omx/videocodec/include/val_types_public.h": (
                "enum chip { VAL_CHIP_NAME_MT8163 };\n"
            ),
            "omx/core/Android.bp": (
                'cc_library_shared { name: "libMtkOmxCore", '
                'include_dirs: ["vendor/amazon/mt8163/proprietary/include"] }\n'
            ),
        }

    def test_distinguishes_loader_from_decoder_implementation(self) -> None:
        report = self.run_audit(self.loader_files())
        self.assertEqual(
            report["source_boundary"], "open_loader_and_abi_headers_only"
        )
        self.assertTrue(report["mt8163_val_contract_present"])
        self.assertEqual(
            report["decoder_shared_object_references"], ["libMtkOmxVdec.so"]
        )
        self.assertTrue(report["component_factory_loaded_dynamically"])
        self.assertFalse(report["decoder_component_source_present"])
        self.assertEqual(report["decoder_build_targets"], [])
        self.assertEqual(report["component_factory_definition_files"], [])

    def test_reports_decoder_component_source_candidate(self) -> None:
        files = self.loader_files()
        files.update(
            {
                "omx/decoder/Android.bp": (
                    'cc_library_shared { name: "libMtkOmxVdec" }\n'
                ),
                "omx/decoder/decoder.cpp": (
                    'extern "C" OMX_COMPONENTTYPE *MtkOmxComponentCreate('
                    "OMX_STRING name) { return nullptr; }\n"
                ),
            }
        )
        report = self.run_audit(files)
        self.assertEqual(
            report["source_boundary"],
            "decoder_component_source_candidate_present",
        )
        self.assertTrue(report["decoder_component_source_present"])
        self.assertEqual(report["decoder_build_targets"], ["libMtkOmxVdec"])
        self.assertEqual(
            report["component_factory_definition_files"],
            ["omx/decoder/decoder.cpp"],
        )
