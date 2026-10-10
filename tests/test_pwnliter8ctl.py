"""Host-side protocol tests for the `Pwnliter8ctl` control tool.

These tests lock down the externally observable USB behavior of the host
tool — request types, request codes, wValue/wIndex, transfer chunking,
ordering, and timeouts — without requiring pyusb, hardware, or a device.

They intentionally do NOT import the real `usb` (pyusb) module: a recording
stub is injected into sys.modules before the tool is loaded, so the tests
run on a bare Python 3 installation:

    python3 -m unittest discover -s tests -v

Nothing in this file is part of the firmware build; it cannot affect
firmware output, timing, or exploit behavior.
"""

import argparse
import contextlib
import importlib.machinery
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import types
import unittest

TOOL_PATH = Path(__file__).resolve().parent.parent / "tools" / "Pwnliter8ctl"

# Must match the constants in Pwnliter8ctl
DFU_DNLOAD = 1
DFU_ABORT = 4
CUSTOM_DEMOTE = 7
CUSTOM_BOOT = 8
TRANSFER_SIZE = 0x800
CTRL_WRITE = 0x21

DFU_REQ = (lambda request, data, timeout: (CTRL_WRITE, request, 0, 0, data, timeout))


def load_tool():
    """Load the extensionless `Pwnliter8ctl` script as a module."""
    stub = types.ModuleType("usb")
    stub.core = types.SimpleNamespace(find=None)
    old = sys.modules.get("usb")
    sys.modules["usb"] = stub
    try:
        loader = importlib.machinery.SourceFileLoader("Pwnliter8ctl_under_test",
                                                      str(TOOL_PATH))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        mod = importlib.util.module_from_spec(spec)
        loader.exec_module(mod)
    finally:
        if old is None:
            del sys.modules["usb"]
        else:
            sys.modules["usb"] = old
    return mod


TOOL = load_tool()


class FakeDevice:
    """Records every ctrl_transfer the tool performs."""

    def __init__(self, serial="CPID:8020 ... PWND:[Pwnliter8]"):
        self.serial_number = serial
        self.calls = []

    def ctrl_transfer(self, req_type, request, value, index, data, timeout):
        if isinstance(data, (bytes, bytearray, memoryview)):
            data = bytes(data)
        self.calls.append((req_type, request, value, index, data, timeout))
        return len(data) if data is not None else 0


class OpenDeviceTests(unittest.TestCase):
    def stub_find(self, dev):
        self.dev = dev
        TOOL.usb.core.find = self._record

    def _record(self, **kwargs):
        self.find_kwargs = kwargs
        return self.dev

    def test_device_is_searched_by_apple_vid_and_dfu_product_id(self):
        self.stub_find(FakeDevice())
        self.assertIs(TOOL.open_device(), self.dev)
        self.assertEqual(self.find_kwargs, {"idVendor": 0x05AC, "idProduct": 0x1227})

    def test_no_device_raises(self):
        TOOL.usb.core.find = lambda **kwargs: None
        with self.assertRaisesRegex(RuntimeError, "no Apple DFU device found"):
            TOOL.open_device()

    def test_non_pwned_device_raises(self):
        TOOL.usb.core.find = lambda **kwargs: FakeDevice(serial="CPID:8020 CPID")
        with self.assertRaisesRegex(RuntimeError, "not a Pwned DFU device"):
            TOOL.open_device()


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.dev = FakeDevice()

    def run_download(self, buf):
        with contextlib.redirect_stdout(io.StringIO()):
            TOOL.download(self.dev, buf)

    def test_empty_payload_sends_only_final_zero_length_dnload(self):
        self.run_download(b"")
        self.assertEqual(self.dev.calls,
                         [DFU_REQ(DFU_DNLOAD, None, 100)])

    def test_payload_is_chunked_at_0x800(self):
        buf = bytes(range(256)) * (2 * TRANSFER_SIZE // 256)
        self.run_download(buf)

        expected = [DFU_REQ(DFU_DNLOAD, buf[i:i + TRANSFER_SIZE], 1000)
                    for i in range(0, len(buf), TRANSFER_SIZE)]
        expected.append(DFU_REQ(DFU_DNLOAD, None, 100))
        self.assertEqual(self.dev.calls, expected)

    def test_final_chunk_size_is_the_exact_remainder(self):
        buf = b"\xaa" * (TRANSFER_SIZE + 1)
        self.run_download(buf)

        self.assertEqual(len(self.dev.calls), 3)
        self.assertEqual(len(self.dev.calls[0][4]), TRANSFER_SIZE)
        self.assertEqual(self.dev.calls[1][4], b"\xaa")
        self.assertEqual(self.dev.calls[0][4], buf[:TRANSFER_SIZE])
        # final zero-length DNLOAD uses the short 100 ms timeout
        self.assertEqual(self.dev.calls[2], DFU_REQ(DFU_DNLOAD, None, 100))

    def test_every_chunk_uses_wValue_0_wIndex_0_and_dfu_class_request(self):
        self.run_download(b"\x01" * (TRANSFER_SIZE + 3))
        for req_type, request, value, index, _data, _timeout in self.dev.calls:
            self.assertEqual(req_type, CTRL_WRITE)
            self.assertEqual(request, DFU_DNLOAD)
            self.assertEqual(value, 0)
            self.assertEqual(index, 0)


class BootFlowTests(unittest.TestCase):
    def test_boot_sequence_and_ordering(self):
        dev = FakeDevice()
        TOOL.usb.core.find = lambda **kwargs: dev

        iboot = b"\x5a" * (TRANSFER_SIZE + 5)
        with tempfile.TemporaryDirectory() as tmp:
            iboot_path = Path(tmp) / "iboot.bin"
            iboot_path.write_bytes(iboot)
            args = argparse.Namespace(iboot=iboot_path)
            with contextlib.redirect_stdout(io.StringIO()):
                TOOL.do_boot(args)

        expected = [
            DFU_REQ(DFU_DNLOAD, iboot[:TRANSFER_SIZE], 1000),
            DFU_REQ(DFU_DNLOAD, iboot[TRANSFER_SIZE:], 1000),
            DFU_REQ(DFU_DNLOAD, None, 100),      # end-of-transfer marker
            DFU_REQ(CUSTOM_BOOT, None, 100),     # then boot...
            DFU_REQ(DFU_ABORT, None, 100),       # ...then abort
        ]
        self.assertEqual(dev.calls, expected)


class DemoteFlowTests(unittest.TestCase):
    def test_demote_sends_single_custom_request(self):
        dev = FakeDevice()
        TOOL.usb.core.find = lambda **kwargs: dev

        TOOL.do_demote(argparse.Namespace())
        self.assertEqual(dev.calls, [DFU_REQ(CUSTOM_DEMOTE, None, 100)])


if __name__ == "__main__":
    unittest.main()
