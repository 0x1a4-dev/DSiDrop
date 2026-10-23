"""Check the pinned TCP patch and, optionally, its linked ARM implementation."""
import hashlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("prepare_dswifi", ROOT / "tools/prepare_dswifi.py")
DSWIFI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DSWIFI)
ARCHIVE = ROOT / "build/dswifi" / f"{DSWIFI.COMMIT}.tar.gz"
CHECK_ARM = "--check-arm" in sys.argv


class DswifiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.source = DSWIFI.extract_sources(ARCHIVE.read_bytes(), Path(cls.temporary.name))
        cls.config = (cls.source / "source/sgIP/sgIP_Config.h").read_text(encoding="utf-8")
        cls.tcp = (cls.source / "source/sgIP/sgIP_TCP.c").read_text(encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_pinned_sources(self):
        for relative, expected in DSWIFI.SOURCE_BLOBS.items():
            data = (self.source / relative).read_bytes()
            self.assertEqual(hashlib.sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest(), expected)

    def test_only_receive_capacity_changes(self):
        updated = DSWIFI.patch_config(self.config)
        self.assertIn("32768", updated)
        self.assertEqual(updated.replace("32768", "8192"), self.config)

    def test_data_syn_and_initial_windows(self):
        updated = DSWIFI.patch_tcp(self.tcp)
        self.assertNotIn("1400", updated)
        self.assertIn("rec->rxwindow=rec->ack+SGIP_TCP_RECEIVEBUFFERLENGTH-1;", updated)
        self.assertIn("windowlen = SGIP_TCP_RECEIVEBUFFERLENGTH-windowlen-1;", updated)
        self.assertIn("if(windowlen<0) windowlen=0;", updated)
        self.assertIn("if(windowlen>65535) windowlen=65535;", updated)
        self.assertIn("windowlen<SGIP_TCP_REACK_THRESH", updated)
        self.assertEqual(updated[updated.index("void sgIP_TCP_FixChecksum"):updated.index("int sgIP_TCP_SendSynReply")],
                         self.tcp[self.tcp.index("void sgIP_TCP_FixChecksum"):self.tcp.index("int sgIP_TCP_SendSynReply")])
        self.assertEqual(updated[updated.index("int sgIP_TCP_Recv"):], self.tcp[self.tcp.index("int sgIP_TCP_Recv"):])

    def test_unexpected_or_already_patched_sources_fail(self):
        with self.assertRaises(ValueError):
            DSWIFI.patch_config(DSWIFI.patch_config(self.config))
        with self.assertRaises(ValueError):
            DSWIFI.patch_tcp(DSWIFI.patch_tcp(self.tcp))
        with self.assertRaises(ValueError):
            DSWIFI.patch_tcp(self.tcp.replace("rec->rxwindow=rec->ack+1400;", "changed"))

    def test_repeated_preparation_uses_cached_original(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)
            (destination / ARCHIVE.name).write_bytes(ARCHIVE.read_bytes())
            with patch.object(DSWIFI.urllib.request, "urlopen", side_effect=AssertionError("Unexpected download")):
                source = DSWIFI.prepare(destination)
                first = (source / "source/sgIP/sgIP_TCP.c").read_bytes()
                DSWIFI.prepare(destination)
                self.assertEqual((source / "source/sgIP/sgIP_TCP.c").read_bytes(), first)

    def test_archive_cannot_escape_build_directory(self):
        for invalid in ("../outside", "..\\outside", "C:/outside"):
            with self.subTest(path=invalid):
                data = io.BytesIO()
                with tarfile.open(fileobj=data, mode="w:gz") as archive:
                    archive.addfile(tarfile.TarInfo(f"dswifi-{DSWIFI.COMMIT}/{invalid}"))
                with tempfile.TemporaryDirectory() as directory, self.assertRaises(ValueError):
                    DSWIFI.extract_sources(data.getvalue(), Path(directory))

    @unittest.skipUnless(CHECK_ARM, "Pass --check-arm after building the DSi client")
    def test_linked_arm_uses_large_fifo_without_window_cap(self):
        import os
        sdk = Path(os.environ.get("DEVKITPRO", "C:/devkitPro"))
        objdump = sdk / "devkitARM/bin/arm-none-eabi-objdump"
        if not objdump.exists():
            objdump = objdump.with_suffix(".exe")
        for symbol in ("sgIP_TCP_GenHeader", "sgIP_TCP_Recv", "sgIP_TCP_SendSynReply"):
            output = subprocess.check_output([
                str(objdump), "-d", f"--disassemble={symbol}", str(ROOT / "DSiDrop.elf"),
            ], text=True)
            self.assertIn(f"<{symbol}>:", output)
            self.assertNotIn("#1400", output)
            self.assertNotIn("0x00000578", output)
            self.assertNotIn("0x00007805", output)
            self.assertTrue("#32768" in output or "0x00007fff" in output, symbol)
        mapping = (ROOT / "build/obj/DSiDrop.map").read_text(encoding="utf-8")
        self.assertIn("build/dswifi/cmake/libdswifi9.a(sgIP_TCP.o)", mapping)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)