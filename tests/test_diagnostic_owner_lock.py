"""A policy supervisor passes its actual lock, never an arbitrary descriptor."""

from pathlib import Path
import fcntl
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DiagnosticOwnerLockTests(unittest.TestCase):
    def test_inherited_owner_and_rejected_unrelated_descriptors(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            lock = directory / "owner.lock"
            lock.touch(mode=0o600)
            executable = directory / "check"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    f'-DPLB_OWNER_LOCK_FILE="{lock}"',
                    str(ROOT / "tests/fixtures/diagnostic_owner_lock.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            environment = dict(os.environ)
            environment.pop("PLB_DIAGNOSTIC_OWNER_FD", None)
            with lock.open("r+") as owner, (directory / "unrelated").open("w+") as other:
                fcntl.flock(owner, fcntl.LOCK_EX | fcntl.LOCK_NB)
                for text, descriptor, expected in (
                    (str(owner.fileno()), owner.fileno(), "success"),
                    (str(other.fileno()), other.fileno(), "failure"),
                    ("-1", None, "failure"),
                    ("not-an-fd", None, "failure"),
                    ("1025", None, "failure"),
                    ("999", None, "failure"),
                ):
                    with self.subTest(value=text):
                        subprocess.run(
                            [str(executable), expected],
                            env={**environment, "PLB_DIAGNOSTIC_OWNER_FD": text},
                            pass_fds=() if descriptor is None else (descriptor,),
                            check=True,
                        )
                subprocess.run([str(executable), "failure"], env=environment, check=True)
                with lock.open("r+") as competitor:
                    with self.assertRaises(BlockingIOError):
                        fcntl.flock(competitor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            subprocess.run([str(executable), "success"], env=environment, check=True)
