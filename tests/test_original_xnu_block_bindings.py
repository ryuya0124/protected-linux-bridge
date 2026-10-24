"""Refuse incompatible dispatch tables and unqualified original-code imports."""

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build_original_xnu_block_probe import MANGLED, qualify_dispatch, select_bindings
from audit_native_xnu_storage import STORAGE_REVISION


class OriginalBlockBindingsTest(unittest.TestCase):
    def setUp(self):
        self.audit = {
            "public_source_revision": STORAGE_REVISION,
            "method_slots": 190,
            "kernel_bindings": 176,
            "pure_virtual_slots": 14,
            "compiled_public_class_size": 144,
            "original_executable_code_unchanged": True,
            "abstract_complete_destructors_trap": True,
            "original_protected_constructor": "0xffff00001000",
            "original_base_destructor": "0xffff00002000",
            "original_metaclass": "0xffff00003000",
            "vtable": "0xffff00004000",
            "methods": [],
        }
        self.compiled = []
        self.readable = {}
        for index in range(190):
            pure = 160 <= index < 174
            name = "___cxa_pure_virtual" if pure else f"__ZN9IOService7method{index}Ev"
            actual = f"__ZN{MANGLED}7method{index}Ev" if pure else name
            self.audit["methods"].append(
                {
                    "slot": index,
                    "reference_name": name,
                    "candidate_target": hex(0xFFFF00005000 + index * 4),
                    "kernel_names": [name],
                }
            )
            self.compiled.append(actual)
            self.readable[actual] = f"Derived::method{index}()"
            self.readable[name] = f"Base::method{index}()"

    def test_complete_concrete_table(self):
        self.assertEqual(qualify_dispatch(self.audit, self.compiled, self.readable), 14)

    def test_incompatible_base_is_refused(self):
        for key, value in (
            ("compiled_public_class_size", 152),
            ("method_slots", 191),
            ("kernel_bindings", 175),
            ("pure_virtual_slots", 13),
            ("public_source_revision", "0" * 40),
            ("original_executable_code_unchanged", False),
            ("abstract_complete_destructors_trap", False),
        ):
            audit = dict(self.audit, **{key: value})
            with self.subTest(key=key), self.assertRaises(ValueError):
                qualify_dispatch(audit, self.compiled, self.readable)

    def test_pure_missing_extra_and_reordered_slots_are_refused(self):
        for change in (None, "___cxa_pure_virtual", "__ZN9IOService7otherEv"):
            compiled = list(self.compiled)
            compiled[160] = change
            with self.assertRaises(ValueError):
                qualify_dispatch(self.audit, compiled, self.readable)
        with self.assertRaises(ValueError):
            qualify_dispatch(self.audit, self.compiled + [self.compiled[0]], self.readable)
        audit = copy.deepcopy(self.audit)
        audit["methods"][100]["slot"] = 101
        with self.assertRaises(ValueError):
            qualify_dispatch(audit, self.compiled, self.readable)

    def test_override_requires_same_method_signature(self):
        compiled = list(self.compiled)
        compiled[42] = f"__ZN{MANGLED}7wrongEv"
        readable = dict(self.readable, **{compiled[42]: "Derived::wrong()"})
        with self.assertRaises(ValueError):
            qualify_dispatch(self.audit, compiled, readable)

    def test_actual_base_constructor_destructor_and_data_bindings(self):
        names = [
            "__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass",
            "__ZN20IOBlockStorageDeviceD2Ev",
            "__ZN20IOBlockStorageDevice10gMetaClassE",
            "__ZTV20IOBlockStorageDevice",
        ]
        bindings = select_bindings(self.audit, names + ["_ordinary_kernel_import"])
        self.assertEqual(
            list(bindings.values()),
            [0xFFFF00001000, 0xFFFF00002000, 0xFFFF00003000, 0xFFFF00004000],
        )

    def test_abstract_destructors_and_guessed_methods_are_refused(self):
        for name in (
            "__ZN20IOBlockStorageDeviceD0Ev",
            "__ZN20IOBlockStorageDeviceD1Ev",
            "__ZN20IOBlockStorageDevice7guessedEv",
        ):
            with self.assertRaises(ValueError):
                select_bindings(self.audit, [name])

    def test_empty_original_binding_set_is_refused(self):
        with self.assertRaises(ValueError):
            select_bindings(self.audit, ["_ordinary_kernel_import"])


if __name__ == "__main__":
    unittest.main()
