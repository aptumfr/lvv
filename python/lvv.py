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
