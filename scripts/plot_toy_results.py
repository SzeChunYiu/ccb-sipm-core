#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def save(fig, out: Path, plot_id: str, slug: str, source: pd.DataFrame,
         title: str, records: list[dict]) -> None:
    fig_path = out / "plots" / f"{plot_id}_{slug}.png"
    table_path = out / "plot_sources" / f"{plot_id}_source.csv"
    fig.tight_layout()
    fig.savefig(fig_path, dpi=180)
    plt.close(fig)
    source.to_csv(table_path, index=False)
    records.append({
        "plot_id": plot_id,
        "title": title,
        "status": "SYNTHETIC_DEMO_ONLY",
        "figure": str(fig_path.relative_to(out)),
        "source": str(table_path.relative_to(out)),
        "figure_sha256": sha256(fig_path),
        "source_sha256": sha256(table_path),
    })


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "plots").mkdir(exist_ok=True)
    (args.output / "plot_sources").mkdir(exist_ok=True)

    events = pd.read_csv(args.input / "events.csv")
    aval = pd.read_csv(args.input / "avalanches.csv")
    wave = pd.read_csv(args.input / "waveforms.csv")
    records: list[dict] = []

    title = "SYNTHETIC DEMO — mean signal chain"
    means = pd.DataFrame({
        "stage": ["incident photons", "primary candidates", "all avalanches",
                  "unique cells"],
        "mean": [events["n_incident"].mean(),
                 events["n_primary_candidates"].mean(),
                 events["n_avalanches"].mean(),
                 events["n_unique_cells"].mean()],
    })
    fig, ax = plt.subplots()
    ax.bar(means["stage"], means["mean"])
    ax.set_ylabel("Mean per event")
    ax.set_title(title)
    ax.tick_params(axis="x", rotation=20)
    save(fig, args.output, "P001", "signal_chain", means, title, records)

    title = "SYNTHETIC DEMO — avalanche multiplicity"
    fig, ax = plt.subplots()
    ax.hist(events["n_avalanches"], bins=30)
    ax.set_xlabel("Avalanches per event")
    ax.set_ylabel("Events")
    ax.set_title(title)
    save(fig, args.output, "P002", "avalanche_multiplicity",
         events[["event", "n_avalanches"]], title, records)

    type_counts = aval.groupby("type", as_index=False).size()
    title = "SYNTHETIC DEMO — avalanche source composition"
    fig, ax = plt.subplots()
    ax.bar(type_counts["type"], type_counts["size"])
    ax.set_ylabel("Avalanches")
    ax.set_title(title)
    ax.tick_params(axis="x", rotation=25)
    save(fig, args.output, "P003", "avalanche_sources",
         type_counts, title, records)

    title = "SYNTHETIC DEMO — avalanche time by source"
    fig, ax = plt.subplots()
    for name, group in aval.groupby("type"):
        ax.hist(group["time_ns"], bins=80, histtype="step", label=name)
    ax.set_xlabel("Avalanche time [ns]")
    ax.set_ylabel("Entries")
    ax.set_title(title)
    ax.legend(fontsize=7)
    save(fig, args.output, "P004", "avalanche_time",
         aval[["type", "time_ns"]], title, records)

    finite = aval[np.isfinite(aval["delta_since_last_fire_ns"])].copy()
    if len(finite) > 30000:
        finite = finite.sample(30000, random_state=20260723)
    title = "SYNTHETIC DEMO — microcell recovery"
    fig, ax = plt.subplots()
    ax.scatter(finite["delta_since_last_fire_ns"],
               finite["amplitude_pe"], s=4, alpha=0.25)
    ax.set_xlim(0, min(150, finite["delta_since_last_fire_ns"].max()))
    ax.set_xlabel("Time since previous cell fire [ns]")
    ax.set_ylabel("Avalanche amplitude [PE]")
    ax.set_title(title)
    save(fig, args.output, "P005", "recovery",
         finite[["delta_since_last_fire_ns", "amplitude_pe",
                 "recovery_fraction", "type"]], title, records)

    occupancy = aval.pivot_table(index="cell_y", columns="cell_x",
                                 values="index", aggfunc="count",
                                 fill_value=0)
    title = "SYNTHETIC DEMO — cell occupancy"
    fig, ax = plt.subplots()
    image = ax.imshow(occupancy.to_numpy(), origin="lower", aspect="equal")
    fig.colorbar(image, ax=ax, label="Avalanches")
    ax.set_xlabel("Cell x")
    ax.set_ylabel("Cell y")
    ax.set_title(title)
    occ_source = occupancy.stack().rename("count").reset_index()
    save(fig, args.output, "P006", "cell_occupancy",
         occ_source, title, records)

    title = "SYNTHETIC DEMO — waveform peak vs avalanche charge"
    fig, ax = plt.subplots()
    ax.scatter(events["charge_pe"], events["peak_pe"], s=8, alpha=0.35)
    ax.set_xlabel("Sum of avalanche amplitudes [PE]")
    ax.set_ylabel("Waveform peak [PE]")
    ax.set_title(title)
    save(fig, args.output, "P007", "peak_vs_charge",
         events[["event", "charge_pe", "peak_pe"]], title, records)

    qbins = pd.qcut(events["n_primary_candidates"], q=12,
                    duplicates="drop")
    sat = events.groupby(qbins, observed=True).agg(
        primary_mean=("n_primary_candidates", "mean"),
        unique_mean=("n_unique_cells", "mean"),
        unique_std=("n_unique_cells", "std"),
        n=("event", "size"),
    ).reset_index(drop=True)
    title = "SYNTHETIC DEMO — finite-cell response"
    fig, ax = plt.subplots()
    ax.errorbar(sat["primary_mean"], sat["unique_mean"],
                yerr=sat["unique_std"], fmt="o")
    ax.set_xlabel("Primary candidates")
    ax.set_ylabel("Unique fired cells")
    ax.set_title(title)
    save(fig, args.output, "P008", "finite_cell_response",
         sat, title, records)

    title = "SYNTHETIC DEMO — first avalanche time"
    valid_first = events[np.isfinite(events["first_time_ns"])]
    fig, ax = plt.subplots()
    ax.hist(valid_first["first_time_ns"], bins=50)
    ax.set_xlabel("First avalanche time [ns]")
    ax.set_ylabel("Events")
    ax.set_title(title)
    save(fig, args.output, "P009", "first_time",
         valid_first[["event", "first_time_ns"]], title, records)

    selected = wave[wave["event"] < 8]
    title = "SYNTHETIC DEMO — waveform overlays"
    fig, ax = plt.subplots()
    for event, group in selected.groupby("event"):
        ax.plot(group["time_ns"], group["analog_pe"], linewidth=0.8,
                label=f"event {event}")
    ax.set_xlabel("Time [ns]")
    ax.set_ylabel("Analog amplitude [PE]")
    ax.set_title(title)
    ax.legend(fontsize=7)
    save(fig, args.output, "P010", "waveforms",
         selected[["event", "time_ns", "signal_pe", "analog_pe", "adc"]],
         title, records)

    title = "SYNTHETIC DEMO — correlated-noise excess"
    excess = events.assign(
        avalanche_excess=events["n_avalanches"] -
                         events["n_primary_candidates"] -
                         events["n_dark_candidates"])
    fig, ax = plt.subplots()
    ax.hist(excess["avalanche_excess"], bins=25)
    ax.set_xlabel("Avalanches - primary candidates - dark candidates")
    ax.set_ylabel("Events")
    ax.set_title(title)
    save(fig, args.output, "P011", "correlated_excess",
         excess[["event", "avalanche_excess"]], title, records)

    manifest = {
        "status": "SYNTHETIC_DEMO_ONLY",
        "input": str(args.input),
        "n_events": int(len(events)),
        "n_avalanches": int(len(aval)),
        "plots": records,
    }
    (args.output / "plot_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(records)} synthetic diagnostic plots")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
