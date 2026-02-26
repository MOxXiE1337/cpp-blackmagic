"""
inject module for decorator.py modular pipeline.

Hook contract:
- handle(context): rewrite generated inject Bind calls with metadata args.
"""

import re
import secrets
from typing import Dict, List


INJECT_EXPR_RE = re.compile(r"(?:::)?(?:[A-Za-z_]\w*::)*inject$")
DEPENDS_EXPR_RE = re.compile(r"(?:::)?(?:[A-Za-z_]\w*::)*Depends\(.*\)$")


def _normalize_expr(expr: str) -> str:
    return re.sub(r"\s+", "", expr or "")


def _normalize_default_expr(expr: str) -> str:
    return re.sub(r"\s+", "", expr or "")


def _is_inject_expr(expr: str) -> bool:
    return INJECT_EXPR_RE.fullmatch(_normalize_expr(expr)) is not None


def _is_cppbm_inject_binding(binding) -> bool:
    # inject module only applies to decorator(@inject) macro args.
    # In decorator.py, this arrives as:
    # - source == "macro"
    # - expr   == "inject" (or namespaced form ending with ::inject)
    return getattr(binding, "source", "") == "macro" and _is_inject_expr(binding.expr)


def _is_depends_default_expr(expr: str) -> bool:
    return DEPENDS_EXPR_RE.fullmatch(_normalize_default_expr(expr)) is not None


def _records_by_fullname(context) -> Dict[str, List[dict]]:
    out: Dict[str, List[dict]] = {}
    for node in context.cpp_function_like_nodes:
        out.setdefault(node["fullname"], []).append(node)
    for nodes in out.values():
        nodes.sort(key=lambda n: n["start"])
    return out


def _validate_default_consistency(fullname: str, nodes: List[dict]) -> None:
    param_counts = set()
    signatures = set()
    for n in nodes:
        if "param_count" in n:
            param_counts.add(n["param_count"])
        signatures.add(tuple(n.get("param_types", [])))
    if len(param_counts) > 1:
        raise RuntimeError(
            "inject target has ambiguous overload set (different parameter counts) "
            f"for '{fullname}'. Use unique function name/signature."
        )
    if len(signatures) > 1:
        raise RuntimeError(
            "inject target has ambiguous overload set (different parameter types) "
            f"for '{fullname}'. Use unique function name/signature."
        )

    seen_defaults = {}
    for n in nodes:
        for pd in n.get("param_defaults", []):
            idx = pd["index"]
            sig = (pd["param_type"], _normalize_default_expr(pd["default_expr"]))
            prev = seen_defaults.get(idx)
            if prev is None:
                seen_defaults[idx] = sig
                continue
            if prev != sig:
                raise RuntimeError(
                    "inject target has conflicting default expressions between declaration/definition "
                    f"for '{fullname}', param #{idx}."
                )


def _merge_defaults(nodes: List[dict]) -> List[dict]:
    merged: Dict[int, dict] = {}
    for n in nodes:
        for pd in n.get("param_defaults", []):
            if pd["index"] not in merged:
                merged[pd["index"]] = pd
    return [merged[i] for i in sorted(merged)]


def _build_and_validate_defaults_map(context):
    records = _records_by_fullname(context)
    defaults_by_fullname = {}

    for fullname, nodes in records.items():
        _validate_default_consistency(fullname, nodes)
        defaults = _merge_defaults(nodes)
        if len(defaults) > 0:
            defaults_by_fullname[fullname] = defaults

    inject_bindings = [b for b in context.bindings if _is_cppbm_inject_binding(b)]
    for b in inject_bindings:
        if b.target not in records:
            raise RuntimeError(
                f"inject target '{b.target}' cannot be resolved from tree-sitter function nodes."
            )
        # Enforce node kinds we explicitly support today.
        allowed = {"function_definition", "declaration", "field_declaration"}
        for n in records[b.target]:
            node_type = n.get("node_type", "")
            if node_type not in allowed:
                raise RuntimeError(
                    f"inject target '{b.target}' resolved unsupported node type '{node_type}'."
                )

    return defaults_by_fullname


def handle(context):
    state = context.module_state.get("inject")
    if state is None:
        state = {"defaults_by_fullname": None}
        context.module_state["inject"] = state

    if state["defaults_by_fullname"] is None:
        state["defaults_by_fullname"] = _build_and_validate_defaults_map(context)

    defaults_by_fullname = state["defaults_by_fullname"]
    inject_bindings = [b for b in context.bindings if _is_cppbm_inject_binding(b)]
    if len(inject_bindings) == 0:
        return

    alias = "_" + secrets.token_hex(3)
    context.generated_prefix_lines.append(f"namespace {alias} = ::cpp::blackmagic::depends;")

    for binding in inject_bindings:
        defaults = [
            pd
            for pd in defaults_by_fullname.get(binding.target, [])
            if _is_depends_default_expr(pd["default_expr"])
        ]
        if len(defaults) == 0:
            continue

        args = []
        for pd in defaults:
            args.append(
                "{alias}::InjectArgMeta<{index}, {param_type}>"
                "([]() {{ return {default_expr}; }})".format(
                    alias=alias,
                    index=pd["index"],
                    param_type=pd["param_type"],
                    default_expr=pd["default_expr"],
                )
            )

        indented_args = ",\n".join(f"    {arg}" for arg in args)
        binding.sentence = (
            f"inline auto {binding.var_name} = ({binding.expr}).Bind<&{binding.target}>(\n"
            f"{indented_args}\n"
            f");"
        )
        print(f"[inject] bind-meta {binding.target} ({len(defaults)} args)")
