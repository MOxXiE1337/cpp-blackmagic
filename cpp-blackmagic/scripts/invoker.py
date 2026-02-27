"""
invoker module:
- append a default-call lambda invoker into binding.meta_args
- binder decides whether to consume this metadata
"""

import re


def _norm(s: str) -> str:
    return re.sub(r"\s+", "", s or "")


def _select_nodes(nodes, binding):
    count = int(getattr(binding, "target_param_count", 0))
    sig = tuple(_norm(t) for t in getattr(binding, "target_param_types", []))
    out = []
    for n in nodes:
        if int(n.get("param_count", -1)) != count:
            continue
        nsig = tuple(_norm(t) for t in n.get("param_types", []))
        if nsig == sig:
            out.append(n)
    return out


def _all_defaulted(nodes, param_count: int) -> bool:
    if param_count == 0:
        return True
    seen = set()
    for n in nodes:
        for d in n.get("param_defaults", []):
            seen.add(int(d["index"]))
    return all(i in seen for i in range(param_count))


def _is_member_like(binding) -> bool:
    target = (getattr(binding, "target", "") or "").strip().lstrip(":")
    ns = (getattr(binding, "namespace_scope", "") or "").strip()
    if ns and target.startswith(ns + "::"):
        target = target[len(ns) + 2:]
    return "::" in target


def _qualified(name: str) -> str:
    name = (name or "").strip()
    return name if name.startswith("::") else f"::{name}"


def handle(context):
    by_name = {}
    for n in context.cpp_function_like_nodes:
        by_name.setdefault(n["fullname"], []).append(n)

    for b in context.bindings:
        if _is_member_like(b):
            continue
        nodes = _select_nodes(by_name.get(b.target, []), b)
        if not nodes:
            continue
        if not _all_defaulted(nodes, int(getattr(b, "target_param_count", 0))):
            continue
        if not hasattr(b, "meta_args") or b.meta_args is None:
            b.meta_args = []
        b.meta_args.append(f"[]() {{ return {_qualified(b.target)}(); }}")
        print(f"[invoker] bind-invoker {b.target}")

