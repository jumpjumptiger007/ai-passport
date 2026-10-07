#!/usr/bin/env python3
"""Compile and run the firmware URL regression using the tracked Kconfig default."""

from __future__ import annotations

import re
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    config = (repo / "main/Kconfig.projbuild").read_text()
    match = re.search(r'config SONIC_DEMO_URL\s+string[^\n]*\s+default\s+"([^"]*)"', config)
    if match is None:
        raise SystemExit("SONIC_DEMO_URL Kconfig default is missing")
    url = match.group(1)
    encoded = url.encode("utf-8")
    if not encoded or len(encoded) > 93:
        raise SystemExit(f"SONIC_DEMO_URL must contain 1–93 UTF-8 bytes; got {len(encoded)}")
    # Separate hex escapes prevent a following hexadecimal URL character from
    # being absorbed into a preceding C escape.
    literal = '"' + "".join(f"\\x{byte:02x}" for byte in encoded) + '"'
    with tempfile.TemporaryDirectory(prefix="sonic-demo-url-") as temporary:
        binary = Path(temporary) / "test_sonic_demo_url"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            f"-DCONFIG_SONIC_DEMO_URL={literal}",
            f"-I{repo / 'components/sonic_core/include'}",
            str(repo / "tests/test_sonic_demo_url.c"),
            str(repo / "components/sonic_core/sonic_core.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    frames = (len(encoded) + 30) // 31
    print(f"SONIC_DEMO_URL: semantic URL validation and {frames}-frame fragmentation PASS ({len(encoded)} UTF-8 bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
