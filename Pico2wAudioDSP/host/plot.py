#!/usr/bin/env python3
"""Solo grafica los CSV que genera bench / gen_coeffs. No disena ningun filtro:
numpy se usa unicamente para las raices del polinomio FIR (diagrama de ceros)."""
import csv, re, sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

def rows(name):
    with open(f"{OUT}/{name}") as f:
        return list(csv.DictReader(f))

# --- 1. Respuestas en frecuencia -------------------------------------------
r = rows("response.csv")
f = np.array([float(x["f_hz"]) for x in r])
fig, ax = plt.subplots(1, 2, figsize=(13, 4.2))
for a, (lo, hi, t) in zip(ax, [(0, 4000, "0-4 kHz"), (500, 1200, "zoom alrededor de los tonos")]):
    a.plot(f, [float(x["fir_db"]) for x in r], label="FIR multibanda (Hamming, M=256)")
    a.plot(f, [float(x["iir_db"]) for x in r], label="Banco IIR (Butterworth N=2, bilineal)")
    a.set_xlim(lo, hi); a.set_ylim(-100, 5); a.set_xlabel("f [Hz]"); a.set_ylabel("|H| [dB]"); a.set_title(t); a.grid(alpha=.3)
ax[0].legend(loc="lower right")
fig.tight_layout(); fig.savefig(f"{OUT}/fig1_respuesta.png", dpi=130); plt.close(fig)

# --- 2. Polos y ceros en el plano z ----------------------------------------
pz = rows("iir_poles_zeros.csv")
h = []
with open("../dsp/dsp_coeffs.h") as fh:
    txt = fh.read()
m = re.search(r"dsp_fir_h\[DSP_FIR_TAPS\] = \{(.*?)\};", txt, re.S)
h = np.array([float(v.replace("f", "")) for v in re.findall(r"[-+0-9.e]+f", m.group(1))])
zr = np.roots(h)
fig, ax = plt.subplots(1, 2, figsize=(11, 5.2))
th = np.linspace(0, 2 * np.pi, 400)
for a in ax:
    a.plot(np.cos(th), np.sin(th), "k-", lw=.6); a.axhline(0, c="gray", lw=.4); a.axvline(0, c="gray", lw=.4)
    a.set_aspect("equal"); a.set_xlabel("Re"); a.set_ylabel("Im"); a.grid(alpha=.2)
for kind, mk, col in [("zero", "o", "tab:blue"), ("pole", "x", "tab:red")]:
    pts = [(float(x["re"]), float(x["im"])) for x in pz if x["kind"] == kind]
    ax[0].scatter(*zip(*pts), marker=mk, s=70, c=col if mk == "x" else "none", edgecolors=col, label={"zero": "ceros (z=+-1)", "pole": "polos"}[kind])
ax[0].set_title("Banco IIR (todos los polos dentro del circulo)"); ax[0].legend(loc="lower left")
ax[1].scatter(zr.real, zr.imag, marker="o", s=14, facecolors="none", edgecolors="tab:blue", label=f"{len(zr)} ceros")
ax[1].scatter([0], [0], marker="x", s=70, c="tab:red", label=f"{len(zr)} polos en z=0")
ax[1].set_title("FIR multibanda (fase lineal)"); ax[1].legend(loc="lower left")
fig.tight_layout(); fig.savefig(f"{OUT}/fig2_polos_ceros.png", dpi=130); plt.close(fig)

# --- 3. Perfilado espectral antes / despues ---------------------------------
s = rows("spectra.csv")
f = np.array([float(x["f_hz"]) for x in s])
fig, ax = plt.subplots(3, 1, figsize=(11, 8.5), sharex=True, sharey=True)
for a, (key, t, c) in zip(ax, [("raw_db", "Senal contaminada (AWGN + factory1 + volvo + tono 700 Hz, SNR 0 dB)", "gray"),
                                ("fir_db", "Salida FIR multibanda", "tab:blue"), ("iir_db", "Salida banco IIR", "tab:orange")]):
    a.plot(f, [float(x[key]) for x in s], c=c, lw=.9); a.set_title(t, fontsize=10); a.grid(alpha=.3); a.set_ylabel("PSD [dB]")
    for t0 in (700, 1000): a.axvline(t0, c="red", ls=":", lw=.8)
ax[-1].set_xlabel("f [Hz]  (lineas rojas: tonos de comando 700 y 1000 Hz)")
fig.tight_layout(); fig.savefig(f"{OUT}/fig3_espectros.png", dpi=130); plt.close(fig)

# --- 4. Curva de estres -------------------------------------------------------
st = [x for x in rows("stress.csv") if x["snr_db"] != "false_alarms"]
scn = list(dict.fromkeys(x["scenario"] for x in st))
fig, ax = plt.subplots(1, len(scn), figsize=(5 * len(scn), 4), sharey=True)
for a, sc in zip(np.atleast_1d(ax), scn):
    for ch, col in [("sin_filtro", "gray"), ("FIR", "tab:blue"), ("IIR", "tab:orange")]:
        d = [x for x in st if x["scenario"] == sc and x["chain"] == ch]
        a.plot([float(x["snr_db"]) for x in d], [100 * float(x["correct_rate"]) for x in d], "o-", c=col, label=ch.replace("_", " "))
    a.set_title(sc); a.set_xlabel("SNR de entrada [dB]"); a.grid(alpha=.3); a.invert_xaxis()
np.atleast_1d(ax)[0].set_ylabel("Comandos correctos [%]"); np.atleast_1d(ax)[0].legend()
fig.tight_layout(); fig.savefig(f"{OUT}/fig4_estres_snr.png", dpi=130); plt.close(fig)
print("Figuras escritas en", OUT)
