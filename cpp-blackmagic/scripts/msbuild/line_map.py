"""
Inject #line mapping directives for MSBuild-generated cppbm files.

Behavior:
1) Prefix generated body with:
   #line 1 "<original-source-path>"
2) If marker exists (default: "// Generated decorator bindings."),
   switch file/line mapping before marker to:
   #line 1 "<mapped-output-path>"
"""

import argparse
import codecs
from pathlib import Path
from typing import Tuple


def detect_text_codec_and_bom(raw: bytes) -> Tuple[str, bytes]:
    if raw.startswith(codecs.BOM_UTF8):
        return "utf-8", codecs.BOM_UTF8
    if raw.startswith(codecs.BOM_UTF32_LE):
        return "utf-32-le", codecs.BOM_UTF32_LE
    if raw.startswith(codecs.BOM_UTF32_BE):
        return "utf-32-be", codecs.BOM_UTF32_BE
    if raw.startswith(codecs.BOM_UTF16_LE):
        return "utf-16-le", codecs.BOM_UTF16_LE
    if raw.startswith(codecs.BOM_UTF16_BE):
        return "utf-16-be", codecs.BOM_UTF16_BE

    if len(raw) >= 4:
        even_zeros = raw[0::2].count(0)
        odd_zeros = raw[1::2].count(0)
        if odd_zeros > len(raw) // 8 and even_zeros < len(raw) // 32:
            return "utf-16-le", b""
        if even_zeros > len(raw) // 8 and odd_zeros < len(raw) // 32:
            return "utf-16-be", b""

    for codec in ("utf-8", "gb18030", "cp1252", "latin-1"):
        try:
            raw.decode(codec)
            return codec, b""
        except UnicodeDecodeError:
            pass

    return "utf-8", b""


def read_text_auto(path: Path) -> Tuple[str, str, bytes]:
    raw = path.read_bytes()
    codec, bom = detect_text_codec_and_bom(raw)
    payload = raw[len(bom):] if len(bom) > 0 else raw
    text = payload.decode(codec)
    return text, codec, bom


def write_text_auto(path: Path, text: str, codec: str, bom: bytes) -> None:
    encoded = bom + text.encode(codec)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        try:
            if path.read_bytes() == encoded:
                return
        except OSError:
            pass
    path.write_bytes(encoded)


def escape_path_for_line(path: Path) -> str:
    normalized = str(path.resolve()).replace("\\", "/")
    return normalized.replace('"', '\\"')


def inject_line_map(content: str, src_path: Path, out_path: Path, marker: str) -> str:
    src = escape_path_for_line(src_path)
    out = escape_path_for_line(out_path)
    prefix = f'#line 1 "{src}"\n'

    idx = content.find(marker) if marker else -1
    if idx < 0:
        return prefix + content

    head = content[:idx]
    tail = content[idx:]
    switch = f'#line 1 "{out}"\n'
    if len(head) > 0 and not head.endswith("\n"):
        switch = "\n" + switch
    return prefix + head + switch + tail


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", required=True, help="Original source file path")
    parser.add_argument("--in", dest="inp", required=True, help="Decorator-generated input path")
    parser.add_argument("--out", required=True, help="Mapped output path for compilation")
    parser.add_argument(
        "--marker",
        default="// Generated decorator bindings.",
        help="Marker before generated-only tail section",
    )
    args = parser.parse_args()

    src = Path(args.src)
    inp = Path(args.inp)
    out = Path(args.out)

    text, codec, bom = read_text_auto(inp)
    mapped = inject_line_map(text, src_path=src, out_path=out, marker=args.marker)
    write_text_auto(out, mapped, codec, bom)


if __name__ == "__main__":
    main()
