'''
Usage: place this script into your project
Add this to your cmake project:

add_custom_command(
      OUTPUT ${SRC_OUT}
      COMMAND ${Python3_EXECUTABLE} ${PATCH_SCRIPT}
              --in ${SRC_IN}
              --replace "OLD_MACRO=>NEW_MACRO"
      DEPENDS ${SRC_IN} ${PATCH_SCRIPT}
      COMMENT "Patching foo.cpp before compile"
      VERBATIM
)
  
'''
import re
import argparse
from pathlib import Path
import sys

from dataclasses import dataclass
from typing import Optional, List, Tuple

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser

DECORATOR_RE = re.compile(
    r"""
    decorator
    \(
        \s*
        (?:
            (?P<global>::)                                   # ::@xxx
            |
            (?P<ns>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*::    # aaa::@xxx / a::b::@xxx
        )?
        \s*@(?P<name>[A-Za-z_]\w*)
        \s*
    \)
    """,
    re.VERBOSE,
)

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

if cpp_parser is None:
    print(
        "warning: tree_sitter C++ parser unavailable, fallback to regex function scan.",
        file=sys.stderr,
    )

# info of decorator
@dataclass
class DecoratorHit:
    start: int
    end: int
    ns: Optional[str]   # None / "::" / "a::b"
    name: str
    
def find_decorators(text: str):
    out = []
    for m in DECORATOR_RE.finditer(text):
        ns = "::" if m.group("global") else m.group("ns")
        if ns is None:
            ns = ""
        out.append(DecoratorHit(
            start=m.start(),
            end=m.end(),
            ns=ns,
            name=m.group("name"),
        ))
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
def get_function_info(func_node, code):
    # extract function name with namespace and class
    declarator_node = func_node.child_by_field_name('declarator')
    if not declarator_node:
        return None
    
    # find true function name node
    func_name_node = declarator_node
    while func_name_node.type not in ['identifier', 'field_identifier']:
        if func_name_node.child_by_field_name('declarator'):
            func_name_node = func_name_node.child_by_field_name('declarator')
        elif func_name_node.children:
            func_name_node = func_name_node.children[-1]
        else:
            break
    
    def get_node_text(node, code):
        return code[node.start_byte:node.end_byte].decode('utf-8').strip()
    
    func_name = get_node_text(func_name_node, code)
    if not func_name:
        return None

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

    # contact full function name
    if parent_nodes:
        parent_nodes.reverse()
        fullname = "::".join(parent_nodes) + "::" + func_name
    else:
        fullname = func_name

    function_info = {
        "name": func_name,
        "fullname": fullname,
        "start": func_node.start_byte,  
        "end": func_node.end_byte       
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
            }
        )
    return functions


def extract_all_cpp_functions(text : bytes):
    if cpp_parser is None:
        return extract_all_cpp_functions_regex(text)

    try:
        tree = cpp_parser.parse(text)
    except Exception as e:
        print(
            "warning: tree_sitter parse failed ({0}), fallback to regex function scan.".format(e),
            file=sys.stderr,
        )
        return extract_all_cpp_functions_regex(text)

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


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="inp", required=True)
    p.add_argument("--out", dest="out", required=True)
    args = p.parse_args()

    src = Path(args.inp)
    dst = Path(args.out)
    
    text = src.read_text(encoding="utf-8")

    decorators = find_decorators(text)
    
    # remove decorator(xxx)
    masked = mask_ranges_keep_layout(text, [(dec.start, dec.end) for dec in decorators])

    # find functions
    cpp_functions = extract_all_cpp_functions(masked.encode())
    
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
            raise RuntimeError(
                f"Cannot find target function for decorator @{dec.name} near byte {dec.end} in {src}"
            )
        
        # create decorator
        
        if dec.ns != "":
            dec.ns += "::"
            
        dec_type = dec.ns + "_Decorator_" + dec.name + "_"
        var_name = func["name"] + "_dec_" + str(func["start"])
        func_fullname = func["fullname"]
        sentence = f'inline {dec_type}<&{func_fullname}> {var_name}{{}};'
        print(f'generate ({dec.ns}@{dec.name} => {func_fullname}) | ' + sentence)
        masked += sentence + "\n"
    
    
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(masked, encoding="utf-8")

if __name__ == "__main__":
    main()
            
