#!/usr/bin/env python3
import argparse
import hashlib
import subprocess
import sys
import urllib.request
from pathlib import Path


FONT_URL = (
    "https://github.com/notofonts/noto-cjk/raw/main/"
    "Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf"
)
FONT_SHA256 = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def download_font(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and sha256(path) == FONT_SHA256:
        return
    tmp = path.with_suffix(path.suffix + ".tmp")
    print(f"[gui-font] downloading {FONT_URL}", flush=True)
    urllib.request.urlretrieve(FONT_URL, tmp)
    digest = sha256(tmp)
    if digest != FONT_SHA256:
        tmp.unlink(missing_ok=True)
        raise RuntimeError(f"font sha256 mismatch: expected {FONT_SHA256}, got {digest}")
    tmp.replace(path)


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
    print("[gui-font] subsetting Noto Sans CJK SC", flush=True)
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
    source = Path(args.source) if args.source else project_root / "third_party" / "fonts" / "NotoSansCJKsc-Regular.otf"
    chars_output = Path(args.chars_output) if args.chars_output else output.with_suffix(".chars.txt")

    download_font(source)
    chars_output.parent.mkdir(parents=True, exist_ok=True)
    chars_output.write_text(collect_locale_chars(project_root / "locales"), encoding="utf-8")
    ensure_fonttools(args.python)
    subset_font(args.python, source, chars_output, output)
    print(f"[gui-font] wrote {output} ({output.stat().st_size} bytes)", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
