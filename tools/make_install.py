#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Add the web installer and the firmware to the site (beside the emulator):

  OUT/install/index.html          web/amb_installer.html, with fm1pkg.js, fm1ota.js and the package's metadata inlined
  OUT/firmware/dubengine-VER.fwsc the package it writes; also the download

  tools/make_install.py build/dubengine-0.1.0.fwsc 0.1.0 build/emu

The package must be a release build (./build.sh --release X.Y.Z): identity FM-1_5XXYYZZ."""
import json
import re
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
from make_site import product_of, strip_module  # noqa: E402


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_5\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y.Z)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"dubengine-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    for d in ("install", "firmware"):
        (out / d).mkdir(parents=True, exist_ok=True)
    shutil.copy(pkg, out / "firmware" / name)
    page = (SRC / "web" / "amb_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"amb_installer.html must contain {mark} once")
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    print(f"install/ ({product}), firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
