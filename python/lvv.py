"""
LVV Python client module.

Provides the same API as the embedded PocketPy lvv module, but communicates
with a running lvv server over HTTP. Used by `lvv run --python`.

The server URL is read from the LVV_URL environment variable
(set automatically by `lvv run --python`).
"""

import json
import os
import re
import time
from urllib.request import urlopen, Request
from urllib.error import URLError, HTTPError

_BASE_URL = os.environ.get("LVV_URL", "http://127.0.0.1:8080")
_object_map = {}


def _resolve(name):
    return _object_map.get(name, name)


def _get(path):
    try:
        with urlopen(f"{_BASE_URL}{path}", timeout=30) as r:
            return json.loads(r.read())
    except HTTPError as e:
        body = e.read().decode()
        try:
            err = json.loads(body)
            if "error" in err:
                raise RuntimeError(f"{path}: {err['error']}")
            return err
        except json.JSONDecodeError:
            pass
        raise RuntimeError(f"{path}: HTTP {e.code} - {body[:200]}")


def _post(path, data=None):
    body = json.dumps(data or {}).encode()
    req = Request(f"{_BASE_URL}{path}", data=body,
                  headers={"Content-Type": "application/json"})
    try:
        with urlopen(req, timeout=30) as r:
            return json.loads(r.read())
    except HTTPError as e:
        resp = e.read().decode()
        try:
            err = json.loads(resp)
            if "error" in err:
                raise RuntimeError(f"{path}: {err['error']}")
        except json.JSONDecodeError:
            pass
        raise RuntimeError(f"{path}: HTTP {e.code} - {resp[:200]}")
    except URLError as e:
        raise RuntimeError(f"{path}: {e.reason}")


def _get_binary(path):
    with urlopen(f"{_BASE_URL}{path}", timeout=30) as r:
        return r.read()


# ---- Connection ----

def ping():
    return _get("/api/ping")["version"]


def screen_info():
    return json.dumps(_get("/api/screen-info"))


# ---- Input ----

def click(name):
    return _post("/api/click", {"name": _resolve(name)}).get("success", False)


def click_at(x, y):
    return _post("/api/click", {"x": x, "y": y}).get("success", False)


def press(x, y):
    return _post("/api/press", {"x": x, "y": y}).get("success", False)


def release():
    return _post("/api/release").get("success", False)


def move_to(x, y):
    return _post("/api/move", {"x": x, "y": y}).get("success", False)


def swipe(x1, y1, x2, y2, duration):
    return _post("/api/swipe", {
        "x": x1, "y": y1, "x_end": x2, "y_end": y2, "duration": duration
    }).get("success", False)


def long_press(x, y, duration):
    return _post("/api/long-press", {"x": x, "y": y, "duration": duration}).get("success", False)


def drag(x1, y1, x2, y2, duration):
    return _post("/api/drag", {
        "x": x1, "y": y1, "x_end": x2, "y_end": y2, "duration": duration
    }).get("success", False)


def type_text(text):
    return _post("/api/type", {"text": text}).get("success", False)


def key(code):
    return _post("/api/key", {"key": code}).get("success", False)


# ---- Inspection ----

def find(name):
    resp = _get(f"/api/find?name={_resolve(name)}")
    if not resp.get("found", False):
        return None
    # Remove the 'found' key and return as JSON string (matches PocketPy behavior)
    resp.pop("found", None)
    return json.dumps(resp)


def find_at(x, y):
    resp = _post("/api/find-at", {"x": x, "y": y})
    if not resp.get("found", False):
        return None
    resp.pop("found", None)
    resp.pop("selector", None)
    return json.dumps(resp)


def find_by(selector):
    resp = _post("/api/find-by", {"selector": selector})
    if not resp.get("found", False):
        return None
    resp.pop("found", None)
    return json.dumps(resp)


def find_all_by(selector):
    resp = _post("/api/find-by", {"selector": selector, "all": True})
    return json.dumps(resp.get("widgets", []))


def get_tree():
    return json.dumps(_get("/api/tree"))


def get_props(name):
    return json.dumps(_get(f"/api/widget/{_resolve(name)}"))


def widget_coords(name):
    """Return (x, y, width, height) for a widget, or raise if not found."""
    name = _resolve(name)
    resp = _get(f"/api/find?name={name}")
    if not resp.get("found", False):
        raise RuntimeError(f"Widget '{name}' not found")
    return (resp["x"], resp["y"], resp["width"], resp["height"])


def get_all_widgets():
    return json.dumps(_get("/api/widgets"))


# ---- Waiting ----

def wait(ms):
    time.sleep(ms / 1000.0)


def wait_for(name_or_selector, timeout_ms):
    deadline = time.monotonic() + timeout_ms / 1000.0
    is_selector = "=" in name_or_selector

    while True:
        try:
            if is_selector:
                w = find_by(name_or_selector)
            else:
                w = find(_resolve(name_or_selector))
            if w is not None:
                info = json.loads(w)
                if info.get("visible", False):
                    return True
        except Exception:
            pass

        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"Timed out waiting for widget '{name_or_selector}' ({timeout_ms}ms)")
        time.sleep(0.1)


def wait_until(name, prop, expected, timeout_ms):
    deadline = time.monotonic() + timeout_ms / 1000.0
    name = _resolve(name)
    last_value = ""

    while True:
        try:
            props = json.loads(get_props(name))
            if prop in props:
                val = props[prop]
                if isinstance(val, str):
                    last_value = val
                else:
                    last_value = json.dumps(val)
                if last_value == expected:
                    return True
        except Exception:
            pass

        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"Timed out waiting for '{name}'.{prop} == '{expected}' "
                f"(last: '{last_value}', {timeout_ms}ms)")
        time.sleep(0.1)


def find_with_retry(name_or_selector, timeout_ms):
    deadline = time.monotonic() + timeout_ms / 1000.0
    is_selector = "=" in name_or_selector

    while True:
        try:
            if is_selector:
                w = find_by(name_or_selector)
            else:
                w = find(_resolve(name_or_selector))
            if w is not None:
                return w
        except Exception:
            pass

        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"Timed out waiting for '{name_or_selector}' ({timeout_ms}ms)")
        time.sleep(0.1)


# ---- Assertions ----

def assert_visible(name):
    name = _resolve(name)
    w = find(name)
    if w is None:
        raise AssertionError(f"Widget '{name}' is not visible")
    info = json.loads(w)
    if not info.get("visible", False):
        raise AssertionError(f"Widget '{name}' is not visible")
    return True


def assert_hidden(name):
    name = _resolve(name)
    w = find(name)
    if w is not None:
        info = json.loads(w)
        if info.get("visible", False):
            raise AssertionError(f"Widget '{name}' is visible (expected hidden)")
    return True


def assert_value(name, prop, expected):
    name = _resolve(name)
    props = json.loads(get_props(name))
    if prop not in props:
        raise RuntimeError(f"Widget '{name}' has no property '{prop}'")
    val = props[prop]
    actual = val if isinstance(val, str) else json.dumps(val)
    if actual != expected:
        raise AssertionError(
            f"Widget '{name}' property '{prop}': expected '{expected}', got '{actual}'")
    return True


def assert_range(name, prop, min_val, max_val):
    name = _resolve(name)
    props = json.loads(get_props(name))
    if prop not in props:
        raise RuntimeError(f"Widget '{name}' has no property '{prop}'")
    try:
        actual = float(props[prop])
    except (TypeError, ValueError):
        raise AssertionError(
            f"Widget '{name}' property '{prop}': '{props[prop]}' is not a number")
    if actual < min_val or actual > max_val:
        raise AssertionError(
            f"Widget '{name}' property '{prop}': {actual} not in range [{min_val}, {max_val}]")
    return True


def assert_match(name, prop, pattern):
    name = _resolve(name)
    props = json.loads(get_props(name))
    if prop not in props:
        raise RuntimeError(f"Widget '{name}' has no property '{prop}'")
    val = str(props[prop])
    if not re.search(pattern, val):
        raise AssertionError(
            f"Widget '{name}' property '{prop}': '{val}' does not match /{pattern}/")
    return True


def assert_true(name, prop):
    name = _resolve(name)
    props = json.loads(get_props(name))
    if prop not in props:
        raise RuntimeError(f"Widget '{name}' has no property '{prop}'")
    val = str(props[prop])
    if val not in ("true", "1", "True"):
        raise AssertionError(
            f"Widget '{name}' property '{prop}': expected true, got '{val}'")
    return True


def assert_false(name, prop):
    name = _resolve(name)
    props = json.loads(get_props(name))
    if prop not in props:
        raise RuntimeError(f"Widget '{name}' has no property '{prop}'")
    val = str(props[prop])
    if val not in ("false", "0", "False"):
        raise AssertionError(
            f"Widget '{name}' property '{prop}': expected false, got '{val}'")
    return True


# ---- Tree snapshots ----

def _normalize_node(node):
    """Strip volatile fields, keep structural properties."""
    j = {
        "type": node.get("type", ""),
        "name": node.get("name", ""),
        "text": node.get("text", ""),
        "visible": node.get("visible", False),
        "clickable": node.get("clickable", False),
    }
    children = node.get("children", [])
    if children:
        j["children"] = [_normalize_node(c) for c in children]
    return j


def _diff_trees(expected, actual, path=""):
    """Compare two normalized trees, return list of difference strings."""
    diffs = []
    label = path or (expected.get("name") or expected.get("type", "?"))

    for prop in ("type", "name", "text"):
        ev = expected.get(prop, "")
        av = actual.get(prop, "")
        if ev != av:
            diffs.append(f"{label}: property '{prop}' changed: '{ev}' -> '{av}'")

    for prop in ("visible", "clickable"):
        ev = expected.get(prop, False)
        av = actual.get(prop, False)
        if ev != av:
            diffs.append(f"{label}: property '{prop}' changed: {ev} -> {av}")

    ec = expected.get("children", [])
    ac = actual.get("children", [])

    # Match by name when possible, fall back to index for unnamed
    actual_by_name = {}
    for i, c in enumerate(ac):
        n = c.get("name", "")
        if n:
            actual_by_name[n] = i
    actual_matched = [False] * len(ac)

    for i, e_child in enumerate(ec):
        e_name = e_child.get("name", "")
        child_label = f"{label}/{e_name or e_child.get('type', '?')}"

        if e_name and e_name in actual_by_name:
            j = actual_by_name[e_name]
            actual_matched[j] = True
            diffs.extend(_diff_trees(e_child, ac[j], child_label))
        elif i < len(ac) and not actual_matched[i]:
            actual_matched[i] = True
            diffs.extend(_diff_trees(e_child, ac[i], child_label))
        else:
            diffs.append(f"{label}: missing child '{e_name or e_child.get('type', '?')}'")

    for i, a_child in enumerate(ac):
        if not actual_matched[i]:
            n = a_child.get("name") or a_child.get("type", "?")
            diffs.append(f"{label}: extra child '{n}'")

    return diffs


def save_tree(path):
    """Save normalized widget tree to a JSON file."""
    tree = json.loads(get_tree())
    normalized = _normalize_node(tree)
    with open(path, "w") as f:
        json.dump(normalized, f, indent=2)
    return True


def _normalize_node_ex(node, include_geometry=False):
    """Normalize with optional geometry fields."""
    j = {
        "type": node.get("type", ""),
        "name": node.get("name", ""),
        "text": node.get("text", ""),
        "visible": node.get("visible", False),
        "clickable": node.get("clickable", False),
    }
    if include_geometry:
        for k in ("x", "y", "width", "height"):
            j[k] = node.get(k, 0)
    children = node.get("children", [])
    if children:
        j["children"] = [_normalize_node_ex(c, include_geometry) for c in children]
    return j


def _find_subtree(node, name):
    """Find a named widget in the tree."""
    if node.get("name") == name:
        return node
    for child in node.get("children", []):
        found = _find_subtree(child, name)
        if found:
            return found
    return None


def _diff_trees_ex(expected, actual, path="", geometry_tolerance=0):
    """Compare trees with optional geometry tolerance."""
    diffs = []
    label = path or (expected.get("name") or expected.get("type", "?"))

    for prop in ("type", "name", "text"):
        ev = expected.get(prop, "")
        av = actual.get(prop, "")
        if ev != av:
            diffs.append(f"{label}: property '{prop}' changed: '{ev}' -> '{av}'")

    for prop in ("visible", "clickable"):
        ev = expected.get(prop, False)
        av = actual.get(prop, False)
        if ev != av:
            diffs.append(f"{label}: property '{prop}' changed: {ev} -> {av}")

    for prop in ("x", "y", "width", "height"):
        if prop not in expected:
            continue
        ev = expected.get(prop, 0)
        av = actual.get(prop, 0)
        if abs(ev - av) > geometry_tolerance:
            msg = f"{label}: geometry '{prop}' changed: {ev} -> {av}"
            if geometry_tolerance > 0:
                msg += f" (tolerance: {geometry_tolerance})"
            diffs.append(msg)

    ec = expected.get("children", [])
    ac = actual.get("children", [])

    actual_by_name = {}
    for i, c in enumerate(ac):
        n = c.get("name", "")
        if n:
            actual_by_name[n] = i
    actual_matched = [False] * len(ac)

    for i, e_child in enumerate(ec):
        e_name = e_child.get("name", "")
        child_label = f"{label}/{e_name or e_child.get('type', '?')}"

        if e_name and e_name in actual_by_name:
            j = actual_by_name[e_name]
            actual_matched[j] = True
            diffs.extend(_diff_trees_ex(e_child, ac[j], child_label, geometry_tolerance))
        elif i < len(ac) and not actual_matched[i]:
            actual_matched[i] = True
            diffs.extend(_diff_trees_ex(e_child, ac[i], child_label, geometry_tolerance))
        else:
            diffs.append(f"{label}: missing child '{e_name or e_child.get('type', '?')}'")

    for i, a_child in enumerate(ac):
        if not actual_matched[i]:
            n = a_child.get("name") or a_child.get("type", "?")
            diffs.append(f"{label}: extra child '{n}'")

    return diffs


def assert_tree(ref_path, root="", include_geometry=False, tolerance=0):
    """Compare widget tree against a reference JSON file.
    On first run (no reference), saves the current tree and passes.

    Args:
        ref_path: Path to reference JSON file
        root: Widget name for subtree ("" = full tree)
        include_geometry: 1 to include x/y/width/height, 0 to skip
        tolerance: Pixel tolerance for geometry comparison
    """
    if tolerance < 0:
        raise ValueError("geometry tolerance must be >= 0")

    if ref_path and ref_path[0] != '/':
        ref_dir = os.environ.get("LVV_REF_IMAGES", "ref_images")
        ref_path = os.path.join(ref_dir, ref_path)

    tree = json.loads(get_tree())

    # Find subtree if root specified
    node = tree
    if root:
        node = _find_subtree(tree, root)
        if not node:
            raise RuntimeError(f"Subtree root '{root}' not found")

    actual = _normalize_node_ex(node, include_geometry)

    try:
        with open(ref_path) as f:
            expected = json.load(f)
    except FileNotFoundError:
        os.makedirs(os.path.dirname(ref_path) or ".", exist_ok=True)
        with open(ref_path, "w") as f:
            json.dump(actual, f, indent=2)
        return True

    diffs = _diff_trees_ex(expected, actual, geometry_tolerance=tolerance)
    if not diffs:
        return True
    msg = f"Tree structure mismatch ({len(diffs)} differences):\n"
    for d in diffs:
        msg += f"  {d}\n"
    raise AssertionError(msg)


# ---- Screenshots ----

def screenshot(path):
    data = _get_binary("/api/screenshot")
    with open(path, "wb") as f:
        f.write(data)
    return True


def screenshot_compare(ref_path, threshold):
    resp = _post("/api/visual/compare", {
        "reference": ref_path,
        "threshold": threshold,
    })
    return resp.get("passed", False)


def screenshot_compare_ex(ref_path, threshold, ignore_json):
    regions = json.loads(ignore_json) if isinstance(ignore_json, str) else ignore_json
    ignore_regions = [{"x": r[0], "y": r[1], "width": r[2], "height": r[3]} for r in regions]
    resp = _post("/api/visual/compare", {
        "reference": ref_path,
        "threshold": threshold,
        "ignore_regions": ignore_regions,
    })
    return resp.get("passed", False)


# ---- Log capture ----

def set_log_capture(enable):
    return _post("/api/logs/capture", {"enable": enable}).get("success", False)


def get_logs():
    return json.dumps(_get("/api/logs"))


def clear_logs():
    return _post("/api/logs/clear").get("success", False)


# ---- Metrics ----

def get_metrics():
    return json.dumps(_get("/api/metrics"))


# ---- Object map ----

def load_object_map(path):
    global _object_map
    with open(path) as f:
        _object_map = json.load(f)
    return len(_object_map)


# PocketPy compatibility: AssertionError (their spelling)
AssertionError = AssertionError
