#!/usr/bin/env python3
import argparse
import hashlib
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path


FONT_ARCHIVE_URL = (
    "https://github.com/TakWolf/fusion-pixel-font/releases/download/"
    "2026.05.07/fusion-pixel-font-12px-monospaced-ttf-v2026.05.07.zip"
)
FONT_ARCHIVE_SHA256 = "bb8ad772030d7671abee86aa53c281701efd8fd38259278c89549371cca1070c"
FONT_ARCHIVE_MEMBER = "fusion-pixel-12px-monospaced-zh_hans.ttf"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def extract_font_from_archive(archive: Path, member: str, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as zf:
        with zf.open(member) as src, output.open("wb") as dst:
            for chunk in iter(lambda: src.read(1024 * 1024), b""):
                dst.write(chunk)


def download_font_archive(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and sha256(path) == FONT_ARCHIVE_SHA256:
        return
    tmp = path.with_suffix(path.suffix + ".tmp")
    print(f"[gui-font] downloading {FONT_ARCHIVE_URL}", flush=True)
    urllib.request.urlretrieve(FONT_ARCHIVE_URL, tmp)
    digest = sha256(tmp)
    if digest != FONT_ARCHIVE_SHA256:
        tmp.unlink(missing_ok=True)
        raise RuntimeError(f"font archive sha256 mismatch: expected {FONT_ARCHIVE_SHA256}, got {digest}")
    tmp.replace(path)


def ensure_default_font(project_root: Path, source: Path) -> None:
    if source.exists():
        return
    archive = project_root / "third_party" / "fonts" / "fusion-pixel-font-12px-monospaced-ttf-v2026.05.07.zip"
    download_font_archive(archive)
    extract_font_from_archive(archive, FONT_ARCHIVE_MEMBER, source)


def strip_raw_line(line: str) -> bool:
    stripped = line.strip()
    return (
        not stripped
        or stripped.startswith("#")
        or stripped == 'R"(placeholder=a'
        or stripped == 'placeholder2=a)"'
    )


def collect_locale_chars(locale_dir: Path) -> str:
    chars = set()
    for path in sorted(locale_dir.glob("*.lc")):
        text = path.read_text(encoding="utf-8")
        for line in text.splitlines():
            if strip_raw_line(line):
                continue
            value = line.split("=", 1)[1] if "=" in line else line
            chars.update(value)

    chars.update(
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        " .,;:!?()[]{}<>+-*/=%#&_\"'`~\\|/@"
        "\u00a0\u2026\u2013\u2014\u2018\u2019\u201c\u201d"
        "\u3000\u3001\u3002\uff0c\u300a\u300b\uff1a\uff1b\uff1f\uff01"
        "\uff08\uff09\u3010\u3011\uff0b\uff0d\uff1d\uff05"
    )
    return "".join(sorted(chars))


def ensure_fonttools(python: str) -> None:
    try:
        subprocess.check_call(
            [python, "-c", "import fontTools.subset"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return
    except subprocess.CalledProcessError:
        pass

    print("[gui-font] installing fonttools for font subsetting", flush=True)
    subprocess.check_call([python, "-m", "pip", "install", "--user", "fonttools"])


def subset_font(python: str, source: Path, text_file: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        python,
        "-m",
        "fontTools.subset",
        str(source),
        f"--text-file={text_file}",
        f"--output-file={output}",
        "--layout-features=*",
        "--glyph-names",
        "--symbol-cmap",
        "--legacy-cmap",
        "--notdef-glyph",
        "--notdef-outline",
        "--recommended-glyphs",
        "--name-IDs=*",
        "--name-legacy",
        "--name-languages=*",
    ]
    print("[gui-font] subsetting Fusion Pixel 12px Monospaced SC", flush=True)
    subprocess.check_call(cmd)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the bundled ImGui CJK fallback font subset.")
    parser.add_argument("--project-root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--source", default="")
    parser.add_argument("--chars-output", default="")
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()

    project_root = Path(args.project_root)
    output = Path(args.output)
    source = Path(args.source) if args.source else project_root / "third_party" / "fonts" / FONT_ARCHIVE_MEMBER
    chars_output = Path(args.chars_output) if args.chars_output else output.with_suffix(".chars.txt")

    if not args.source:
        ensure_default_font(project_root, source)
    chars_output.parent.mkdir(parents=True, exist_ok=True)
    chars_output.write_text(collect_locale_chars(project_root / "locales"), encoding="utf-8")
    ensure_fonttools(args.python)
    subset_font(args.python, source, chars_output, output)
    print(f"[gui-font] wrote {output} ({output.stat().st_size} bytes)", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
