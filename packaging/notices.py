#!/usr/bin/env python3
"""The third-party notices for a release: every library built into the plugins and standalones, with its
license text (the licenses ask for their notice beside every copy of a binary).

    python3 packaging/notices.py <linux|macos|windows|all> <out.txt>

From the sources themselves: DPF's parts from the submodule (plugin/dpf), the engine's Rust crates from
`cargo metadata` (the normal dependencies for the platform's targets) and each crate's own license files
in cargo's registry, the CLAP headers' and Rust's standard library's from packaging/notices/ (no file of
their own in the tree), Windows' winpthreads from MSYS2. A crate without a license file is an error (unless
its license is a public-domain dedication, NO_NOTICE): a new dependency cannot ship without its notice. Run from the repository's root.
"""

import glob
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DPF = ROOT / "plugin" / "dpf"
HERE = ROOT / "packaging" / "notices"

# the Rust targets each platform's binaries are built for (macOS: universal)
TARGETS = {
    "linux": ["x86_64-unknown-linux-gnu"],
    "macos": ["aarch64-apple-darwin", "x86_64-apple-darwin"],
    "windows": ["x86_64-pc-windows-gnu"],
}
# dedications to the public domain: no notice to carry, a crate under one may come without a file
NO_NOTICE = {"Unlicense", "CC0-1.0", "0BSD"}
LICENSE_FILE = re.compile(r"^(licen[cs]e|copying|copyright|notice|unlicense)", re.I)


def leading_comment(path: Path) -> str:
    """The first comment block of a C file: its license header."""
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.match(r"\s*/\*(.*?)\*/", text, re.S)
    if m:
        lines = [re.sub(r"^\s*\* ?", "", l) for l in m.group(1).splitlines()]
    else:
        lines = []
        for l in text.splitlines():
            if not l.startswith("//"):
                break
            lines.append(l[2:].removeprefix(" "))
    return "\n".join(lines).strip("\n")


def stb_license(path: Path) -> str:
    """stb's two licenses, at the end of its header."""
    text = path.read_text(encoding="utf-8", errors="replace")
    i = text.rfind("This software is available under 2 licenses")
    if i < 0:
        raise SystemExit(f"notices: no license at the end of {path}")
    return text[i:].strip().removesuffix("*/").strip()


def dpf_parts(platform: str) -> list[tuple[str, str]]:
    """(title, text) of DPF and what it builds in."""
    parts = [
        ("DPF, the DISTRHO Plugin Framework (every format)", (DPF / "LICENSE").read_text()),
        ("pugl, windows and events (in DPF: dgl/src/pugl-upstream)",
         (DPF / "dgl/src/pugl-upstream/COPYING").read_text()),
        ("NanoVG, drawing (in DPF: dgl/src/nanovg)", (DPF / "dgl/src/nanovg/LICENSE.txt").read_text()),
        ("fontstash, text (in DPF: dgl/src/nanovg)", leading_comment(DPF / "dgl/src/nanovg/fontstash.h")),
        ("stb_truetype and stb_image (in DPF: dgl/src/nanovg)", stb_license(DPF / "dgl/src/nanovg/stb_truetype.h")),
        ("DejaVu Sans, the interface's font (in DPF: dgl/src/resources)",
         (DPF / "dgl/src/resources/LICENSE-DejaVuSans.ttf.txt").read_text()),
        ("RtAudio (the standalones)", (DPF / "distrho/src/jackbridge/rtaudio/LICENSE").read_text()),
        ("RtMidi (the standalones)", (DPF / "distrho/src/jackbridge/rtmidi/LICENSE").read_text()),
        ("The LV2 headers (the LV2 plugins)", leading_comment(DPF / "distrho/src/lv2/lv2.h")),
        ("The CLAP headers (the CLAP plugins)", (HERE / "clap.txt").read_text()),
    ]
    if platform in ("linux", "all"):
        parts.append(("libSOFD, the file dialog on Linux (in DPF: distrho/extra/sofd)",
                      leading_comment(DPF / "distrho/extra/sofd/libsofd.c")))
    return parts


def cargo_metadata(target: str) -> dict:
    out = subprocess.run(
        ["cargo", "metadata", "--format-version", "1", "--locked", "--filter-platform", target,
         "--manifest-path", str(ROOT / "engine" / "Cargo.toml")],
        check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def engine_crates(platform: str) -> list[dict]:
    """The crates the engine links on the platform: its normal dependencies, followed from the root."""
    targets = sorted({t for ts in TARGETS.values() for t in ts}) if platform == "all" else TARGETS[platform]
    found: dict[str, dict] = {}
    for target in targets:
        m = cargo_metadata(target)
        packages = {p["id"]: p for p in m["packages"]}
        nodes = {n["id"]: n for n in m["resolve"]["nodes"]}
        seen, todo = set(), [m["resolve"]["root"]]
        while todo:
            pid = todo.pop()
            if pid in seen:
                continue
            seen.add(pid)
            for d in nodes[pid]["deps"]:
                if any(k["kind"] is None for k in d["dep_kinds"]):
                    todo.append(d["pkg"])
        for pid in seen:
            p = packages[pid]
            if p["source"] is not None:  # not the engine itself
                found[f'{p["name"]} {p["version"]}'] = p
    return [found[k] for k in sorted(found)]


def crate_license(p: dict) -> str:
    """The crate's license files; of a choice that includes MIT ("MIT OR Apache-2.0"), its MIT file."""
    d = Path(p["manifest_path"]).parent
    files = sorted(f for f in d.iterdir() if f.is_file() and LICENSE_FILE.match(f.name))
    expr = p.get("license") or ""
    if " AND " not in expr and "(" not in expr and re.search(r"\bMIT\b", expr) and " OR " in expr:
        mit = [f for f in files if "mit" in f.name.lower()]
        if mit:
            files = mit + [f for f in files if f.name.lower().startswith("notice")]
    if not files and expr in NO_NOTICE:
        return f"{expr}: dedicated to the public domain, no notice required (the crate ships no license file)."
    if not files:
        raise SystemExit(f'notices: {p["name"]} {p["version"]} ({expr}) has no license file in {d}')
    return "\n\n".join(f.read_text(encoding="utf-8", errors="replace").strip() for f in files)


def winpthreads() -> tuple[str, str]:
    prefix = os.environ.get("MINGW_PREFIX", "/ucrt64")
    hits = sorted(glob.glob(f"{prefix}/share/licenses/*winpthread*/*"))
    if not hits:
        raise SystemExit(f"notices: no winpthreads license under {prefix}/share/licenses")
    return ("winpthreads, MinGW-w64's threads (linked into the Windows builds)",
            "\n\n".join(Path(h).read_text(errors="replace").strip() for h in hits))


def notices(platform: str) -> str:
    rule = "=" * 100
    out = [
        "Dussek and Weidinger: third-party notices",
        "",
        "Dussek and Weidinger are licensed under the GNU Affero General Public License 3.0 (LICENSE.txt).",
        "Each instrument carries its own LICENSE.md (its data's licenses and sources). The plugins and",
        "standalones include the following software, under the licenses below.",
        "",
    ]
    parts = dpf_parts(platform)
    if platform == "windows":
        parts.append(winpthreads())
    elif platform == "all":  # the site's copy, made on Linux: MSYS2's file is on Windows only
        parts.append(("winpthreads, MinGW-w64's threads (linked into the Windows builds)",
                      "MIT and ZPL-2.1 (MinGW-w64): the full text is in the Windows downloads' copy of this file."))
    parts.append(("The Rust standard library (the engine)", (HERE / "rust.txt").read_text()))
    for title, text in parts:
        out += [rule, title, rule, "", text.strip(), ""]
    crates = engine_crates(platform)
    out += [rule, f"The engine's Rust crates ({len(crates)})", rule, ""]
    out += [f'- {p["name"]} {p["version"]}: {p.get("license")}' for p in crates]
    out.append("")
    for p in crates:
        out += ["-" * 100, f'{p["name"]} {p["version"]} ({p.get("license")})', "-" * 100, "",
                crate_license(p), ""]
    return "\n".join(out).rstrip() + "\n"


def main() -> None:
    if len(sys.argv) != 3 or sys.argv[1] not in (*TARGETS, "all"):
        raise SystemExit("usage: notices.py <linux|macos|windows|all> <out.txt>")
    Path(sys.argv[2]).write_text(notices(sys.argv[1]), encoding="utf-8")


if __name__ == "__main__":
    main()
