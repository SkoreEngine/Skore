#!/usr/bin/env python3
"""
ui_vision_assert.py — grok-vision grader for UI widget frames (APX-251).

Takes a rendered widget PNG plus a per-widget rubric and asks grok vision to
confirm fine-grained details. Prints a single JSON object to stdout:

  {"pass": true|false, "reason": "..."}

Exit codes:
  0  graded (pass or fail — inspect JSON)
  2  skipped (no credentials / backend unavailable)
  1  hard error (bad args, I/O, API failure)

Auth (first match wins):
  1. env XAI_API_KEY or SK_UI_VISION_API_KEY
  2. env SK_UI_VISION_AUTH_JSON → JSON file with {"key": "..."} or grok auth map
  3. ~/.grok/auth.json (agent environments)

Mock / replay (no network):
  SK_UI_VISION_MOCK_RESPONSE='{"pass":true,"reason":"mock"}'
  or --mock-response PATH

Env:
  SK_UI_VISION_MODEL   default grok-4.5
  SK_UI_VISION_API_URL default https://api.x.ai/v1/chat/completions
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Optional, Tuple


DEFAULT_MODEL = os.environ.get("SK_UI_VISION_MODEL", "grok-4.5")
DEFAULT_API_URL = os.environ.get(
    "SK_UI_VISION_API_URL", "https://api.x.ai/v1/chat/completions"
)


def eprint(*args: object) -> None:
    print(*args, file=sys.stderr)


def load_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def resolve_api_key() -> Optional[str]:
    for env in ("XAI_API_KEY", "SK_UI_VISION_API_KEY"):
        v = os.environ.get(env)
        if v:
            return v.strip()

    auth_path = os.environ.get("SK_UI_VISION_AUTH_JSON")
    candidates = []
    if auth_path:
        candidates.append(Path(auth_path))
    candidates.append(Path.home() / ".grok" / "auth.json")

    for p in candidates:
        if not p.is_file():
            continue
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if isinstance(data, dict):
            if "key" in data and isinstance(data["key"], str) and data["key"]:
                return data["key"]
            # grok CLI auth map: { "https://auth.x.ai::...": { "key": "..." } }
            for v in data.values():
                if isinstance(v, dict) and isinstance(v.get("key"), str) and v["key"]:
                    return v["key"]
    return None


def build_prompt(rubric: str, family: str, state_hint: str) -> str:
    parts = [
        "You are a strict visual QA judge for a C game-engine UI toolkit.",
        "Grade the attached screenshot against the rubric. Be precise about",
        "fine-grained mark styles (X vs checkmark vs filled square, handle vs bar).",
        "Do NOT pass on a vague 'looks fine' — apply every checklist item.",
        "",
        f"Widget family: {family or '(see rubric)'}",
    ]
    if state_hint:
        parts.append(f"Claimed state / scene hint: {state_hint}")
    parts.extend(
        [
            "",
            "=== RUBRIC ===",
            rubric.strip(),
            "=== END RUBRIC ===",
            "",
            "Respond with ONLY compact JSON (no markdown fences):",
            '{"pass": true|false, "reason": "<one short sentence naming the deciding detail>"}',
        ]
    )
    return "\n".join(parts)


def parse_model_json(text: str) -> dict[str, Any]:
    text = text.strip()
    # Strip optional ```json fences
    if text.startswith("```"):
        text = re.sub(r"^```(?:json)?\s*", "", text)
        text = re.sub(r"\s*```$", "", text)
    # Find first JSON object
    m = re.search(r"\{.*\}", text, flags=re.DOTALL)
    if not m:
        raise ValueError(f"model response is not JSON: {text[:200]!r}")
    obj = json.loads(m.group(0))
    if "pass" not in obj:
        raise ValueError(f"JSON missing 'pass': {obj!r}")
    passed = obj["pass"]
    if isinstance(passed, str):
        passed = passed.strip().lower() in ("1", "true", "yes", "pass")
    else:
        passed = bool(passed)
    reason = obj.get("reason") or obj.get("message") or ""
    if not isinstance(reason, str):
        reason = str(reason)
    return {"pass": passed, "reason": reason.strip() or "(no reason)"}


def grade_with_api(
    image_path: Path, prompt: str, api_key: str, model: str, api_url: str
) -> dict[str, Any]:
    b64 = base64.b64encode(image_path.read_bytes()).decode("ascii")
    mime = "image/png"
    if image_path.suffix.lower() in (".jpg", ".jpeg"):
        mime = "image/jpeg"
    body = {
        "model": model,
        "temperature": 0,
        "messages": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "image_url",
                        "image_url": {"url": f"data:{mime};base64,{b64}"},
                    },
                    {"type": "text", "text": prompt},
                ],
            }
        ],
    }
    req = urllib.request.Request(
        api_url,
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "User-Agent": "skore-ui-vision-assert/APX-251",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        err_body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"vision API HTTP {exc.code}: {err_body[:500]}") from exc

    try:
        content = data["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError) as exc:
        raise RuntimeError(f"unexpected API response: {data!r}") from exc
    if not isinstance(content, str):
        # Some models return content as a list of parts
        if isinstance(content, list):
            content = "".join(
                p.get("text", "") if isinstance(p, dict) else str(p) for p in content
            )
        else:
            content = str(content)
    return parse_model_json(content)


def load_mock_response(path: Optional[str]) -> Optional[dict[str, Any]]:
    env = os.environ.get("SK_UI_VISION_MOCK_RESPONSE")
    if env:
        return parse_model_json(env)
    if path:
        return parse_model_json(load_text(Path(path)))
    return None


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Grade a UI widget frame with grok vision")
    ap.add_argument("--image", required=True, help="Path to rendered PNG/JPEG frame")
    ap.add_argument("--rubric-file", help="Path to rubric text file")
    ap.add_argument("--rubric-text", help="Inline rubric text (overrides file if both set)")
    ap.add_argument("--family", default="", help="Widget family name for the prompt")
    ap.add_argument("--state", default="", help="Claimed state hint (checked, disabled, ...)")
    ap.add_argument("--out", help="Optional path to write the JSON result")
    ap.add_argument("--mock-response", help="Use this JSON file instead of calling the API")
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--api-url", default=DEFAULT_API_URL)
    args = ap.parse_args(argv)

    image_path = Path(args.image)
    if not image_path.is_file():
        eprint(f"image not found: {image_path}")
        return 1

    if args.rubric_text:
        rubric = args.rubric_text
    elif args.rubric_file:
        rf = Path(args.rubric_file)
        if not rf.is_file():
            eprint(f"rubric file not found: {rf}")
            return 1
        rubric = load_text(rf)
    else:
        eprint("need --rubric-file or --rubric-text")
        return 1

    if not rubric.strip():
        eprint("rubric is empty")
        return 1

    mock = load_mock_response(args.mock_response)
    if mock is not None:
        result = mock
    else:
        api_key = resolve_api_key()
        if not api_key:
            eprint(
                "ui_vision_assert: no API key "
                "(set XAI_API_KEY / SK_UI_VISION_API_KEY or provide --mock-response); skipping"
            )
            # Structured skip so the C helper can treat as SKIPPED
            result = {
                "pass": False,
                "reason": "vision backend skipped: no API credentials",
                "skipped": True,
            }
            payload = json.dumps(result, ensure_ascii=False)
            print(payload)
            if args.out:
                Path(args.out).write_text(payload + "\n", encoding="utf-8")
            return 2

        prompt = build_prompt(rubric, args.family, args.state)
        try:
            result = grade_with_api(
                image_path, prompt, api_key, args.model, args.api_url
            )
        except Exception as exc:  # noqa: BLE001 — surface as structured error
            eprint(f"ui_vision_assert: {exc}")
            return 1

    payload = json.dumps(result, ensure_ascii=False)
    print(payload)
    if args.out:
        Path(args.out).write_text(payload + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
