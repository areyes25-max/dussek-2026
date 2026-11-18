"""packaging/notices.py: the third-party notices every download carries, each library built into the
plugins with its license text; a crate the engine links without one stops the release."""
import importlib.util
import os
import shutil
import subprocess
import sys

import pytest

import project as P

SCRIPT = f"{P.REPO}/packaging/notices.py"


def load():
    spec = importlib.util.spec_from_file_location("notices", SCRIPT)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


needs_cargo = pytest.mark.skipif(shutil.which("cargo") is None, reason="no cargo: the crates come from cargo metadata")
needs_dpf = pytest.mark.skipif(not os.path.exists(f"{P.REPO}/plugin/dpf/LICENSE"), reason="no DPF submodule (just dpf)")


@needs_cargo
@needs_dpf
def test_every_linked_crate_has_its_license_text(tmp_path):
    out = tmp_path / "notices.txt"
    subprocess.run([sys.executable, SCRIPT, "linux", str(out)], check=True)
    text = out.read_text()
    crates = load().engine_crates("linux")
    assert len(crates) > 20
    for p in crates:
        head = f'{p["name"]} {p["version"]} ({p.get("license")})'
        assert head in text, head
        # the section: its head, a rule, the text up to the next rule
        body = text.split(head + "\n" + "-" * 100 + "\n", 1)[1].split("-" * 100 + "\n", 1)[0]
        assert len(body.strip()) > 40, head  # a license, not an empty section
    # the engine's own crate is not a third party
    assert not any(p["source"] is None for p in crates)


@needs_cargo
@needs_dpf
def test_dpf_and_what_it_builds_in_are_named():
    text = load().notices("linux")
    for title in ("DPF, the DISTRHO Plugin Framework", "pugl", "NanoVG", "DejaVu Sans", "RtAudio", "RtMidi",
                  "The LV2 headers", "The CLAP headers", "libSOFD", "The Rust standard library"):
        assert f"\n{title}" in text, title
    assert "Filipe Coelho" in text and "Gary P. Scavone" in text and "Bitstream" in text
    # macOS has no X11 file dialog
    assert "libSOFD" not in load().notices("macos")


def test_a_crate_without_a_license_file_stops_the_release(tmp_path):
    mod = load()
    (tmp_path / "Cargo.toml").write_text("")
    crate = {"name": "nameless", "version": "1.0.0", "license": "MIT", "manifest_path": str(tmp_path / "Cargo.toml")}
    with pytest.raises(SystemExit, match="nameless 1.0.0"):
        mod.crate_license(crate)
    # a public-domain dedication needs no notice
    crate["license"] = "Unlicense"
    assert "public domain" in mod.crate_license(crate)


def test_of_mit_or_apache_the_mit_file_is_kept(tmp_path):
    mod = load()
    (tmp_path / "Cargo.toml").write_text("")
    (tmp_path / "LICENSE-MIT").write_text("MIT text " * 10)
    (tmp_path / "LICENSE-APACHE").write_text("Apache text " * 10)
    crate = {"name": "dual", "version": "1.0.0", "license": "MIT OR Apache-2.0", "manifest_path": str(tmp_path / "Cargo.toml")}
    text = mod.crate_license(crate)
    assert "MIT text" in text and "Apache text" not in text
    # both where both apply
    crate["license"] = "(MIT OR Apache-2.0) AND Unicode-3.0"
    text = mod.crate_license(crate)
    assert "MIT text" in text and "Apache text" in text
