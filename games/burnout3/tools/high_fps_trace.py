#!/usr/bin/env python3
"""Find the pictures a high frame rate placed wrongly, from its trace.

    set PS2HOST_HIGH_FPS_TRACE=trace.csv      (then play, or a scripted run)
    python games/burnout3/tools/high_fps_trace.py trace.csv [--camera 0|1]

port/high_fps.cpp writes a line for each race frame above 60 pictures a
second: the ticks it ran, where the picture is between the last two (t),
and each camera's position as those two ticks left it and as placed for
the picture. Listed here, each picture where a camera's path looks wrong:

  jump    the placed position moved much further from the picture before
          than its neighbours did (a glitch seen as a one-picture lurch)
  cut     the tick's own step much longer than the ticks around it, and the
          picture blended across it - the game cut to another camera; a
          picture placed between the two shows neither
  turn    the camera's +0x20 values turned in a tick much more than in the
          ticks around it (a cut too)
  back    the placed position went backwards along the camera's way
  ticks   a frame that ran 2 or more ticks (a late picture caught up)
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys


def median_around(values: list[float], i: int, half: int = 6) -> float:
    window = [v for j, v in enumerate(values[max(0, i - half):i + half + 1], start=max(0, i - half)) if j != i]
    return statistics.median(window) if window else 0.0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace")
    parser.add_argument("--camera", type=int, default=None, help="only this camera (0: player 1, 1: player 2)")
    parser.add_argument("--factor", type=float, default=3.0, help="how many times its neighbours' a jump is")
    parser.add_argument("--limit", type=int, default=60, help="the most pictures listed")
    args = parser.parse_args()

    with open(args.trace, newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        print("no race frames in the trace")
        return 1
    cameras = sorted({int(k[1]) for k in rows[0] if k.startswith("c") and k.endswith("_x") and "_" in k})
    if args.camera is not None:
        cameras = [args.camera]
    found = []
    for c in cameras:
        def pos(row, which):
            return tuple(float(row[f"c{c}_{which}{axis}"]) for axis in ("x", "y", "z")) if which else \
                tuple(float(row[f"c{c}_{axis}"]) for axis in ("x", "y", "z"))
        placed = [pos(r, "") for r in rows]
        steps = [float(r[f"c{c}_step"]) for r in rows]
        moves = [0.0] + [math.dist(placed[i], placed[i - 1]) for i in range(1, len(placed))]
        turns = [math.degrees(math.acos(max(-1.0, min(1.0, float(r[f"c{c}_turn"]))))) for r in rows]
        # The first frames after the rate changed (the frame numbers not one
        # apart): nothing placed yet, the last tick unknown.
        fresh = {0} | {i for i in range(1, len(rows)) if int(rows[i]["frame"]) != int(rows[i - 1]["frame"]) + 1}
        fresh |= {i + 1 for i in fresh}
        if max(steps) == 0.0:
            continue                    # (a camera not in use: player 2's in a one-player race)
        for i, r in enumerate(rows):
            if i in fresh:
                continue
            reasons = []
            around = median_around(moves, i)
            if moves[i] > args.factor * around and moves[i] > 0.5:
                reasons.append(f"jump {moves[i]:.2f} (around {around:.2f})")
            # (a cut shown as it is - placed where the tick left it - is fine)
            blended = math.dist(placed[i], pos(r, "now_")) > 1e-3
            step_around = median_around(steps, i)
            if blended and steps[i] > args.factor * step_around and steps[i] > 1.0:
                reasons.append(f"cut {steps[i]:.2f} a tick (around {step_around:.2f}), blended")
            turn_around = median_around(turns, i)
            if blended and turns[i] > args.factor * turn_around and turns[i] > 5.0:
                reasons.append(f"turn {turns[i]:.0f} deg (around {turn_around:.0f}), blended")
            if i >= 2 and moves[i] > 0.05:
                way = [placed[i - 1][k] - placed[i - 2][k] for k in range(3)]
                now = [placed[i][k] - placed[i - 1][k] for k in range(3)]
                if sum(a * b for a, b in zip(way, now)) < -0.25 * math.hypot(*way) * math.hypot(*now):
                    reasons.append("back")
            if int(r["ticks"]) >= 2:
                reasons.append(f"ticks {r['ticks']}")
            if reasons:
                found.append((int(r["frame"]), c, float(r["ms"]), float(r["t"]), int(r["ticks"]), reasons))

    found.sort()
    print(f"{len(rows)} race frames, {len(found)} pictures listed")
    for frame, c, ms, t, ticks, reasons in found[:args.limit]:
        print(f"  frame {frame:7d}  camera {c}  {ms / 1000:9.3f} s  t {t:.2f}  ticks {ticks}  " + "; ".join(reasons))
    if len(found) > args.limit:
        print(f"  ... {len(found) - args.limit} more (--limit)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
