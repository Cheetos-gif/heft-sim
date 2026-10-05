#!/usr/bin/env python3
"""analysis/plots.py

Predicted-vs-measured error% bar chart, grouped by DAG and colored by
scheduler, from out/results.csv (benchmark_suite.cpp's output). Writes
plain SVG by hand -- this environment has no pip/network access to
install matplotlib, and a hand-rolled SVG writer needs nothing beyond
the standard library. Output: analysis/out/error_pct.svg (open directly
in a browser or any SVG-capable viewer).

Usage: python3 analysis/plots.py [path/to/results.csv] [path/to/output.svg]
"""

from __future__ import annotations

import sys
from pathlib import Path

from common import load_results

SCHED_COLOR = {
    "naive": "#999999",
    "eheft": "#4c72b0",
    "cheft": "#c44e52",
}


def svg_bar_chart(rows, width: int = 900, height: int = 440) -> str:
    margin_left, margin_bottom, margin_top = 50, 90, 40
    plot_w = width - margin_left - 20
    plot_h = height - margin_bottom - margin_top

    dag_ids = sorted({r.dag_id for r in rows})
    schedulers = sorted({r.scheduler for r in rows}, key=lambda s: {"naive": 0, "eheft": 1, "cheft": 2}.get(s, 9))

    max_abs_err = max((abs(r.error_pct) for r in rows), default=1.0) or 1.0
    zero_y = margin_top + plot_h / 2
    half_h = plot_h / 2 - 15

    group_w = plot_w / max(len(dag_ids), 1)
    bar_w = group_w / (len(schedulers) + 1)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'font-family="monospace" font-size="11">',
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="white"/>',
        f'<text x="10" y="20" font-size="14">predicted-vs-measured error% '
        f'(0 = perfect prediction; bars above the line overpredict, below underpredict)</text>',
        f'<line x1="{margin_left}" y1="{zero_y}" x2="{width - 20}" y2="{zero_y}" stroke="black"/>',
    ]

    for gi, dag_id in enumerate(dag_ids):
        gx = margin_left + gi * group_w
        parts.append(
            f'<text x="{gx + group_w / 2:.1f}" y="{height - margin_bottom + 20}" text-anchor="middle">{dag_id}</text>'
        )
        for si, sched in enumerate(schedulers):
            match = [r for r in rows if r.dag_id == dag_id and r.scheduler == sched]
            if not match:
                continue
            err = match[0].error_pct
            bar_h = abs(err) / max_abs_err * half_h
            bx = gx + (si + 0.5) * bar_w
            by = zero_y - bar_h if err >= 0 else zero_y
            color = SCHED_COLOR.get(sched, "#333333")
            parts.append(f'<rect x="{bx:.1f}" y="{by:.1f}" width="{bar_w * 0.8:.1f}" height="{bar_h:.1f}" '
                         f'fill="{color}"/>')
            label_y = (by - 3) if err >= 0 else (by + bar_h + 12)
            parts.append(
                f'<text x="{bx + bar_w * 0.4:.1f}" y="{label_y:.1f}" text-anchor="middle" font-size="9">'
                f'{err:.0f}%</text>'
            )

    legend_y = height - 15
    legend_x = margin_left
    for i, sched in enumerate(schedulers):
        lx = legend_x + i * 90
        parts.append(
            f'<rect x="{lx}" y="{legend_y - 10}" width="12" height="12" fill="{SCHED_COLOR.get(sched, "#333")}"/>'
        )
        parts.append(f'<text x="{lx + 16}" y="{legend_y}">{sched}</text>')

    parts.append("</svg>")
    return "\n".join(parts)


def main() -> None:
    in_path = sys.argv[1] if len(sys.argv) > 1 else "out/results.csv"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "analysis/out/error_pct.svg"

    try:
        rows = load_results(in_path)
    except FileNotFoundError:
        print(f"{in_path} does not exist -- run benchmark_suite first")
        return

    if not rows:
        print(f"no rows in {in_path}")
        return

    svg = svg_bar_chart(rows)
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(svg)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
