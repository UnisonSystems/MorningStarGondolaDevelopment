#!/usr/bin/env python3
"""
Flight Report generator.

Produces:
  - Report/Flight_Report.pdf   (7 pages of plots + instructions)
  - Report/trajectory_3d.kmz  (3-D path for Google Earth)
  - Report/trajectory_snapshot.png (Trajectory-Google Earth overlay, included in Flight_Report.pdf)

Assumes DATALOG.CSV...
  - is present in the current working directory;
  - has a header row;
  - contains only data below the header row.

To install all necessary dependencies, run: pip install -r requirements.txt

See COL (below) for CSV column structure.

Ascent (blue) vs Descent (red) is determined by the single maximum-altitude sample.
"""

import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.ticker import MaxNLocator, ScalarFormatter
import contextily as cx
from pyproj import Transformer
import simplekml
from simplekml import AltitudeMode, Color, LookAt


# ----------------------------------------------------------------------
# Configuration / constants
# ----------------------------------------------------------------------
CSV_PATH = "DATALOG.CSV"
PDF_PATH = os.path.join("Report", "Flight_Report.pdf")
KMZ_PATH = os.path.join("Report", "trajectory_3d.kmz")
MAP_PNG  = os.path.join("Report", "trajectory_snapshot.png")

# 0-based column indices (user provided 1-based numbers)
COL = {
    "lat": 2,      # degrees * 1e5
    "lon": 3,      # degrees * 1e5
    "hrs": 4,
    "min": 5,
    "sec": 6,
    "alt": 7,      # m ASL
    "speed": 9,    # m/s
    "temp": 11,    # °C
    "press": 12,   # mbar
    "hum": 14,     # RH %
}

# Colors for ascent / descent phases
ASC_COLOR = "#1f77b4"   # blue
DES_COLOR = "#d62728"   # red


# ----------------------------------------------------------------------
# 1. Data loading, cleaning, derived quantities
# ----------------------------------------------------------------------
def load_and_prepare(csv_path: str = CSV_PATH):
    """Load CSV (with header), clean, build time & ascent-rate series.

    Cleaning is intentionally permissive and matches the spirit of the
    original mapper2.py / mapper3d_2.py scripts: only require valid
    lat / lon / alt + time components and basic physical bounds.
    Sensor channels (temp, pressure, humidity, speed) are allowed to
    contain NaNs; the plots simply break the line at missing samples.
    """
    df = pd.read_csv(csv_path, header=0)

    # Extract raw series (coerce to numeric)
    lat_raw  = pd.to_numeric(df.iloc[:, COL["lat"]],  errors="coerce") / 1e5
    lon_raw  = pd.to_numeric(df.iloc[:, COL["lon"]],  errors="coerce") / 1e5
    hrs      = pd.to_numeric(df.iloc[:, COL["hrs"]],  errors="coerce")
    mins     = pd.to_numeric(df.iloc[:, COL["min"]],  errors="coerce")
    secs     = pd.to_numeric(df.iloc[:, COL["sec"]],  errors="coerce")
    alt_raw  = pd.to_numeric(df.iloc[:, COL["alt"]],  errors="coerce")
    spd_raw  = pd.to_numeric(df.iloc[:, COL["speed"]],errors="coerce")
    temp_raw = pd.to_numeric(df.iloc[:, COL["temp"]], errors="coerce")
    prs_raw  = pd.to_numeric(df.iloc[:, COL["press"]],errors="coerce")
    hum_raw  = pd.to_numeric(df.iloc[:, COL["hum"]],  errors="coerce")

    # Time in minutes from an arbitrary origin (will subtract t0 later)
    t_min_raw = hrs * 60.0 + mins + secs / 60.0

    # CORE mask only – navigation + time (matches original mapper scripts)
    # Deliberately do NOT require every sensor to be valid at every row.
    mask = (
        lat_raw.notna() & lon_raw.notna() & alt_raw.notna()
        & hrs.notna() & mins.notna() & secs.notna()
        & (lat_raw.abs() <= 90.0) & (lon_raw.abs() <= 180.0)
        & ~((lat_raw == 0.0) & (lon_raw == 0.0))
        & (alt_raw > -500.0) & (alt_raw < 100_000.0)
    )

    # Apply core mask
    lat  = lat_raw[mask].to_numpy()
    lon  = lon_raw[mask].to_numpy()
    alt  = alt_raw[mask].to_numpy()
    spd  = spd_raw[mask].to_numpy()   # may still contain NaNs
    temp = temp_raw[mask].to_numpy()  # may still contain NaNs
    prs  = prs_raw[mask].to_numpy()   # may still contain NaNs
    hum  = hum_raw[mask].to_numpy()   # may still contain NaNs
    t_min = t_min_raw[mask].to_numpy()

    n_valid = len(lat)
    print(f"Loaded {len(df)} rows → {n_valid} valid points after core cleaning.")
    if n_valid < 2:
        # Extra diagnostic to help the user
        print("Diagnostic counts (before mask):")
        print(f"  lat finite : {lat_raw.notna().sum()}")
        print(f"  lon finite : {lon_raw.notna().sum()}")
        print(f"  alt finite : {alt_raw.notna().sum()}")
        print(f"  hrs finite : {hrs.notna().sum()}")
        print(f"  min finite : {mins.notna().sum()}")
        print(f"  sec finite : {secs.notna().sum()}")
        raise ValueError(
            "Fewer than 2 valid points after cleaning. "
            "Check that columns 3/4/5/6/7/8 contain lat/lon/hrs/min/sec/alt "
            "and that the values are numeric."
        )

    # Relative time (minutes from first valid sample)
    t0 = t_min[0]
    time_min = t_min - t0                     # minutes
    time_sec = time_min * 60.0                # seconds (for gradient)

    # Ascent rate (m/s)
    ascent = np.gradient(alt, time_sec)

    # Simple stats (ignore NaNs in sensor channels)
    print(f"Flight duration: {time_min[-1]:.1f} min | "
          f"Max altitude: {np.nanmax(alt):.0f} m | "
          f"Max speed: {np.nanmax(spd):.1f} m/s")

    # Optional NaN counts so you can see sensor health
    for name, arr in [("temp", temp), ("press", prs), ("hum", hum), ("speed", spd)]:
        n_nan = int(np.isnan(arr).sum())
        if n_nan:
            print(f"  {name}: {n_nan} NaNs remaining (will appear as gaps)")

    return {
        "lat": lat, "lon": lon, "alt": alt,
        "temp": temp, "press": prs, "hum": hum,
        "speed": spd, "ascent": ascent,
        "time_min": time_min,
    }


# ----------------------------------------------------------------------
# 2. Plot helpers
# ----------------------------------------------------------------------
def style_axes(ax, xlabel: str, ylabel: str, nbins: int = 8):
    """Apply consistent, non-crowded tick formatting and labels."""
    ax.set_xlabel(xlabel, fontsize=11)
    ax.set_ylabel(ylabel, fontsize=11)
    ax.xaxis.set_major_locator(MaxNLocator(nbins=nbins, prune="both"))
    ax.yaxis.set_major_locator(MaxNLocator(nbins=nbins, prune="both"))
    ax.tick_params(axis="both", labelsize=9)
    for axis in (ax.xaxis, ax.yaxis):
        fmt = ScalarFormatter(useOffset=False)
        fmt.set_scientific(False)
        axis.set_major_formatter(fmt)
    ax.grid(True, which="major", linestyle="--", alpha=0.35)
    ax.set_axisbelow(True)


def plot_time_series(ax, t, y, ylabel, idx_peak, show_legend=True):
    """Plot a time series split into ascent (blue) and descent (red)."""
    # Ascent (includes the peak point)
    ax.plot(t[:idx_peak + 1], y[:idx_peak + 1],
            color=ASC_COLOR, linewidth=1.7, solid_capstyle="round", label="Ascent")
    # Descent (from the peak onward)
    if idx_peak < len(t) - 1:
        ax.plot(t[idx_peak:], y[idx_peak:],
                color=DES_COLOR, linewidth=1.7, solid_capstyle="round", label="Descent")
    style_axes(ax, "Time (min)", ylabel)
    if show_legend:
        ax.legend(loc="best", fontsize=9, framealpha=0.9)


def plot_vs_altitude(ax, x, alt, xlabel, idx_peak, show_legend=True):
    """Plot altitude vs another quantity, colored by ascent / descent phase."""
    ax.plot(x[:idx_peak + 1], alt[:idx_peak + 1],
            color=ASC_COLOR, linewidth=1.5, label="Ascent")
    if idx_peak < len(x) - 1:
        ax.plot(x[idx_peak:], alt[idx_peak:],
                color=DES_COLOR, linewidth=1.5, label="Descent")
    style_axes(ax, xlabel, "Altitude (m)")
    if show_legend:
        ax.legend(loc="best", fontsize=9, framealpha=0.9)


# ----------------------------------------------------------------------
# 3. 2-D trajectory map (adapted from mapper2.py)
# ----------------------------------------------------------------------
def create_trajectory_map(lat, lon, idx_peak=None, ax=None, save_png: str = None):
    """
    Plot the ground track on Esri World Imagery.
    If idx_peak is given, color the path blue (ascent) then red (descent).
    If ax is None a new figure is created. Returns the figure.
    """
    transformer = Transformer.from_crs("EPSG:4326", "EPSG:3857", always_xy=True)
    x, y = transformer.transform(lon, lat)

    if ax is None:
        fig, ax = plt.subplots(figsize=(10, 10))
    else:
        fig = ax.figure

    # Trajectory – split by phase when possible
    if idx_peak is not None and 0 <= idx_peak < len(x):
        ax.plot(x[:idx_peak + 1], y[:idx_peak + 1],
                color=ASC_COLOR, linewidth=2.5, zorder=3, solid_capstyle="round")
        if idx_peak < len(x) - 1:
            ax.plot(x[idx_peak:], y[idx_peak:],
                    color=DES_COLOR, linewidth=2.5, zorder=3, solid_capstyle="round")
    else:
        ax.plot(x, y, color="crimson", linewidth=2.5, zorder=3, solid_capstyle="round")

    # Start / End markers
    ax.scatter(x[0], y[0], c="lime", s=160, zorder=4,
               edgecolors="black", linewidths=1.3, marker="o")
    ax.scatter(x[-1], y[-1], c="red", s=160, zorder=4,
               edgecolors="black", linewidths=1.3, marker="s")

    # Adaptive padding
    x_range = x.max() - x.min()
    y_range = y.max() - y.min()
    pad_x = max(x_range * 0.08, 200.0)
    pad_y = max(y_range * 0.08, 200.0)
    ax.set_xlim(x.min() - pad_x, x.max() + pad_x)
    ax.set_ylim(y.min() - pad_y, y.max() + pad_y)

    # Satellite basemap
    try:
        cx.add_basemap(
            ax,
            source=cx.providers.Esri.WorldImagery,
            zoom="auto",
            attribution=False,
        )
    except Exception as e:
        print(f"Warning: basemap failed ({e}). Continuing without imagery.")

    ax.set_aspect("equal")
    ax.set_axis_off()          # pure image – no labels, ticks, spines

    if save_png:
        fig.savefig(save_png, dpi=250, bbox_inches="tight",
                    facecolor="white", pad_inches=0.02)
        print(f"Saved map PNG → {save_png}")

    return fig


# ----------------------------------------------------------------------
# 4. KMZ generation (adapted from mapper3d_2.py)
# ----------------------------------------------------------------------
def compute_bearing(lat1, lon1, lat2, lon2):
    lat1, lon1, lat2, lon2 = map(np.radians, [lat1, lon1, lat2, lon2])
    dlon = lon2 - lon1
    x = np.sin(dlon) * np.cos(lat2)
    y = np.cos(lat1) * np.sin(lat2) - np.sin(lat1) * np.cos(lat2) * np.cos(dlon)
    bearing = np.degrees(np.arctan2(x, y))
    return (bearing + 360.0) % 360.0


def create_3d_trajectory_kmz(lats, lons, alts, output_path=KMZ_PATH,
                             name="Flight Trajectory", tilt=55.0,
                             range_factor=1.9, min_range=800.0, side="+90"):
    lats = np.asarray(lats, dtype=float)
    lons = np.asarray(lons, dtype=float)
    alts = np.asarray(alts, dtype=float)

    if len(lats) < 2:
        raise ValueError("Need at least two valid points for KMZ")

    to_ecef = Transformer.from_crs("EPSG:4326", "EPSG:4978", always_xy=True)
    to_geodetic = Transformer.from_crs("EPSG:4978", "EPSG:4326", always_xy=True)

    x, y, z = to_ecef.transform(lons, lats, alts)
    mean_x, mean_y, mean_z = np.mean(x), np.mean(y), np.mean(z)
    target_lon, target_lat, target_alt = to_geodetic.transform(mean_x, mean_y, mean_z)

    bearing = compute_bearing(lats[0], lons[0], lats[-1], lons[-1])
    heading = (bearing + 90.0) % 360.0 if side == "+90" else (bearing - 90.0) % 360.0

    dists = np.sqrt((x - mean_x)**2 + (y - mean_y)**2 + (z - mean_z)**2)
    range_m = max(float(np.max(dists)) * range_factor, min_range)

    kml = simplekml.Kml(name=name)

    kml.document.lookat = LookAt(
        longitude=target_lon,
        latitude=target_lat,
        altitude=target_alt,
        range=range_m,
        heading=heading,
        tilt=tilt,
        altitudemode=AltitudeMode.absolute,
    )

    # 3-D path
    ls = kml.newlinestring(name="3D Trajectory")
    ls.coords = list(zip(lons, lats, alts))
    ls.altitudemode = AltitudeMode.absolute
    ls.style.linestyle.color = Color.red
    ls.style.linestyle.width = 5

    # Ground track
    gt = kml.newlinestring(name="Ground Track")
    gt.coords = list(zip(lons, lats))
    gt.altitudemode = AltitudeMode.clamptoground
    gt.style.linestyle.color = Color.changealphaint(180, Color.yellow)
    gt.style.linestyle.width = 2.5

    # Placemarks
    max_idx = int(np.argmax(alts))
    for label, idx, color in [
        ("Start", 0, Color.lime),
        ("End", -1, Color.red),
        (f"Max Altitude ({alts[max_idx]:.0f} m)", max_idx, Color.cyan),
    ]:
        p = kml.newpoint(name=label)
        p.coords = [(lons[idx], lats[idx], alts[idx])]
        p.altitudemode = AltitudeMode.absolute
        p.style.iconstyle.color = color
        p.style.iconstyle.scale = 1.15

    kml.document.description = (
        f"<b>{name}</b><br/>"
        f"Points: {len(lats)}<br/>"
        f"Max altitude: {np.max(alts):.1f} m<br/>"
        f"Min altitude: {np.min(alts):.1f} m<br/>"
        f"Start: {lats[0]:.5f}, {lons[0]:.5f}<br/>"
        f"End:   {lats[-1]:.5f}, {lons[-1]:.5f}"
    )

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    kml.savekmz(output_path)
    print(f"Saved KMZ → {output_path}")
    return output_path


# ----------------------------------------------------------------------
# 5. Build the multi-page PDF
# ----------------------------------------------------------------------
def build_report(data: dict, pdf_path: str = PDF_PATH):
    t   = data["time_min"]
    alt = data["alt"]
    temp = data["temp"]
    prs  = data["press"]
    hum  = data["hum"]
    spd  = data["speed"]
    asc  = data["ascent"]
    lat  = data["lat"]
    lon  = data["lon"]

    # Split point: index of maximum altitude
    idx_peak = int(np.argmax(alt))
    print(f"Peak altitude {alt[idx_peak]:.0f} m at t = {t[idx_peak]:.1f} min "
          f"(index {idx_peak} of {len(alt)-1})")

    with PdfPages(pdf_path) as pdf:
        # --------------------------------------------------------------
        # Page 1 – Altitude vs Time
        # --------------------------------------------------------------
        fig, ax = plt.subplots(figsize=(10, 8))
        plot_time_series(ax, t, alt, "Altitude (m)", idx_peak)
        fig.tight_layout(pad=1.5)
        pdf.savefig(fig, dpi=200)
        plt.close(fig)

        # --------------------------------------------------------------
        # Page 2 – Temperature vs Time  +  Altitude vs Temperature
        # --------------------------------------------------------------
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10))
        plot_time_series(ax1, t, temp, "Temperature (°C)", idx_peak)
        plot_vs_altitude(ax2, temp, alt, "Temperature (°C)", idx_peak)
        fig.tight_layout(pad=2.0, h_pad=2.5)
        pdf.savefig(fig, dpi=200)
        plt.close(fig)

        # --------------------------------------------------------------
        # Page 3 – Pressure vs Time  +  Altitude vs Pressure
        # --------------------------------------------------------------
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10))
        plot_time_series(ax1, t, prs, "Pressure (mbar)", idx_peak)
        plot_vs_altitude(ax2, prs, alt, "Pressure (mbar)", idx_peak)
        fig.tight_layout(pad=2.0, h_pad=2.5)
        pdf.savefig(fig, dpi=200)
        plt.close(fig)

        # --------------------------------------------------------------
        # Page 4 – Humidity vs Time  +  Altitude vs Humidity
        # --------------------------------------------------------------
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10))
        plot_time_series(ax1, t, hum, "Humidity (RH%)", idx_peak)
        plot_vs_altitude(ax2, hum, alt, "Humidity (RH%)", idx_peak)
        fig.tight_layout(pad=2.0, h_pad=2.5)
        pdf.savefig(fig, dpi=200)
        plt.close(fig)

        # --------------------------------------------------------------
        # Page 5 – Speed vs Time  +  Altitude vs Speed
        # --------------------------------------------------------------
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10))
        plot_time_series(ax1, t, spd, "Speed (m/s)", idx_peak)
        plot_vs_altitude(ax2, spd, alt, "Speed (m/s)", idx_peak)
        fig.tight_layout(pad=2.0, h_pad=2.5)
        pdf.savefig(fig, dpi=200)
        plt.close(fig)

        # --------------------------------------------------------------
        # Page 6 – Ascent Rate vs Time  +  Altitude vs Ascent Rate
        # --------------------------------------------------------------
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 10))
        plot_time_series(ax1, t, asc, "Ascent Rate (m/s)", idx_peak)
        plot_vs_altitude(ax2, asc, alt, "Ascent Rate (m/s)", idx_peak)
        fig.tight_layout(pad=2.0, h_pad=2.5)
        pdf.savefig(fig, dpi=200)
        plt.close(fig)

        # --------------------------------------------------------------
        # Page 7 – 2-D flight path (pure image) + Google Earth instructions
        # --------------------------------------------------------------
        fig = plt.figure(figsize=(10, 12))

        # Map occupies the upper ~70 % of the page
        ax_map = fig.add_axes([0.04, 0.28, 0.92, 0.68])
        create_trajectory_map(lat, lon, idx_peak=idx_peak, ax=ax_map)
        # (create_trajectory_map already turns axes off)

        # Instruction text in the lower portion
        ax_txt = fig.add_axes([0.06, 0.03, 0.88, 0.22])
        ax_txt.axis("off")

        instruction = (
            "Interactive 3-D Trajectory (Google Earth)\n\n"
            f"A 3-D flight path has been written to: {KMZ_PATH}\n\n"
            "To explore it:\n"
            "  1. Open https://earth.google.com/web/\n"
            "  2. Choose New → Import file to map project → Upload from device\n"
            "  3. Select the trajectory_3d.kmz file (in the Report folder).\n"
        )
        ax_txt.text(
            0.0, 0.95, instruction,
            transform=ax_txt.transAxes,
            fontsize=10.5,
            verticalalignment="top",
            fontfamily="sans-serif",
            linespacing=1.35,
            bbox=dict(boxstyle="round,pad=0.6", facecolor="#f7f7f7",
                      edgecolor="#cccccc", alpha=0.95),
        )

        pdf.savefig(fig, dpi=200)
        plt.close(fig)

    print(f"Saved multi-page report → {pdf_path}")


# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------
if __name__ == "__main__":
    os.makedirs("Report", exist_ok=True)

    data = load_and_prepare(CSV_PATH)

    # Generate the PDF report (includes 2-D map page)
    build_report(data, PDF_PATH)

    # Generate the interactive 3-D KMZ
    create_3d_trajectory_kmz(
        data["lat"], data["lon"], data["alt"],
        output_path=KMZ_PATH,
        name="Flight Trajectory",
    )

    # Optional standalone high-res map PNG (useful for other uses)
    idx_peak = int(np.argmax(data["alt"]))
    create_trajectory_map(data["lat"], data["lon"],
                          idx_peak=idx_peak, save_png=MAP_PNG)

    print("\nDone. Created the following in Report in your working directory:")
    print(f"  • {PDF_PATH}")
    print(f"  • {KMZ_PATH}")
    print(f"  • {MAP_PNG}")
