// Genera dsp/dsp_coeffs.h (coeficientes que se compilan en la Pico) y un
// reporte de diseno con todos los pasos intermedios (memoria de calculo).
#include <cstdio>
#include <cstdlib>
#include "../dsp/dsp_config.h"
#include "../dsp/dsp_design.hpp"

int main(int argc, char** argv) {
    const char* hdr = argc > 1 ? argv[1] : "../dsp/dsp_coeffs.h";
    const char* rep = argc > 2 ? argv[2] : "out/design_report.txt";
    const char* pzf = argc > 3 ? argv[3] : "out/iir_poles_zeros.csv";
    const double fs = DSP_FS_HZ;
    std::vector<double> tones = DSP_TONE_HZ_INIT;

    auto fir = dsp::design_fir_multiband(tones, fs, DSP_FIR_M, DSP_FIR_HALFBW_HZ);
    std::vector<dsp::IirDesign> iir;
    for (double f : tones) iir.push_back(dsp::design_iir_bandpass(f, DSP_IIR_BW_HZ, fs, DSP_IIR_LP_ORDER));

    // Estabilidad (REQ-AUD-05 c): todos los polos dentro del circulo unitario.
    bool stable = true;
    for (auto& d : iir) if (!(d.max_pole_radius < 1.0)) stable = false;

    FILE* f = fopen(hdr, "w");
    if (!f) { perror(hdr); return 1; }
    fprintf(f, "/* GENERADO por host/gen_coeffs.cpp. No editar a mano. */\n#ifndef DSP_COEFFS_H\n#define DSP_COEFFS_H\n#include \"dsp_config.h\"\n\n");
    fprintf(f, "static const float dsp_tone_hz[DSP_NTONES] = {");
    for (double t : tones) fprintf(f, " %.1ff,", t);
    fprintf(f, " };\n\n/* FIR multibanda Hamming, M=%d, simetrico (fase lineal) */\nstatic const float dsp_fir_h[DSP_FIR_TAPS] = {", DSP_FIR_M);
    for (size_t n = 0; n < fir.h.size(); n++) fprintf(f, "%s%.9ef,", (n % 4 == 0) ? "\n    " : " ", fir.h[n]);
    fprintf(f, "\n};\n\n/* Banco IIR: por tono, DSP_IIR_NSEC biquads {b0,b1,b2,a1,a2}; y[n]=b0 w+b1 w1+b2 w2, w=x-a1 w1-a2 w2 */\n");
    fprintf(f, "static const float dsp_iir_sos[DSP_NTONES * DSP_IIR_NSEC][5] = {\n");
    for (size_t t = 0; t < iir.size(); t++)
        for (auto& q : iir[t].sos)
            fprintf(f, "    { %.9ef, %.9ef, %.9ef, %.9ef, %.9ef },  /* tono %zu */\n", q.b0, q.b1, q.b2, q.a1, q.a2, t);
    fprintf(f, "};\n\n#endif\n");
    fclose(f);

    FILE* r = fopen(rep, "w");
    FILE* pz = fopen(pzf, "w");
    if (!r || !pz) { perror("out/"); return 1; }
    fprintf(pz, "tone_hz,kind,re,im\n");
    fprintf(r, "REPORTE DE DISENO  Fs=%d Hz\nTonos:", DSP_FS_HZ);
    for (double t : tones) fprintf(r, " %.1f", t);
    fprintf(r, " Hz  (separacion minima %.1f Hz)\n\n", tones.size() > 1 ? tones[1] - tones[0] : 0.0);

    double Df = 3.3 * fs / DSP_FIR_M;
    fprintf(r, "== FIR multibanda (Hamming) ==\n M=%d  taps=%d  banda de paso = tono +/- %.0f Hz\n", DSP_FIR_M, DSP_FIR_TAPS, DSP_FIR_HALFBW_HZ);
    fprintf(r, " Ancho de transicion Hamming  df ~ 3.3 Fs/M = %.1f Hz\n Retardo de grupo tau = M/(2 Fs) = %.2f ms (%d muestras)\n", Df, 1000.0 * DSP_FIR_M / (2 * fs), DSP_FIR_M / 2);
    for (size_t k = 0; k < tones.size(); k++)
        fprintf(r, " Banda %zu: ganancia sin normalizar en el centro = %.4f (se divide entre ella)\n", k, fir.band_gain_before_norm[k]);
    fprintf(r, " Requisito de orden para la separacion minima de 150 Hz: df <= 2(150-%.0f) = %.0f Hz  =>  M >= 3.3*Fs/df = %.0f\n\n",
            DSP_FIR_HALFBW_HZ, 2 * (150 - DSP_FIR_HALFBW_HZ), 3.3 * fs / (2 * (150 - DSP_FIR_HALFBW_HZ)));

    fprintf(r, "== Banco IIR (Butterworth N=%d -> pasabanda, bilineal con prewarping) ==\n", DSP_IIR_LP_ORDER);
    for (size_t t = 0; t < iir.size(); t++) {
        auto& d = iir[t];
        fprintf(r, "\n-- Tono %.1f Hz, BW(-3dB)=%.0f Hz --\n", tones[t], DSP_IIR_BW_HZ);
        fprintf(r, " Prewarp: Omega1=%.3f  Omega2=%.3f rad/s ; B=%.3f ; Omega_0,k = sqrt(Omega1 Omega2) = %.3f rad/s\n", d.omega1, d.omega2, d.B, d.omega0_analog);
        fprintf(r, " Centro digital real 2 atan(Omega0 Ts/2) = %.3f Hz (tono %.1f Hz)\n", d.w0_digital * fs / (2 * dsp::PI), tones[t]);
        for (auto& p : d.lp_poles) fprintf(r, " Polo prototipo LP  p = %+.6f %+.6fj\n", p.real(), p.imag());
        for (size_t i = 0; i < d.poles_z.size(); i += 2)
            fprintf(r, " Polo z = %+.6f %+.6fj  |z|=%.6f\n", d.poles_z[i].real(), d.poles_z[i].imag(), std::abs(d.poles_z[i]));
        for (size_t i = 0; i < d.sos.size(); i++)
            fprintf(r, " Biquad %zu: b=[%.8f %.8f %.8f]  a=[1 %.8f %.8f]\n", i, d.sos[i].b0, d.sos[i].b1, d.sos[i].b2, d.sos[i].a1, d.sos[i].a2);
        fprintf(r, " Radio maximo de polos = %.6f  (%s)\n", d.max_pole_radius, d.max_pole_radius < 1 ? "ESTABLE" : "INESTABLE");
        for (auto& p : d.poles_z) fprintf(pz, "%.1f,pole,%.9f,%.9f\n", tones[t], p.real(), p.imag());
        for (auto& z : d.zeros_z) fprintf(pz, "%.1f,zero,%.9f,%.9f\n", tones[t], z.real(), z.imag());
    }
    fprintf(r, "\nEstabilidad global: %s\n", stable ? "TODOS los polos dentro del circulo unitario" : "FALLA");
    fclose(r); fclose(pz);
    printf("Escribi %s, %s, %s  (%s)\n", hdr, rep, pzf, stable ? "estable" : "INESTABLE");
    return stable ? 0 : 2;
}
