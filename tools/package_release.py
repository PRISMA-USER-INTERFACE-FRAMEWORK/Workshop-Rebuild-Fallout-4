import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = re.compile(r'^local PRISMACRAFTING_VERSION = "([0-9][0-9.]*)"$', re.M)
DLL_ENTRY = "Data/F4SE/Plugins/PrismaCrafting.dll"
WORKBENCH_ENTRY = "Data/PrismaUI_F4/JsonData/PrismaCraftingWorkbenches.example.json"
WORKBENCH_SOURCE = ROOT / "docs" / "PrismaCraftingWorkbenches.example.json"
REQUIRED = (DLL_ENTRY, WORKBENCH_ENTRY, "fomod/ModuleConfig.xml", "fomod/info.xml")
SOURCE_SUFFIXES = (".cpp", ".h")
STAMP = (1980, 1, 1, 0, 0, 0)


def fail(message: str) -> None:
    raise SystemExit(message)


def xmake_version() -> str:
    found = VERSION_PATTERN.search((ROOT / "xmake.lua").read_text(encoding="utf-8"))
    if not found:
        fail("could not read PRISMACRAFTING_VERSION from xmake.lua")
    return found.group(1)


def tree(src: Path, prefix: str) -> dict[str, Path]:
    found = {f"{prefix}/{p.relative_to(src).as_posix()}": p for p in sorted(src.rglob("*")) if p.is_file()}
    if not found:
        fail(f"no files under {src}")
    return found


def layout(dll: Path) -> dict[str, Path]:
    entries = {DLL_ENTRY: dll, WORKBENCH_ENTRY: WORKBENCH_SOURCE}
    entries.update(tree(ROOT / "assets" / "views", "Data/PrismaUI_F4/views"))
    entries.update(tree(ROOT / "assets" / "translations", "Data/Interface/Translations"))
    entries.update(tree(ROOT / "fomod", "fomod"))
    return entries


def write_zip(zpath: Path, entries: dict[str, Path]) -> None:
    with zipfile.ZipFile(zpath, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name in sorted(entries):
            info = zipfile.ZipInfo(name, date_time=STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, entries[name].read_bytes())


def verify(zpath: Path, entries: dict[str, Path]) -> None:
    with zipfile.ZipFile(zpath) as zf:
        if zf.testzip() is not None:
            fail(f"corrupt entry in {zpath.name}")
        names = set(zf.namelist())
    missing = sorted((set(REQUIRED) | set(entries)) - names)
    if missing:
        fail(f"missing from {zpath.name}: {', '.join(missing)}")
    source = sorted(n for n in names if n.lower().endswith(SOURCE_SUFFIXES))
    if source:
        fail(f"source files in {zpath.name}: {', '.join(source)}")


def main(argv: list[str]) -> None:
    if len(argv) not in (3, 4):
        fail("usage: package_release.py <dll> <out-dir> [expected-version]")
    dll, out = Path(argv[1]), Path(argv[2])
    if not dll.is_file():
        fail(f"no DLL at {dll}")
    ver = xmake_version()
    if len(argv) == 4 and argv[3] != ver:
        fail(f"expected version {argv[3]} does not match xmake.lua {ver}")
    entries = layout(dll)
    out.mkdir(parents=True, exist_ok=True)
    zpath = out / f"PrismaCrafting-{ver}.zip"
    write_zip(zpath, entries)
    verify(zpath, entries)
    print(f"packaged {zpath} ({len(entries)} files)")


if __name__ == "__main__":
    main(sys.argv)
