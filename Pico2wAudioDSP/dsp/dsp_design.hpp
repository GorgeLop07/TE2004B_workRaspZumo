/* Diseno de filtros desde primeros principios (REQ-AUD-04 / 05).
 * Sin cajas negras: nada de butter/fir1/designfilt/scipy.signal. Solo
 * ecuaciones explicitas, en doble precision, para ejecutarse en la PC.
 * El firmware solo recibe los coeficientes ya calculados (dsp_coeffs.h). */
#ifndef DSP_DESIGN_HPP
#define DSP_DESIGN_HPP

#include <cmath>
#include <complex>
#include <vector>

namespace dsp {

using cd = std::complex<double>;
const double PI = 3.14159265358979323846;

/* ---------------------------------------------------------------- FIR ---
 * Respuesta ideal de una banda [w1,w2] (rad/muestra), retardada M/2:
 *     h_k[m] = ( sin(w2 m) - sin(w1 m) ) / (pi m),   m = n - M/2
 *     h_k[0] = (w2 - w1) / pi
 * Se enventana con Hamming  w[n] = 0.54 - 0.46 cos(2 pi n / M)  y se suma la
 * contribucion de cada tono (superposicion). Cada banda se normaliza a
 * ganancia unitaria en el centro (el enventanado la baja un poco cuando la
 * banda es angosta frente al lobulo principal de la ventana).               */
struct FirDesign {
    std::vector<double> h;
    std::vector<double> band_gain_before_norm;   // |H_k(w_k)| antes de normalizar
};

inline cd freq_response_fir(const std::vector<double>& h, double w) {
    cd acc = 0;
    for (size_t n = 0; n < h.size(); n++) acc += h[n] * std::exp(cd(0, -w * (double)n));
    return acc;
}

inline FirDesign design_fir_multiband(const std::vector<double>& tones_hz, double fs,
                                      int M, double half_bw_hz) {
    FirDesign d;
    d.h.assign(M + 1, 0.0);
    for (double f0 : tones_hz) {
        double w1 = 2 * PI * (f0 - half_bw_hz) / fs;
        double w2 = 2 * PI * (f0 + half_bw_hz) / fs;
        std::vector<double> hk(M + 1);
        for (int n = 0; n <= M; n++) {
            double m = n - M / 2.0;
            double ideal = (std::fabs(m) < 1e-12) ? (w2 - w1) / PI
                                                   : (std::sin(w2 * m) - std::sin(w1 * m)) / (PI * m);
            double win = 0.54 - 0.46 * std::cos(2 * PI * n / M);
            hk[n] = ideal * win;
        }
        double g = std::abs(freq_response_fir(hk, 2 * PI * f0 / fs));
        d.band_gain_before_norm.push_back(g);
        for (int n = 0; n <= M; n++) d.h[n] += hk[n] / g;
    }
    return d;
}

/* ---------------------------------------------------------------- IIR ---
 * Pasos (REQ-AUD-05 a, b):
 *  1. Prototipo analogico Butterworth pasabajas, Omega_c = 1 rad/s, orden N:
 *         p_i = exp( j pi (2i + N - 1) / (2N) ),   i = 1..N   (semiplano izq.)
 *  2. Prewarping de los bordes -3 dB de la banda (w1, w2 en rad/muestra):
 *         Omega = (2/Ts) tan(w/2),   Omega_0^2 = Omega_1 Omega_2,  B = Omega_2 - Omega_1
 *  3. Transformacion pasabajas -> pasabanda  s -> (s^2 + Omega_0^2) / (B s).
 *     Cada polo p del prototipo da dos polos de la banda: raices de
 *         s^2 - p B s + Omega_0^2 = 0.
 *     El pasabanda tiene N ceros en s = 0 y N en s = inf.
 *  4. Bilineal  s = (2/Ts)(1 - z^-1)/(1 + z^-1)  <=>  z = (1 + s Ts/2)/(1 - s Ts/2).
 *     s = 0 -> z = 1 y s = inf -> z = -1, asi que cada biquad queda con
 *     numerador g (1 - z^-2)  y denominador 1 + a1 z^-1 + a2 z^-2.
 *  5. Ganancia: |H(e^{j w_k})| = 1 en la frecuencia del tono.                */
struct Sos { double b0, b1, b2, a1, a2; };

struct IirDesign {
    std::vector<Sos> sos;
    double omega0_analog = 0;          // Omega_{0,k} [rad/s] (prewarped)
    double omega1 = 0, omega2 = 0, B = 0;
    std::vector<cd> lp_poles;          // prototipo analogico
    std::vector<cd> bp_poles_s;        // polos analogicos del pasabanda (ambos signos de Im)
    std::vector<cd> poles_z;           // polos digitales (todos)
    std::vector<cd> zeros_z;           // ceros digitales (todos)
    double max_pole_radius = 0;
    double w0_digital = 0;             // 2 atan(Omega_0 Ts/2): donde cae realmente el centro
};

inline cd freq_response_sos(const std::vector<Sos>& s, double w) {
    cd z1 = std::exp(cd(0, -w)), z2 = z1 * z1, h = 1;
    for (const Sos& q : s)
        h *= (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
    return h;
}

inline IirDesign design_iir_bandpass(double f0_hz, double bw_hz, double fs, int N) {
    IirDesign d;
    const double Ts = 1.0 / fs;
    double w1 = 2 * PI * (f0_hz - bw_hz / 2) / fs;
    double w2 = 2 * PI * (f0_hz + bw_hz / 2) / fs;
    d.omega1 = (2 / Ts) * std::tan(w1 / 2);
    d.omega2 = (2 / Ts) * std::tan(w2 / 2);
    d.B = d.omega2 - d.omega1;
    d.omega0_analog = std::sqrt(d.omega1 * d.omega2);
    d.w0_digital = 2 * std::atan(d.omega0_analog * Ts / 2);
    const double O0 = d.omega0_analog;

    for (int i = 1; i <= N; i++)
        d.lp_poles.push_back(std::exp(cd(0, PI * (2.0 * i + N - 1) / (2.0 * N))));

    auto bilinear = [&](cd s) { return (1.0 + s * Ts / 2.0) / (1.0 - s * Ts / 2.0); };

    for (const cd& p : d.lp_poles) {
        if (p.imag() < -1e-12) continue;          // el conjugado ya se cubre con Im >= 0
        cd disc = std::sqrt((p * d.B) * (p * d.B) - 4.0 * O0 * O0);
        cd sa = (p * d.B + disc) / 2.0, sb = (p * d.B - disc) / 2.0;
        std::vector<cd> roots;
        if (std::fabs(p.imag()) < 1e-12) roots = { (sa.imag() >= 0) ? sa : sb };   // polo real: un biquad
        else roots = { sa, sb };                                                   // par complejo: dos biquads
        for (const cd& s : roots) {
            cd zp = bilinear(s);
            Sos q{ 1.0, 0.0, -1.0, -2.0 * zp.real(), std::norm(zp) };
            d.sos.push_back(q);
            d.bp_poles_s.push_back(s);  d.bp_poles_s.push_back(std::conj(s));
            d.poles_z.push_back(zp);    d.poles_z.push_back(std::conj(zp));
            d.zeros_z.push_back(1.0);   d.zeros_z.push_back(-1.0);
            d.max_pole_radius = std::max(d.max_pole_radius, std::abs(zp));
        }
    }
    /* Normalizacion de ganancia, repartida por igual entre las secciones. */
    double g = std::abs(freq_response_sos(d.sos, 2 * PI * f0_hz / fs));
    double per = std::pow(1.0 / g, 1.0 / d.sos.size());
    for (Sos& q : d.sos) { q.b0 *= per; q.b1 *= per; q.b2 *= per; }
    return d;
}

} // namespace dsp
#endif
