#!/usr/bin/env python3
"""
plot_ppg_data.py

Parses a log file containing multiple stages of PPG (photoplethysmography)
processing data, e.g.:

    -------------------- Read SPO2 -----------------------
    (red, ir), (ax, ay, az)
    ...
    -------------------- BANDPASS DATA --------------------
    (red, ir), (ax, ay, az)
    ...
    -------------------- NLMS DATA -------------------------
    (red, ir)
    ...

Each "-----...-----" line is a section separator. The text on that line
(with dashes stripped) becomes the section name. Every following line is a
data row containing one or two comma-separated tuples in parentheses:

    (red, ir)                      -> 2 values, no accelerometer
    (red, ir), (ax, ay, az)        -> 2 values + 3-axis accelerometer

Lines that don't match this pattern (e.g. debug prints like
"[task_oxim.c:268]Mean Red: 0, Mean IR: 0") are ignored.

All sections are drawn in ONE figure, arranged as a grid:
    - rows    = signal (Red, IR, Ax, Ay, Az) -- only rows that actually
                appear somewhere in the log are shown
    - columns = pipeline stage, in log order (e.g. Read SPO2 | BANDPASS
                DATA | NLMS DATA)
Each subplot autoscales its own y-axis to its own data, so small-amplitude
stages (e.g. raw) show their actual shape instead of being flattened by a
shared axis sized for a bigger-swing stage (e.g. bandpass).

Usage:
    python plot_ppg_data.py --logfile path/to/logfile.txt
    python plot_ppg_data.py --logfile path/to/logfile.txt --save combined_plot.png
    python plot_ppg_data.py --logfile path/to/logfile.txt --skip 0   # keep all samples
"""

import argparse
import re
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

# A section separator line looks like: ----...---- NAME ----...----
SEPARATOR_RE = re.compile(r'^-{3,}.*-{3,}$')

# Matches one or more parenthesized groups of comma-separated numbers on a line,
# e.g. "(0.04733, 0), (0.00457, -0.00503, -0.12344)"
GROUP_RE = re.compile(r'\(([^)]*)\)')

NUMBER_RE = re.compile(r'^-?\d+\.?\d*(?:[eE][+-]?\d+)?$')


def clean_section_name(line: str) -> str:
    """Strip dashes/whitespace from a separator line to get the section name."""
    return line.strip('-').strip()


def parse_data_line(line: str):
    """
    Try to parse a line into a flat list of floats: [red, ir] or
    [red, ir, ax, ay, az]. Returns None if the line doesn't match the
    expected tuple-of-numbers pattern.
    """
    groups = GROUP_RE.findall(line)
    if not groups:
        return None

    values = []
    for group in groups:
        parts = [p.strip() for p in group.split(',')]
        if not parts or parts == ['']:
            return None
        for p in parts:
            if not NUMBER_RE.match(p):
                return None
            values.append(float(p))

    # Expect either 2 values (red, ir) or 5 values (red, ir, ax, ay, az)
    if len(values) not in (2, 5):
        return None
    return values


def parse_log(filepath: Path) -> dict:
    """
    Parse the log file into a dict: {section_name: DataFrame}.
    DataFrame columns are ['red', 'ir'] or ['red', 'ir', 'ax', 'ay', 'az'].
    """
    sections = {}
    current_name = None
    current_rows = []

    def flush():
        if current_name is not None and current_rows:
            ncols = len(current_rows[0])
            cols = ['red', 'ir'] if ncols == 2 else ['red', 'ir', 'ax', 'ay', 'az']
            # Keep only rows matching the dominant column count for this section
            rows = [r for r in current_rows if len(r) == ncols]
            sections[current_name] = pd.DataFrame(rows, columns=cols)

    with open(filepath, 'r') as f:
        for raw_line in f:
            line = raw_line.rstrip('\n')
            if not line.strip():
                continue

            if SEPARATOR_RE.match(line.strip()):
                flush()
                current_name = clean_section_name(line.strip())
                current_rows = []
                continue

            values = parse_data_line(line)
            if values is not None:
                current_rows.append(values)
            # else: ignore non-data lines (debug prints, etc.)

    flush()
    return sections


METRIC_ORDER = ['red', 'ir', 'ax', 'ay', 'az']
METRIC_COLOR = {
    'red': 'tab:red',
    'ir': 'tab:blue',
    'ax': 'tab:green',
    'ay': 'tab:orange',
    'az': 'tab:purple',
}


def plot_all_sections(sections: dict, save_path: Path | None, skip_samples: int = 0):
    """
    Single figure, grid layout:
        rows    = metrics (red, ir, ax, ay, az) -- only rows that appear
                  in at least one section are shown
        columns = sections, in the order they appeared in the log
                  (e.g. Read SPO2 | BANDPASS DATA | NLMS DATA)

    Each subplot autoscales to its own data (with small padding), so a
    flat-looking raw signal with tiny variation still shows its actual
    shape instead of being squashed by a shared axis sized for a bigger
    stage.

    skip_samples: drop this many leading rows from every section before
    plotting (e.g. to hide a startup/warm-up transient). Original sample
    indices are kept on the x-axis so the remaining data still lines up
    with, e.g., NLMS's own warm-up region.
    """
    if skip_samples > 0:
        sections = {
            name: df.iloc[skip_samples:] for name, df in sections.items()
        }

    section_names = list(sections.keys())
    # Only include metric rows that exist in at least one section
    metrics_present = [m for m in METRIC_ORDER if any(m in df.columns for df in sections.values())]

    n_rows = len(metrics_present)
    n_cols = len(section_names)

    fig, axes = plt.subplots(
        n_rows, n_cols,
        figsize=(4.5 * n_cols, 2.0 * n_rows),
        squeeze=False,
    )

    # Column headers (section names)
    for col, name in enumerate(section_names):
        axes[0, col].set_title(name, fontsize=11, fontweight='bold')

    for row, metric in enumerate(metrics_present):
        for col, name in enumerate(section_names):
            ax_plot = axes[row, col]
            df = sections[name]
            if metric in df.columns:
                ax_plot.plot(df.index, df[metric], color=METRIC_COLOR[metric], linewidth=1)
                # Autoscale y to this subplot's own data with a little padding,
                # so small-amplitude columns (e.g. raw) don't get flattened by
                # a shared axis sized for a bigger-swing column (e.g. bandpass).
                y = df[metric].to_numpy()
                y_min, y_max = y.min(), y.max()
                span = y_max - y_min
                pad = span * 0.1 if span > 0 else max(abs(y_min), 1e-6) * 0.1
                ax_plot.set_ylim(y_min - pad, y_max + pad)
            else:
                ax_plot.text(0.5, 0.5, 'n/a', ha='center', va='center',
                             transform=ax_plot.transAxes, color='gray', fontsize=9)
                ax_plot.set_xticks([])
                ax_plot.set_yticks([])

            ax_plot.grid(True, alpha=0.3)
            if col == 0:
                ax_plot.set_ylabel(metric.upper(), fontsize=10, fontweight='bold')
            if row == n_rows - 1:
                ax_plot.set_xlabel('Sample index', fontsize=9)

    fig.suptitle('PPG Pipeline: Raw -> Bandpass -> NLMS (rows = signal, columns = stage)',
                 fontsize=13, fontweight='bold')
    fig.tight_layout(rect=(0, 0, 1, 0.95))

    if save_path is not None:
        save_path.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(save_path, dpi=150)
        print(f'Saved: {save_path}')


def main():
    parser = argparse.ArgumentParser(description='Parse and plot staged PPG log data.')
    parser.add_argument('--logfile', type=str, default='log.txt',
                         help='Path to the log text file (default: log.txt)')
    parser.add_argument('--save', type=str, default=None,
                         help='Path to save the combined PNG plot to (default: just show it)')
    parser.add_argument('--skip', type=int, default=10,
                         help='Number of leading samples to drop from every section before '
                              'plotting, e.g. to hide a startup transient (default: 10)')
    args = parser.parse_args()

    filepath = Path(args.logfile)
    if not filepath.exists():
        print(f'File not found: {filepath}', file=sys.stderr)
        sys.exit(1)

    sections = parse_log(filepath)
    if not sections:
        print('No parseable data sections found.', file=sys.stderr)
        sys.exit(1)

    for name, df in sections.items():
        print(f'Section "{name}": {len(df)} rows, columns={list(df.columns)}')

    save_path = Path(args.save) if args.save else None
    plot_all_sections(sections, save_path, skip_samples=args.skip)

    if save_path is None:
        plt.show()


if __name__ == '__main__':
    main()
