'''
Decorator preprocessor (decorator.py).

Responsibilities:
1) Remove `decorator(...)` markers from source while preserving layout.
2) Append generated decorator-binding instances.

Non-responsibilities:
- Does NOT generate @inject default-argument metadata.
  That part is handled by `inject.py`.
'''
import re
import argparse
from pathlib import Path
import sys

from dataclasses import dataclass
from typing import Optional, List, Tuple

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser

DECORATOR_AT_SPLIT_RE = re.compile(
    r"""
    (?:
        (?P<global>::)                                   # ::@xxx
        |
        (?P<ns>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*::    # aaa::@xxx / a::b::@xxx
    )?
    \s*@(?P<tail>.+)
    \s*$
    """,
    re.VERBOSE,
)

SIMPLE_DECORATOR_NAME_RE = re.compile(r"^[A-Za-z_]\w*$")

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
        # New API style
        try:
            return Language(candidate)
        except Exception:
            pass
        # Old API style
        try:
            return Language(candidate, "cpp")
        except Exception:
            pass
        if isinstance(candidate, (str, bytes, Path)):
            try:
                return Language(str(candidate), "cpp")
            except Exception:
                pass

    # Optional fallback package
    try:
        from tree_sitter_languages import get_language
        return get_language("cpp")
    except Exception:
        return None


def build_cpp_parser(language):
    if language is None:
        return None

    # Try constructor styles across tree_sitter versions.
    try:
        parser = Parser(language)
    except Exception:
        parser = Parser()

    # Force language binding for old/new APIs.
    try:
        if hasattr(parser, "set_language"):
            parser.set_language(language)
        elif hasattr(parser, "language"):
            parser.language = language
    except Exception:
        return None

    # Probe parse to ensure parser is actually usable.
    try:
        parser.parse(b"int __cppbm_probe__() { return 0; }")
    except Exception:
        return None

    return parser


# language to parse cpp
CPP_LANGUAGE = build_cpp_language()
cpp_parser = build_cpp_parser(CPP_LANGUAGE)
STRICT_PARSER = False
WARNED_REGEX_FALLBACK = False

def set_strict_parser(enabled: bool):
    global STRICT_PARSER
    STRICT_PARSER = bool(enabled)


def fallback_or_fail(reason: str, text: bytes):
    global WARNED_REGEX_FALLBACK
    if STRICT_PARSER:
        raise RuntimeError(
            "strict parser mode enabled, but tree_sitter C++ parser is unavailable or failed: "
            + reason
        )
    if not WARNED_REGEX_FALLBACK:
        print(
            "warning: tree_sitter C++ parser unavailable/failed, fallback to regex function scan. "
            + reason,
            file=sys.stderr,
        )
        WARNED_REGEX_FALLBACK = True
    return extract_all_cpp_functions_regex(text)

# info of decorator
@dataclass
class DecoratorHit:
    start: int
    end: int
    kind: str  # "symbol" or "expr"
    ns: Optional[str]   # "" / "::" / "a::b"
    name: Optional[str]
    expr: Optional[str]
    
def find_decorators(text: str):
    def ns_prefix(ns: str) -> str:
        if ns == "":
            return ""
        if ns == "::":
            return "::"
        return ns + "::"

    def skip_string(i: int, quote: str) -> int:
        i += 1
        while i < len(text):
            c = text[i]
            if c == "\\":
                i += 2
                continue
            if c == quote:
                return i + 1
            i += 1
        return i

    def parse_decorator_body(raw: str):
        body = raw.strip()
        if not body:
            return None

        m = DECORATOR_AT_SPLIT_RE.fullmatch(body)
        if not m:
            return None

        ns = "::" if m.group("global") else (m.group("ns") or "")
        tail = m.group("tail").strip()
        if not tail:
            return None

        if SIMPLE_DECORATOR_NAME_RE.fullmatch(tail):
            return DecoratorHit(
                start=0,
                end=0,
                kind="symbol",
                ns=ns,
                name=tail,
                expr=None,
            )

        return DecoratorHit(
            start=0,
            end=0,
            kind="expr",
            ns=ns,
            name=None,
            expr=ns_prefix(ns) + tail,
        )

    out = []
    i = 0
    key = "decorator"
    n = len(text)
    while i < n:
        ch = text[i]

        # Skip line comments: // ...
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            i += 2
            while i < n and text[i] != "\n":
                i += 1
            continue

        # Skip block comments: /* ... */
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            if i + 1 < n:
                i += 2
            continue

        pos = i
        if not text.startswith(key, pos):
            i += 1
            continue

        prev_ok = pos == 0 or (not (text[pos - 1].isalnum() or text[pos - 1] == "_"))
        next_idx = pos + len(key)
        next_ok = next_idx >= n or (not (text[next_idx].isalnum() or text[next_idx] == "_"))
        if not (prev_ok and next_ok):
            i = pos + 1
            continue

        j = next_idx
        while j < n and text[j].isspace():
            j += 1
        if j >= n or text[j] != "(":
            i = pos + 1
            continue

        body_start = j + 1
        depth = 1
        k = body_start
        while k < n and depth > 0:
            ch = text[k]
            if ch == '"' or ch == "'":
                k = skip_string(k, ch)
                continue
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1

        if depth != 0:
            raise RuntimeError(f"Unmatched parenthesis for decorator() near byte {pos}")

        body = text[body_start:k]
        parsed = parse_decorator_body(body)
        if parsed is not None:
            parsed.start = pos
            parsed.end = k + 1
            out.append(parsed)

        i = k + 1

    return out

# replace decorator to white blanks
def mask_ranges_keep_layout(text: str, ranges: List[Tuple[int, int]]) -> str:
    buf = list(text)
    for s, e in ranges:
        for i in range(s, e):
            if buf[i] != "\n":
                buf[i] = " "
    return "".join(buf)

# get cpp function info
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
    # extract function name with namespace and class
    declarator_node = func_node.child_by_field_name('declarator')
    if not declarator_node:
        return None
    
    # find true function name node
    func_name_node = declarator_node
    while func_name_node.type not in ['identifier', 'field_identifier', 'qualified_identifier']:
        if func_name_node.child_by_field_name('declarator'):
            func_name_node = func_name_node.child_by_field_name('declarator')
        elif func_name_node.children:
            func_name_node = func_name_node.children[-1]
        else:
            break
    
    def get_node_text(node, code):
        return code[node.start_byte:node.end_byte].decode('utf-8').strip()

    def merge_scope_with_qualified(scope_parts: List[str], qualified_parts: List[str]) -> List[str]:
        # Find the maximal overlap where suffix(scope_parts) == prefix(qualified_parts),
        # then prepend only the missing outer scopes.
        max_k = min(len(scope_parts), len(qualified_parts))
        overlap = 0
        for k in range(max_k, -1, -1):
            if scope_parts[len(scope_parts) - k:] == qualified_parts[:k]:
                overlap = k
                break
        return scope_parts[:len(scope_parts) - overlap] + qualified_parts
    
    func_name_text = get_node_text(func_name_node, code)
    if not func_name_text:
        return None

    func_name_text = func_name_text.strip()
    if func_name_text.startswith("::"):
        func_name_text = func_name_text[2:]

    # find class / namespace
    parent_nodes = []
    current_node = func_node.parent
    while current_node and current_node.type != 'translation_unit':
        if current_node.type in ['class_specifier', 'struct_specifier']:
            class_name_node = current_node.child_by_field_name('name')
            if class_name_node:
                parent_nodes.append(get_node_text(class_name_node, code))
        elif current_node.type == 'namespace_definition':
            ns_name_node = current_node.child_by_field_name('name')
            if ns_name_node:
                parent_nodes.append(get_node_text(ns_name_node, code))
        current_node = current_node.parent

    scope_parts = []
    if parent_nodes:
        parent_nodes.reverse()
        scope_parts = [n for n in parent_nodes if n]

    if "::" in func_name_text:
        # Out-of-class / out-of-namespace definitions may carry a partially
        # qualified name (e.g. Test::print inside namespace ns { ... }).
        # Merge lexical scopes and declarator name without duplicating overlap.
        qualified_parts = [p for p in func_name_text.split("::") if p]
        merged_parts = merge_scope_with_qualified(scope_parts, qualified_parts)
        fullname = "::".join(merged_parts)
        func_name = qualified_parts[-1] if qualified_parts else ""
    else:
        func_name = func_name_text

    # contact full function name
    if "::" not in func_name_text:
        if scope_parts:
            fullname = "::".join(scope_parts) + "::" + func_name
        else:
            fullname = func_name

    param_defaults = []
    parameter_list_node = find_first_descendant_by_type(declarator_node, "parameter_list")
    if parameter_list_node is not None:
        param_index = 0
        for child in parameter_list_node.children:
            if child.type == "optional_parameter_declaration":
                eq_node = None
                for n in child.children:
                    if n.type == "=":
                        eq_node = n
                        break

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
                param_index += 1
            elif child.type in ["parameter_declaration", "variadic_parameter"]:
                param_index += 1

    function_info = {
        "name": func_name,
        "fullname": fullname,
        "start": func_node.start_byte,  
        "end": func_node.end_byte,
        "param_defaults": param_defaults,
    }
    return function_info

# parse source file, and find functions
def extract_all_cpp_functions_regex(text: bytes):
    src = text.decode("utf-8", errors="ignore")
    functions = []
    # Rough fallback parser for function definitions.
    pattern = re.compile(
        r"(?P<name>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:->\s*[^{};]+)?\s*\{"
    )
    blacklist = {"if", "for", "while", "switch", "catch"}
    for m in pattern.finditer(src):
        fullname = m.group("name")
        short_name = fullname.split("::")[-1]
        if short_name in blacklist:
            continue
        functions.append(
            {
                "name": short_name,
                "fullname": fullname,
                "start": m.start(),
                "end": m.end(),
                "param_defaults": [],
            }
        )
    return functions


def extract_all_cpp_functions(text : bytes):
    if cpp_parser is None:
        return fallback_or_fail("parser init failed", text)

    try:
        tree = cpp_parser.parse(text)
    except Exception as e:
        return fallback_or_fail("parse failed ({0})".format(e), text)

    root_node = tree.root_node
    
    functions = []
    def traverse(node):
        if node.type == 'function_definition':
            func_info = get_function_info(node, text)
            if func_info:
                functions.append(func_info)
        for child in node.children:
            traverse(child)
            
    traverse(root_node)
    return functions


def extract_all_cpp_functions_and_declarations(text: bytes):
    if cpp_parser is None:
        return fallback_or_fail("parser init failed", text)

    try:
        tree = cpp_parser.parse(text)
    except Exception as e:
        return fallback_or_fail("parse failed ({0})".format(e), text)

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


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="inp", required=True)
    p.add_argument("--out", dest="out", required=True)
    p.add_argument(
        "--strict-parser",
        action="store_true",
        help="Fail when tree_sitter C++ parser is unavailable or parse fails.",
    )
    args = p.parse_args()
    set_strict_parser(args.strict_parser)

    src = Path(args.inp)
    dst = Path(args.out)
    
    # Keep original source text. We only mask decorator(...) calls and then append
    # generated decorator instances at the end of file.
    text = src.read_text(encoding="utf-8")

    decorators = find_decorators(text)
    
    # Remove decorator(xxx) syntax from compile input.
    masked = mask_ranges_keep_layout(text, [(dec.start, dec.end) for dec in decorators])

    # Parse function list from masked text and bind each decorator to the nearest
    # following function definition.
    cpp_functions = extract_all_cpp_functions(masked.encode())

    if len(decorators) > 0:
        masked += "\n\n\n// Generated decorator instances.\n"
    
    # find the nearest function to the decorator
    def find_nearest_funcion(end):
        min_distance = 0
        result = None
        for func in cpp_functions:
            distance = func["start"] - end
            if distance > 0 and (distance < min_distance or min_distance == 0):
                min_distance = distance
                result = func
        return result
    
    for dec in decorators:
        func = find_nearest_funcion(dec.end)
        if func is None:
            dec_label = dec.name if dec.kind == "symbol" else dec.expr
            raise RuntimeError(
                f"Cannot find target function for decorator @{dec_label} near byte {dec.end} in {src}"
            )
        
        # create decorator
        
        var_name = "__cppbm_" + func["name"] + "_dec_" + str(func["start"])
        func_fullname = func["fullname"]

        if dec.kind == "symbol":
            ns_prefix = dec.ns
            if ns_prefix == "":
                ns_scope = ""
            elif ns_prefix == "::":
                ns_scope = "::"
            else:
                ns_scope = ns_prefix + "::"

            dec_type = ns_scope + "_Decorator_" + dec.name + "_"
            sentence = f'inline {dec_type}<&{func_fullname}> {var_name}{{}};'
            print(f'generate decorator ({ns_scope}@{dec.name} => {func_fullname}) | ' + sentence)
        elif dec.kind == "expr":
            sentence = f'inline auto {var_name} = ({dec.expr}).bind<&{func_fullname}>();'
            print(f'generate decorator (@{dec.expr} => {func_fullname}) | ' + sentence)
        else:
            raise RuntimeError(f"Unknown decorator kind: {dec.kind}")

        masked += sentence + "\n"
    
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(masked, encoding="utf-8")

if __name__ == "__main__":
    main()
            
