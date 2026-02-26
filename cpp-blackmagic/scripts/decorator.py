"""
Decorator preprocess pass (decorator.py).

Responsibilities:
1) Remove `decorator(...)` macro markers while preserving source layout.
2) Append generated Bind registrations:
   inline auto __cppbm_dec_xxx = (expr).Bind<&Target>();
3) Run optional module handlers around generation (`handle`).
"""

import argparse
import codecs
import importlib.util
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser


def detect_text_codec_and_bom(raw: bytes) -> Tuple[str, bytes]:
    # BOM-based fast path.
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

    # Heuristic for UTF-16 without BOM.
    if len(raw) >= 4:
        even_zeros = raw[0::2].count(0)
        odd_zeros = raw[1::2].count(0)
        if odd_zeros > len(raw) // 8 and even_zeros < len(raw) // 32:
            return "utf-16-le", b""
        if even_zeros > len(raw) // 8 and odd_zeros < len(raw) // 32:
            return "utf-16-be", b""

    # Lightweight fallback codecs.
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
    try:
        text = payload.decode(codec)
    except UnicodeDecodeError as e:
        raise RuntimeError(f"Failed to decode source file '{path}' with codec '{codec}'.") from e
    return text, codec, bom


def write_text_auto(path: Path, text: str, codec: str, bom: bytes) -> None:
    try:
        encoded = text.encode(codec)
    except UnicodeEncodeError as e:
        raise RuntimeError(f"Failed to encode output file '{path}' with codec '{codec}'.") from e
    payload = bom + encoded
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        try:
            if path.read_bytes() == payload:
                return
        except OSError:
            pass
    path.write_bytes(payload)


def build_cpp_language():
    candidates = []
    if hasattr(tscpp, "language"):
        try:
            candidates.append(tscpp.language())
        except Exception:
            pass
    if hasattr(tscpp, "LANGUAGE"):
        candidates.append(tscpp.LANGUAGE)

    for candidate in candidates:
        # New API style.
        try:
            return Language(candidate)
        except Exception:
            pass
        # Old API style.
        try:
            return Language(candidate, "cpp")
        except Exception:
            pass
        if isinstance(candidate, (str, bytes, Path)):
            try:
                return Language(str(candidate), "cpp")
            except Exception:
                pass

    # Optional fallback package.
    try:
        from tree_sitter_languages import get_language
        return get_language("cpp")
    except Exception:
        return None


def build_cpp_parser(language):
    if language is None:
        return None

    try:
        parser = Parser(language)
    except Exception:
        parser = Parser()

    try:
        if hasattr(parser, "set_language"):
            parser.set_language(language)
        elif hasattr(parser, "language"):
            parser.language = language
    except Exception:
        return None

    try:
        parser.parse(b"int __cppbm_probe__() { return 0; }")
    except Exception:
        return None

    return parser


CPP_LANGUAGE = build_cpp_language()
cpp_parser = build_cpp_parser(CPP_LANGUAGE)


@dataclass
class DecoratorHit:
    start: int
    end: int
    expr: str
    source: str


@dataclass
class DecoratorBinding:
    var_name: str
    expr: str
    source: str
    target: str
    sentence: str


@dataclass
class DecoratorContext:
    source_path: Path
    output_path: Path
    text: str
    masked_text: str
    decorators: List[DecoratorHit]
    cpp_functions: List[dict]
    cpp_function_like_nodes: List[dict]
    bindings: List[DecoratorBinding]
    generated_prefix_lines: List[str]
    generated_suffix_lines: List[str]
    module_state: Dict[str, Any]


def parse_tree_strict(text: bytes):
    if cpp_parser is None:
        raise RuntimeError("tree_sitter C++ parser is unavailable.")
    try:
        return cpp_parser.parse(text)
    except Exception as e:
        raise RuntimeError(f"tree_sitter C++ parse failed: {e}") from e


def _node_text(code: bytes, node) -> str:
    return code[node.start_byte:node.end_byte].decode("utf-8", errors="ignore")


def _is_outside_function_scope(node) -> bool:
    cur = node.parent
    while cur is not None:
        if cur.type in ("compound_statement", "function_definition", "lambda_expression"):
            return False
        cur = cur.parent
    return True


def _skip_string_or_char(text: str, i: int) -> int:
    quote = text[i]
    i += 1
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\\":
            i += 2
            continue
        if c == quote:
            return i + 1
        i += 1
    return n


def _skip_line_comment(text: str, i: int) -> int:
    n = len(text)
    i += 2
    while i < n and text[i] != "\n":
        i += 1
    return i


def _skip_block_comment(text: str, i: int) -> int:
    n = len(text)
    i += 2
    while i + 1 < n:
        if text[i] == "*" and text[i + 1] == "/":
            return i + 2
        i += 1
    return n


def _split_macro_args(raw: str) -> List[str]:
    args: List[str] = []
    n = len(raw)
    i = 0
    start = 0
    paren = 0
    bracket = 0
    brace = 0
    while i < n:
        c = raw[i]
        if c == "/" and i + 1 < n and raw[i + 1] == "/":
            i = _skip_line_comment(raw, i)
            continue
        if c == "/" and i + 1 < n and raw[i + 1] == "*":
            i = _skip_block_comment(raw, i)
            continue
        if c == '"' or c == "'":
            i = _skip_string_or_char(raw, i)
            continue
        if c == "(":
            paren += 1
            i += 1
            continue
        if c == ")":
            if paren > 0:
                paren -= 1
            i += 1
            continue
        if c == "[":
            bracket += 1
            i += 1
            continue
        if c == "]":
            if bracket > 0:
                bracket -= 1
            i += 1
            continue
        if c == "{":
            brace += 1
            i += 1
            continue
        if c == "}":
            if brace > 0:
                brace -= 1
            i += 1
            continue
        if c == "," and paren == 0 and bracket == 0 and brace == 0:
            args.append(raw[start:i].strip())
            i += 1
            start = i
            continue
        i += 1
    tail = raw[start:].strip()
    if tail:
        args.append(tail)
    return args


def _normalize_macro_expr(expr: str) -> str:
    out = expr.strip()
    if not out:
        return ""
    if not out.startswith("@"):
        raise ValueError(
            "decorator(...) arguments must start with '@', "
            f"got: {out!r}"
        )
    out = out[1:].strip()
    if not out:
        raise ValueError("decorator(...) argument '@' must be followed by an expression.")
    return out


def _find_decorator_macros(text: str) -> List[DecoratorHit]:
    out: List[DecoratorHit] = []
    n = len(text)
    i = 0
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            i = _skip_line_comment(text, i)
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i = _skip_block_comment(text, i)
            continue
        if c == '"' or c == "'":
            i = _skip_string_or_char(text, i)
            continue

        if c.isalpha() or c == "_":
            ident_start = i
            i += 1
            while i < n and (text[i].isalnum() or text[i] == "_"):
                i += 1
            ident = text[ident_start:i]

            if ident != "decorator":
                continue

            prev = text[ident_start - 1] if ident_start > 0 else ""
            if prev and (prev.isalnum() or prev == "_" or prev == ":"):
                continue

            j = i
            while j < n and text[j].isspace():
                j += 1
            if j >= n or text[j] != "(":
                continue

            depth = 1
            k = j + 1
            while k < n and depth > 0:
                ch = text[k]
                if ch == "/" and k + 1 < n and text[k + 1] == "/":
                    k = _skip_line_comment(text, k)
                    continue
                if ch == "/" and k + 1 < n and text[k + 1] == "*":
                    k = _skip_block_comment(text, k)
                    continue
                if ch == '"' or ch == "'":
                    k = _skip_string_or_char(text, k)
                    continue
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                k += 1

            if depth != 0:
                continue

            macro_end = k
            args_src = text[j + 1:macro_end - 1]
            for raw_arg in _split_macro_args(args_src):
                try:
                    expr = _normalize_macro_expr(raw_arg)
                except ValueError as e:
                    raise RuntimeError(
                        f"{e} (near byte {ident_start} in decorator macro)"
                    ) from e
                if not expr:
                    continue
                out.append(
                    DecoratorHit(
                        start=ident_start,
                        end=macro_end,
                        expr=expr,
                        source="macro",
                    )
                )
            i = macro_end
            continue

        i += 1

    out.sort(key=lambda d: d.start)
    return out


def _find_decorators_parser(text: str) -> List[DecoratorHit]:
    return _find_decorator_macros(text)


def find_decorators(text: str) -> List[DecoratorHit]:
    return _find_decorators_parser(text)


def mask_ranges_keep_layout(text: str, ranges: List[Tuple[int, int]]) -> str:
    buf = list(text)
    for s, e in ranges:
        for i in range(s, e):
            if buf[i] != "\n":
                buf[i] = " "
    return "".join(buf)


def find_first_descendant_by_type(node, wanted_type: str):
    if node is None:
        return None
    if node.type == wanted_type:
        return node
    for child in node.children:
        found = find_first_descendant_by_type(child, wanted_type)
        if found is not None:
            return found
    return None


def extract_param_type_from_lhs(lhs: str) -> str:
    s = lhs.strip()
    if not s:
        return s

    m = re.search(r"[A-Za-z_]\w*\s*$", s)
    if m is None:
        return s

    prefix = s[:m.start()].rstrip()
    if not prefix:
        return s

    if prefix.endswith("::"):
        return s

    return prefix


def get_function_info(func_node, code):
    declarator_node = func_node.child_by_field_name("declarator")
    if not declarator_node:
        return None
    parameter_list_node = find_first_descendant_by_type(declarator_node, "parameter_list")
    if parameter_list_node is None:
        return None

    func_name_node = declarator_node
    while func_name_node.type not in ["identifier", "field_identifier", "qualified_identifier"]:
        if func_name_node.child_by_field_name("declarator"):
            func_name_node = func_name_node.child_by_field_name("declarator")
        elif func_name_node.children:
            func_name_node = func_name_node.children[-1]
        else:
            break

    def get_node_text(node, src):
        return src[node.start_byte:node.end_byte].decode("utf-8", errors="ignore").strip()

    def merge_scope_with_qualified(scope_parts: List[str], qualified_parts: List[str]) -> List[str]:
        max_k = min(len(scope_parts), len(qualified_parts))
        overlap = 0
        for k in range(max_k, -1, -1):
            if scope_parts[len(scope_parts) - k:] == qualified_parts[:k]:
                overlap = k
                break
        return scope_parts[: len(scope_parts) - overlap] + qualified_parts

    func_name_text = get_node_text(func_name_node, code)
    if not func_name_text:
        return None

    func_name_text = func_name_text.strip()
    if func_name_text.startswith("::"):
        func_name_text = func_name_text[2:]

    parent_nodes = []
    current_node = func_node.parent
    while current_node and current_node.type != "translation_unit":
        if current_node.type in ["class_specifier", "struct_specifier"]:
            class_name_node = current_node.child_by_field_name("name")
            if class_name_node:
                parent_nodes.append(get_node_text(class_name_node, code))
        elif current_node.type == "namespace_definition":
            ns_name_node = current_node.child_by_field_name("name")
            if ns_name_node:
                parent_nodes.append(get_node_text(ns_name_node, code))
        current_node = current_node.parent

    scope_parts = []
    if parent_nodes:
        parent_nodes.reverse()
        scope_parts = [n for n in parent_nodes if n]

    if "::" in func_name_text:
        qualified_parts = [p for p in func_name_text.split("::") if p]
        merged_parts = merge_scope_with_qualified(scope_parts, qualified_parts)
        fullname = "::".join(merged_parts)
        func_name = qualified_parts[-1] if qualified_parts else ""
    else:
        func_name = func_name_text
        if scope_parts:
            fullname = "::".join(scope_parts) + "::" + func_name
        else:
            fullname = func_name

    param_defaults = []
    param_types = []
    param_index = 0
    for child in parameter_list_node.children:
        if child.type == "optional_parameter_declaration":
            eq_node = None
            for n in child.children:
                if n.type == "=":
                    eq_node = n
                    break

            lhs = ""
            if eq_node is not None:
                lhs = code[child.start_byte:eq_node.start_byte].decode("utf-8", errors="ignore").strip()
                rhs = code[eq_node.end_byte:child.end_byte].decode("utf-8", errors="ignore").strip()
                param_type = extract_param_type_from_lhs(lhs)
                if param_type and rhs:
                    param_defaults.append(
                        {
                            "index": param_index,
                            "param_type": param_type,
                            "default_expr": rhs,
                        }
                    )
            declared_type = extract_param_type_from_lhs(lhs if lhs else code[child.start_byte:child.end_byte].decode("utf-8", errors="ignore").strip())
            param_types.append(declared_type)
            param_index += 1
        elif child.type in ["parameter_declaration", "variadic_parameter"]:
            if child.type == "parameter_declaration":
                decl_text = code[child.start_byte:child.end_byte].decode("utf-8", errors="ignore").strip()
                param_types.append(extract_param_type_from_lhs(decl_text))
            else:
                param_types.append("...")
            param_index += 1

    return {
        "name": func_name,
        "fullname": fullname,
        "start": func_node.start_byte,
        "end": func_node.end_byte,
        "node_type": func_node.type,
        "param_count": param_index,
        "param_types": param_types,
        "param_defaults": param_defaults,
    }


def extract_all_cpp_functions(text: bytes):
    tree = parse_tree_strict(text)

    root_node = tree.root_node
    functions = []

    def traverse(node):
        if node.type == "function_definition":
            func_info = get_function_info(node, text)
            if func_info:
                functions.append(func_info)
        for child in node.children:
            traverse(child)

    traverse(root_node)
    return functions


def extract_all_cpp_functions_and_declarations(text: bytes):
    tree = parse_tree_strict(text)

    root_node = tree.root_node
    function_like_nodes = []

    def traverse(node):
        if node.type in ["function_definition", "declaration", "field_declaration"]:
            func_info = get_function_info(node, text)
            if func_info:
                function_like_nodes.append(func_info)
        for child in node.children:
            traverse(child)

    traverse(root_node)
    return function_like_nodes


def parse_modules_arg(raw: str) -> List[str]:
    if raw is None:
        return []
    out = []
    for part in raw.split(","):
        name = part.strip()
        if name:
            out.append(name)
    # Keep order, remove duplicates.
    seen = set()
    deduped = []
    for name in out:
        if name in seen:
            continue
        seen.add(name)
        deduped.append(name)
    return deduped


def load_modules(module_names: List[str]):
    if len(module_names) == 0:
        return []

    base_dir = Path(__file__).resolve().parent
    loaded = []
    for module_name in module_names:
        module_file = base_dir / f"{module_name}.py"
        if not module_file.exists():
            raise RuntimeError(
                f"Decorator module '{module_name}' not found: {module_file}"
            )

        unique_name = f"cppbm_decorator_module_{module_name}"
        spec = importlib.util.spec_from_file_location(unique_name, module_file)
        if spec is None or spec.loader is None:
            raise RuntimeError(
                f"Failed to load decorator module '{module_name}' from {module_file}"
            )

        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        loaded.append((module_name, module))
    return loaded


def run_handle_modules(loaded_modules, context: DecoratorContext) -> None:
    for module_name, module in loaded_modules:
        hook = getattr(module, "handle", None)
        if hook is None:
            continue
        hook(context)
        print(f"[decorator] module handle: {module_name}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="inp", required=True)
    p.add_argument("--out", dest="out", required=True)
    p.add_argument(
        "--modules",
        default="",
        help="Comma-separated decorator modules, e.g. inject",
    )
    args = p.parse_args()

    src = Path(args.inp)
    dst = Path(args.out)
    module_names = parse_modules_arg(args.modules)
    loaded_modules = load_modules(module_names)

    text, source_codec, source_bom = read_text_auto(src)

    decorators = find_decorators(text)
    masked = mask_ranges_keep_layout(text, [(dec.start, dec.end) for dec in decorators])
    cpp_functions = extract_all_cpp_functions(masked.encode("utf-8"))
    cpp_function_like_nodes = extract_all_cpp_functions_and_declarations(masked.encode("utf-8"))

    context = DecoratorContext(
        source_path=src,
        output_path=dst,
        text=text,
        masked_text=masked,
        decorators=decorators,
        cpp_functions=cpp_functions,
        cpp_function_like_nodes=cpp_function_like_nodes,
        bindings=[],
        generated_prefix_lines=[],
        generated_suffix_lines=[],
        module_state={},
    )

    masked = context.masked_text

    def find_nearest_function(end: int):
        min_distance = 0
        result = None
        for func in context.cpp_function_like_nodes:
            distance = func["start"] - end
            if distance > 0 and (distance < min_distance or min_distance == 0):
                min_distance = distance
                result = func
        return result

    for dec_index, dec in enumerate(context.decorators):
        func = find_nearest_function(dec.end)
        if func is None:
            raise RuntimeError(
                f"Cannot find target function for decorator marker ({dec.source}:{dec.expr}) near byte {dec.end} in {src}"
            )

        var_name = f'__cppbm_dec_{func["name"]}_{dec.start}_{dec_index}'
        func_fullname = func["fullname"]
        sentence = f'inline auto {var_name} = ({dec.expr}).Bind<&{func_fullname}>();'
        print(f"[decorator] reg {func_fullname} <- {dec.source}:{dec.expr}")
        context.bindings.append(
            DecoratorBinding(
                var_name=var_name,
                expr=dec.expr,
                source=dec.source,
                target=func_fullname,
                sentence=sentence,
            )
        )

    run_handle_modules(loaded_modules, context)

    if len(context.bindings) > 0 or len(context.generated_prefix_lines) > 0 or len(context.generated_suffix_lines) > 0:
        masked += "\n\n\n// Generated decorator bindings.\n"
        for line in context.generated_prefix_lines:
            masked += line + "\n"
        if len(context.generated_prefix_lines) > 0 and len(context.bindings) > 0:
            masked += "\n"
        for binding in context.bindings:
            masked += binding.sentence + "\n"
        if len(context.generated_suffix_lines) > 0 and len(context.bindings) > 0:
            masked += "\n"
        for line in context.generated_suffix_lines:
            masked += line + "\n"

    write_text_auto(dst, masked, source_codec, source_bom)


if __name__ == "__main__":
    main()
