"""
inject module for decorator.py modular pipeline.

Hook contract:
- handle(context): append InjectArgMeta metadata args into binding.meta_args.
"""

import re
import secrets
from typing import Dict, List, Tuple


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
        signatures.add(tuple(_normalize_type_text(t) for t in n.get("param_types", [])))
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


def _normalize_type_text(text: str) -> str:
    return re.sub(r"\s+", "", text or "")


def _binding_key(binding) -> Tuple[str, int, Tuple[str, ...]]:
    types = tuple(_normalize_type_text(t) for t in getattr(binding, "target_param_types", []))
    count = int(getattr(binding, "target_param_count", len(types)))
    return (binding.target, count, types)


def _format_signature_for_error(binding) -> str:
    types = [t for t in getattr(binding, "target_param_types", [])]
    return f"{binding.target}({', '.join(types)})"


def _select_nodes_for_binding(nodes: List[dict], binding) -> List[dict]:
    sig = tuple(_normalize_type_text(t) for t in getattr(binding, "target_param_types", []))
    count = int(getattr(binding, "target_param_count", len(sig)))
    selected = []
    for n in nodes:
        n_count = int(n.get("param_count", -1))
        n_sig = tuple(_normalize_type_text(t) for t in n.get("param_types", []))
        if n_count == count and n_sig == sig:
            selected.append(n)
    # Backward-compatible fallback: if binding does not carry signature fields
    # (older decorator.py output), keep previous fullname-based behavior.
    if len(selected) == 0 and not hasattr(binding, "target_param_types"):
        return nodes
    return selected


def _build_and_validate_defaults_map(context):
    records = _records_by_fullname(context)
    defaults_by_binding = {}
    inject_bindings = [b for b in context.bindings if _is_cppbm_inject_binding(b)]
    if len(inject_bindings) == 0:
        return defaults_by_binding

    # Explicitly reject duplicated @inject on the same target signature.
    key_to_binding = {}
    key_counts = {}
    for binding in inject_bindings:
        key = _binding_key(binding)
        key_counts[key] = key_counts.get(key, 0) + 1
        if key not in key_to_binding:
            key_to_binding[key] = binding
    duplicates = [key for key, count in key_counts.items() if count > 1]
    if len(duplicates) > 0:
        samples = ", ".join(_format_signature_for_error(key_to_binding[key]) for key in duplicates[:3])
        suffix = ""
        if len(duplicates) > 3:
            suffix = f" (and {len(duplicates) - 3} more)"
        raise RuntimeError(
            "duplicate @inject decorator is not allowed for the same target/signature: "
            f"{samples}{suffix}"
        )

    # Validate and collect metadata only for actual @inject targets/signatures.
    # Do not scan unrelated overload sets in the same translation unit.
    unique_bindings = []
    seen = set()
    for binding in inject_bindings:
        key = _binding_key(binding)
        if key in seen:
            continue
        seen.add(key)
        unique_bindings.append(binding)

    for binding in unique_bindings:
        target = binding.target
        if target not in records:
            raise RuntimeError(
                f"inject target '{target}' cannot be resolved from tree-sitter function nodes."
            )

        nodes = _select_nodes_for_binding(records[target], binding)
        if len(nodes) == 0:
            raise RuntimeError(
                "inject target signature cannot be resolved from tree-sitter function nodes: "
                f"'{target}' with signature {getattr(binding, 'target_param_types', [])}"
            )
        _validate_default_consistency(target, nodes)

        # Enforce node kinds we explicitly support today.
        allowed = {"function_definition", "declaration", "field_declaration"}
        for n in nodes:
            node_type = n.get("node_type", "")
            if node_type not in allowed:
                raise RuntimeError(
                    f"inject target '{target}' resolved unsupported node type '{node_type}'."
                )

        defaults = _merge_defaults(nodes)
        if len(defaults) > 0:
            defaults_by_binding[_binding_key(binding)] = defaults

    return defaults_by_binding


def handle(context):
    state = context.module_state.get("inject")
    if state is None:
        state = {"defaults_by_fullname": None}
        context.module_state["inject"] = state

    if state["defaults_by_fullname"] is None:
        state["defaults_by_fullname"] = _build_and_validate_defaults_map(context)

    defaults_by_binding = state["defaults_by_fullname"]
    inject_bindings = [b for b in context.bindings if _is_cppbm_inject_binding(b)]
    if len(inject_bindings) == 0:
        return

    alias = "_" + secrets.token_hex(3)
    added_any_meta = False

    for binding in inject_bindings:
        defaults = [
            pd
            for pd in defaults_by_binding.get(_binding_key(binding), [])
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

        if not hasattr(binding, "meta_args") or binding.meta_args is None:
            binding.meta_args = []
        binding.meta_args.extend(args)
        added_any_meta = True
        print(f"[inject] bind-meta {binding.target} ({len(defaults)} args)")

    if added_any_meta:
        context.generated_prefix_lines.append(f"namespace {alias} = ::cpp::blackmagic::depends;")
