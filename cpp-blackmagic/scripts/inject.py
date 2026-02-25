'''
Dependency-injection metadata preprocessor (inject.py).

Responsibilities:
1) Scan `decorator(...)` markers and locate functions decorated with `@inject`.
2) For each default argument on those functions, generate InjectRegistry metadata.

Non-responsibilities:
- Does NOT remove decorator(...) syntax.
- Does NOT generate generic decorator binding instances.
  Those are handled by `decorator.py`.
'''

import argparse
import re
from pathlib import Path
from typing import Set

# Reuse parser/discovery utilities from decorator.py to keep behavior identical.
from decorator import (
    find_decorators,
    mask_ranges_keep_layout,
    extract_all_cpp_functions,
    extract_all_cpp_functions_and_declarations,
    set_strict_parser,
    read_text_auto,
    write_text_auto,
)

GENERATED_INJECT_BIND_RE = re.compile(
    r"_Decorator_inject_<\s*&\s*(?P<fullname>(?:::)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*>"
)

GENERATED_METADATA_BLOCK_RE = re.compile(
    r"\n*// Generated @inject default-argument metadata\.\n"
    r"(?:static const bool __cppbm_default_arg_reg_[^\n]*\n)*",
    re.MULTILINE,
)


def is_inject_decorator(dec) -> bool:
    # Only symbol-form @inject is treated as DI decorator metadata source.
    # Expression-form decorators are ignored here.
    return dec.kind == "symbol" and dec.name == "inject"


def build_default_arg_registration(func: dict, pd: dict) -> str:
    reg_var = f'__cppbm_default_arg_reg_{func["name"]}_{func["start"]}_{pd["index"]}'
    func_fullname = func["fullname"]
    param_type = pd["param_type"]
    default_expr = pd["default_expr"]

    # Metadata type is deduced in C++ from:
    # - declared parameter type
    # - exact default expression type
    # This avoids fragile string checks in Python.
    reg_factory = (
        f'[]() {{ return ::cpp::blackmagic::depends::MakeDefaultArgMetadata<{param_type}>({default_expr}); }}'
    )

    return (
        f'static const bool {reg_var} = '
        f'::cpp::blackmagic::depends::InjectRegistry::Register<&{func_fullname}, {pd["index"]}>('
        f'{reg_factory});'
    )


def build_default_arg_registration_async(func: dict, pd: dict) -> str:
    reg_var = f'__cppbm_default_arg_reg_async_{func["name"]}_{func["start"]}_{pd["index"]}'
    func_fullname = func["fullname"]
    param_type = pd["param_type"]
    default_expr = pd["default_expr"]

    # Async metadata variant:
    # keep exact expression text and defer factory execution into coroutine pipeline.
    reg_factory = (
        f'[]() {{ return ::cpp::blackmagic::depends::MakeDefaultArgMetadataAsync<{param_type}>({default_expr}); }}'
    )

    return (
        f'static const bool {reg_var} = '
        f'::cpp::blackmagic::depends::InjectRegistry::Register<&{func_fullname}, {pd["index"]}>('
        f'{reg_factory});'
    )


def extract_inject_targets_from_generated_bindings(text: str) -> Set[str]:
    # Supports decorator-first pipeline:
    # decorator.py emits lines like:
    #   inline _Decorator_inject_<&ns::func> __cppbm_func_dec_xx{};
    # inject.py can resolve targets from these generated bindings.
    targets: Set[str] = set()
    for m in GENERATED_INJECT_BIND_RE.finditer(text):
        fullname = m.group("fullname")
        if fullname.startswith("::"):
            fullname = fullname[2:]
        targets.add(fullname)
    return targets


def strip_generated_inject_metadata(text: str) -> str:
    # Make inject.py idempotent:
    # remove previously generated metadata block(s) before appending fresh output.
    return GENERATED_METADATA_BLOCK_RE.sub("\n", text)


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

    text, source_codec, source_bom = read_text_auto(src)
    base_text = strip_generated_inject_metadata(text)
    decorators = find_decorators(base_text)

    # Keep source unchanged in output (decorator removal is done by decorator.py),
    # but parse functions from a masked view so decorator(...) tokens do not break parsing.
    masked = mask_ranges_keep_layout(base_text, [(dec.start, dec.end) for dec in decorators])
    cpp_functions = extract_all_cpp_functions(masked.encode())
    cpp_function_like_nodes = extract_all_cpp_functions_and_declarations(masked.encode())

    default_arg_registry_sentences = []
    default_arg_registry_seen = set()

    # Find nearest following function for each decorator marker, same rule as decorator.py.
    def find_nearest_function(end: int):
        min_distance = 0
        result = None
        for func in cpp_functions:
            distance = func["start"] - end
            if distance > 0 and (distance < min_distance or min_distance == 0):
                min_distance = distance
                result = func
        return result

    inject_targets = set()

    # Source-form @inject markers.
    for dec in decorators:
        if not is_inject_decorator(dec):
            continue

        func = find_nearest_function(dec.end)
        if func is None:
            raise RuntimeError(
                f"Cannot find target function for decorator @inject near byte {dec.end} in {src}"
            )

        inject_targets.add(func["fullname"])

    # Generated-form @inject bindings (from decorator.py output).
    inject_targets.update(extract_inject_targets_from_generated_bindings(base_text))

    function_map = {func["fullname"]: func for func in cpp_functions}
    function_like_map = {}
    merged_defaults_map = {}
    for func in cpp_function_like_nodes:
        fullname = func["fullname"]
        # Keep the first seen node as fallback metadata source.
        if fullname not in function_like_map:
            function_like_map[fullname] = func
        defaults = merged_defaults_map.setdefault(fullname, {})
        for pd in func.get("param_defaults", []):
            # First declaration providing a default for this index wins.
            if pd["index"] not in defaults:
                defaults[pd["index"]] = pd

    for func_fullname in sorted(inject_targets):
        func = function_map.get(func_fullname, function_like_map.get(func_fullname))
        if func is None:
            # If generated binding references a function we cannot locate in this
            # translation unit, skip it silently (likely from stale/generated text).
            continue

        param_defaults = list(func.get("param_defaults", []))
        if len(param_defaults) == 0 and func_fullname in merged_defaults_map:
            param_defaults = [merged_defaults_map[func_fullname][i] for i in sorted(merged_defaults_map[func_fullname])]
        elif func_fullname in merged_defaults_map:
            existing = {pd["index"] for pd in param_defaults}
            for i in sorted(merged_defaults_map[func_fullname]):
                if i not in existing:
                    param_defaults.append(merged_defaults_map[func_fullname][i])
            param_defaults.sort(key=lambda pd: pd["index"])

        for pd in param_defaults:
            reg_key = (func_fullname, pd["index"])
            if reg_key in default_arg_registry_seen:
                continue
            default_arg_registry_seen.add(reg_key)

            reg_sentence_sync = build_default_arg_registration(func, pd)
            reg_sentence_async = build_default_arg_registration_async(func, pd)
            default_arg_registry_sentences.append(reg_sentence_sync)
            default_arg_registry_sentences.append(reg_sentence_async)
            print(f"[inject] reg {func_fullname}#{pd['index']} sync")
            print(f"[inject] reg {func_fullname}#{pd['index']} async")

    out_text = base_text
    if len(default_arg_registry_sentences) > 0:
        out_text += "\n\n// Generated @inject default-argument metadata.\n"
        for reg_sentence in default_arg_registry_sentences:
            out_text += reg_sentence + "\n"

    write_text_auto(dst, out_text, source_codec, source_bom)


if __name__ == "__main__":
    main()
