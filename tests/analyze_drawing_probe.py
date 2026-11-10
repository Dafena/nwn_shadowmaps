"""Summarize redacted gate-1 logs; never infer that a capability gate passed.

Usage: python tests/analyze_drawing_probe.py CLIENT_LOG [--output REPORT.json]
Only structured diagnostic fields are read. Nonces, chat and unrelated logs are
never copied into reports. Coordinates are diagnostic screen positions, not ink.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import statistics

NUMBER = r"(-?\d+(?:\.\d+)?)"
POINTER = re.compile(r"\[drawing-probe\] pointer kind=(\d+) x=" + NUMBER +
                     r" y=" + NUMBER + r" t_us=(\d+)")
NATIVE = re.compile(r"\[drawing-probe\] native kind=([ODUGCB]) seq=(\d+) x=" +
                    NUMBER + r" y=" + NUMBER + r" w=" + NUMBER + r" h=" +
                    NUMBER + r" t_us=(\d+)")
SCALE = re.compile(r"\[drawing-probe\] gui_scale=" + NUMBER + r" t_us=(\d+)")
QUAD = re.compile(r"\[drawing-probe\] image_quad x=" + NUMBER + r" y=" + NUMBER +
                  r" w=" + NUMBER + r" h=" + NUMBER + r" bottom_up=([01])")
PREVIEW = re.compile(r"\[drawing-probe\] local_preview uploaded strokes=(\d+) calibrated=([01]) full=([01]) rejected=(\d+)")
WORLD = re.compile(r"\[drawing-probe\] world_canvas uploaded strokes=(\d+)")


def correlation_fit(pairs):
    if len(pairs) < 3:
        return None
    q = [[p["root"][i] + p["local"][i] for i in range(2)] for p in pairs]
    raw = [p["raw"] for p in pairs]
    mq = [statistics.mean(a[i] for a in q) for i in range(2)]
    mr = [statistics.mean(a[i] for a in raw) for i in range(2)]
    denom = sum((a[i] - mq[i]) ** 2 for a in q for i in range(2))
    if denom <= 0:
        return None
    fit = sum((q[n][i] - mq[i]) * (raw[n][i] - mr[i])
              for n in range(len(pairs)) for i in range(2)) / denom
    inset = [mr[i] - fit * mq[i] for i in range(2)]
    residuals = [fit * q[n][i] + inset[i] - raw[n][i]
                 for n in range(len(pairs)) for i in range(2)]
    return {"scale": fit, "inset_pixels": inset,
            "rms_pixels": math.sqrt(statistics.mean(r * r for r in residuals)),
            "max_pixels": max(map(abs, residuals))}


def summarize(lines):
    sessions = []
    current = None
    scale = None
    for line in lines:
        m = WORLD.search(line)
        if m and current:
            current["world_uploaded_strokes"] = int(m[1])
            current["peak_world_strokes"] = max(current["peak_world_strokes"], int(m[1]))
            continue
        if current and "[drawing-probe] world base verified;" in line:
            current["world_base_verified"] = True
            continue
        m = QUAD.search(line)
        if m and current:
            current["image_quad_measurements"].append({
                "rectangle": list(map(float, m.groups()[:4])), "bottom_up": m[5] == "1"})
            current["image_quad_measurements"] = current["image_quad_measurements"][-64:]
            continue
        m = PREVIEW.search(line)
        if m and current:
            current["local_preview"] = {"strokes": int(m[1]), "aligned": m[2] == "1",
                                        "full": m[3] == "1", "rejected": int(m[4])}
            current["peak_uploaded_strokes"] = max(current["peak_uploaded_strokes"], int(m[1]))
            continue
        m = SCALE.search(line)
        if m:
            scale = float(m[1])
            if current:
                current["scale_observations"].append(scale)
            continue
        m = NATIVE.search(line)
        if m:
            kind, sequence = m[1], int(m[2])
            x, y, w, h = map(float, m.groups()[2:6])
            timestamp = int(m[7])
            if kind == "O":
                current = {"native_events": Counter(), "raw_events": Counter(),
                           "pairs": [], "unmatched_downs": 0, "ambiguous_downs": 0,
                           "scale_observations": [] if scale is None else [scale],
                           "movement_limit_reported": False,
                           "image_quad_measurements": [], "local_preview": None,
                           "peak_uploaded_strokes": 0,
                           "world_base_verified": False, "world_uploaded_strokes": None,
                           "peak_world_strokes": 0,
                           "_raw_downs": [], "_geometry": None,
                           "_last_sequence": 0, "sequence_errors": 0}
                sessions.append(current)
            if not current:
                continue
            current["native_events"][kind] += 1
            if sequence <= current["_last_sequence"]:
                current["sequence_errors"] += 1
            current["_last_sequence"] = sequence
            if kind == "G":
                current["_geometry"] = (x, y, w, h, timestamp) if w > 0 and h > 0 else None
            elif kind == "B":
                current["_raw_downs"] = []
            elif kind == "D":
                candidates = [r for r in current["_raw_downs"]
                              if 0 <= timestamp - r[2] <= 250_000]
                # Never silently reuse a down or guess between rapid clicks.
                current["_raw_downs"] = []
                if len(candidates) > 1:
                    current["ambiguous_downs"] += 1
                elif len(candidates) == 1:
                    gx = current["_geometry"]
                    raw = candidates[0]
                    if gx and timestamp - gx[4] >= 70_000:
                        current["pairs"].append({
                            "raw": raw[:2], "local": [x, y], "root": gx[:4],
                            "lag_ms": (timestamp - raw[2]) / 1000,
                            "gui_scale": scale})
                    else:
                        current["unmatched_downs"] += 1
                else:
                    current["unmatched_downs"] += 1
            if kind == "C":
                current = None
            continue
        if current and "[drawing-probe] movement limit reached" in line:
            current["movement_limit_reported"] = True
        m = POINTER.search(line)
        if not m or not current:
            continue
        kind = int(m[1])
        current["raw_events"][str(kind)] += 1
        if kind == 1:
            current["_raw_downs"].append([float(m[2]), float(m[3]), int(m[4])])
            current["_raw_downs"] = current["_raw_downs"][-16:]

    for session in sessions:
        pairs = session["pairs"]
        # Do not pool different UI scales into one misleading fit. Coordinates
        # are compared as observed; this does not assume their unit convention.
        groups = {}
        for pair in pairs:
            groups.setdefault(pair["gui_scale"], []).append(pair)
        session["fits_by_gui_scale"] = [
            {"gui_scale": scale, "matched_downs": len(group),
             "fit": correlation_fit(group)} for scale, group in groups.items()]
        if len(groups) == 1:
            fit = correlation_fit(pairs)
            if fit:
                session["correlation_fit"] = fit
        session["matched_downs"] = len(pairs)
        session["raw_sample_limit_reached"] = sum(session["raw_events"].values()) >= 4096
        for key in list(session):
            if key.startswith("_"):
                del session[key]
    return {"sessions": sessions, "gates_complete": False,
            "limitations": [
                "Correlation is affected by movement during network/event latency.",
                "A fit does not prove clipping, occlusion, GUI scaling or authentication.",
                "Only native NUI down events establish canvas hits; raw downs alone do not."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--renderer-sha256")
    parser.add_argument("--game-build")
    parser.add_argument("--platform")
    args = parser.parse_args()
    report = summarize(args.log.read_text(encoding="utf-8", errors="replace").splitlines())
    report["artifact"] = {"platform": args.platform, "game_build": args.game_build,
                          "renderer_sha256": args.renderer_sha256}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"sessions": [{k: v for k, v in s.items() if k != "pairs"}
                                   for s in report["sessions"]],
                      "gates_complete": False}, indent=2))
