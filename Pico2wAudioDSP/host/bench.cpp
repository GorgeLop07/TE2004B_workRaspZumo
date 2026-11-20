// Banco de pruebas de la PC para el modulo DSP.
//  - REQ-AUD-03: inyecta AWGN + dos archivos NoiseX-92 sobre los tonos de comando.
//  - REQ-AUD-09: perfilado espectral (FFT/Welch) antes vs despues de ambos filtros.
//  - Entregable 3: curva de estres SNR vs tasa de acierto (sin filtro / FIR / IIR).
//  - Entregable 4: benchmark en la PC (la Pico mide el suyo con el comando 'b').
// Usa EXACTAMENTE la misma cadena (dsp/dsp_runtime.c) que corre en la Pico.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>
extern "C" {
#include "../dsp/dsp_runtime.h"
}
#include "../dsp/dsp_coeffs.h"

using namespace std;
typedef complex<double> cd;
static const double PI = 3.14159265358979323846;
static const double FS = DSP_FS_HZ;
static const double SIGMA_N = 0.10;     // ruido ambiente fijo (RMS, escala completa = 1)
static const double DC_BIAS = 0.03;     // el MAX4466 sale polarizado a Vcc/2: probamos el bloqueo DC

/* ------------------------------------------------------------ WAV ------ */
static bool load_wav(const string& path, vector<double>& out, double& sr) {
    ifstream f(path, ios::binary);
    if (!f) return false;
    vector<unsigned char> b((istreambuf_iterator<char>(f)), {});
    if (b.size() < 44 || memcmp(&b[0], "RIFF", 4) || memcmp(&b[8], "WAVE", 4)) return false;
    size_t p = 12; int ch = 1, bits = 16; sr = 8000;
    while (p + 8 <= b.size()) {
        uint32_t sz = b[p+4] | b[p+5] << 8 | b[p+6] << 16 | (uint32_t)b[p+7] << 24;
        if (!memcmp(&b[p], "fmt ", 4)) { ch = b[p+10] | b[p+11] << 8; sr = b[p+12] | b[p+13] << 8 | b[p+14] << 16 | b[p+15] << 24; bits = b[p+22] | b[p+23] << 8; }
        if (!memcmp(&b[p], "data", 4)) {
            size_t n = min<size_t>(sz, b.size() - p - 8), bps = bits / 8;
            for (size_t i = 0; i + bps * ch <= n; i += bps * ch) {
                const unsigned char* q = &b[p + 8 + i];
                out.push_back(bits == 16 ? (int16_t)(q[0] | q[1] << 8) / 32768.0 : (q[0] - 128) / 128.0);
            }
            return true;
        }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

/* Remuestreo por sinc enventanada (Blackman): pasabajas a 0.45*Fs_out y evaluacion
 * en los instantes de salida. Explicito, sin librerias de DSP. */
static vector<double> resample(const vector<double>& x, double sr_in, double sr_out) {
    double ratio = sr_in / sr_out;
    double fc = 0.45 / ratio;                     // ciclos / muestra de entrada
    int L = (int)ceil(16.0 / (2 * fc));           // 16 cruces por cero a cada lado
    size_t M = (size_t)((x.size() - 1) / ratio);
    vector<double> y(M);
    for (size_t m = 0; m < M; m++) {
        double t = m * ratio, acc = 0;
        long n0 = (long)floor(t) - L + 1, n1 = (long)floor(t) + L;
        for (long n = max(0L, n0); n <= min((long)x.size() - 1, n1); n++) {
            double u = t - n, a = 2 * PI * fc * u;
            double s = fabs(a) < 1e-12 ? 1.0 : sin(a) / a;
            double w = 0.42 + 0.5 * cos(PI * u / L) + 0.08 * cos(2 * PI * u / L);
            acc += x[n] * 2 * fc * s * w;
        }
        y[m] = acc;
    }
    return y;
}
static void unit_rms(vector<double>& x) {
    double m = 0; for (double v : x) m += v; m /= x.size();
    double e = 0; for (double& v : x) { v -= m; e += v * v; }
    double r = sqrt(e / x.size()); for (double& v : x) v /= r;
}

/* ------------------------------------------------------------ FFT ------ */
static void fft(vector<cd>& a) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        cd wl = polar(1.0, -2 * PI / len);
        for (size_t i = 0; i < n; i += len) { cd w = 1; for (size_t k = 0; k < len / 2; k++) { cd u = a[i+k], v = a[i+k+len/2] * w; a[i+k] = u + v; a[i+k+len/2] = u - v; w *= wl; } }
    }
}
/* PSD de Welch: Hann 2048, 50 % de traslape. Devuelve potencia lineal por bin (0..N/2). */
static vector<double> welch(const vector<double>& x, size_t skip) {
    const size_t N = 2048; vector<double> psd(N / 2 + 1, 0.0), w(N); double wp = 0;
    for (size_t i = 0; i < N; i++) { w[i] = 0.5 - 0.5 * cos(2 * PI * i / N); wp += w[i] * w[i]; }
    int cnt = 0;
    for (size_t s = skip; s + N <= x.size(); s += N / 2) {
        vector<cd> a(N); double mean = 0; for (size_t i = 0; i < N; i++) mean += x[s+i]; mean /= N;
        for (size_t i = 0; i < N; i++) a[i] = (x[s+i] - mean) * w[i];
        fft(a);
        for (size_t k = 0; k <= N / 2; k++) psd[k] += norm(a[k]) / (wp * FS);
        cnt++;
    }
    for (double& v : psd) v /= cnt;
    return psd;
}
static double band_power(const vector<double>& psd, double f1, double f2) {   // media lineal de PSD en [f1,f2]
    const double df = FS / 2048; double s = 0; int n = 0;
    for (size_t k = 0; k < psd.size(); k++) { double f = k * df; if (f >= f1 && f <= f2) { s += psd[k]; n++; } }
    return n ? s / n : 0;
}
static double db(double v) { return 10 * log10(max(v, 1e-30)); }
static double power(const vector<double>& x, size_t skip) { double e = 0; for (size_t i = skip; i < x.size(); i++) e += x[i] * x[i]; return e / (x.size() - skip); }

/* ------------------------------------------------------------ ruido ---- */
struct Noise {
    vector<double> a, b;   // dos ruidos NoiseX-92 remuestreados a Fs, RMS unitario
    mt19937_64 rng; normal_distribution<double> nd{0, 1};
    // kind 0: solo AWGN.  1: (AWGN + NoiseX_a + NoiseX_b)/sqrt(3).  2: (NoiseX_a + NoiseX_b)/sqrt(2).
    // En todos los casos potencias iguales entre componentes y RMS unitario.
    vector<double> gen(size_t n, int kind) {
        vector<double> y(n);
        size_t oa = rng() % a.size(), ob = rng() % b.size();
        for (size_t i = 0; i < n; i++) {
            double g = nd(rng);
            double na = a[(oa + i) % a.size()], nb = b[(ob + i) % b.size()];
            y[i] = kind == 0 ? g : kind == 1 ? (g + na + nb) / sqrt(3.0) : (na + nb) / sqrt(2.0);
        }
        return y;
    }
};

static float quant12(double v) { v = max(-1.0, min(1.0, v)); return (float)(round(v * 2048.0) / 2048.0); }   // ADC de 12 bits

/* ------------------------------------------------------------ pruebas -- */
struct Rates { int ok = 0, wrong = 0, miss = 0, n = 0; };
struct Calib { dsp_detector_t det[DSP_NCHAINS]; };
static const char* CHAIN[] = { "sin_filtro", "FIR", "IIR" };

static Calib calibrate(Noise& nz, int mix) {
    Calib c; dsp_pipeline_t p; dsp_pipeline_init(&p);
    for (int k = 0; k < DSP_NCHAINS; k++) dsp_det_init(&c.det[k]);
    vector<double> n = nz.gen((size_t)(20 + DSP_CAL_BLOCKS + 2) * DSP_GOERTZEL_N, mix);
    int blocks = 0;
    for (double v : n) {
        if (dsp_pipeline_push(&p, quant12(DC_BIAS + SIGMA_N * v))) {
            if (++blocks > 20) for (int k = 0; k < DSP_NCHAINS; k++) dsp_det_update(&c.det[k], &p.blk[k], nullptr);
        }
    }
    return c;
}

/* mode: 1,2 = tono del campo 1/2 ; 0 = solo ruido ; 3 = ambos tonos con la misma amplitud (ambiguo) */
static void run_trial(Noise& nz, int mix, const Calib& cal, int mode, double snr_db, int result[DSP_NCHAINS], int false_alarm[DSP_NCHAINS]) {
    dsp_pipeline_t p; dsp_pipeline_init(&p);
    dsp_detector_t det[DSP_NCHAINS]; memcpy(det, cal.det, sizeof det);
    size_t onset = (size_t)20 * DSP_GOERTZEL_N + nz.rng() % DSP_GOERTZEL_N;     // el tono llega en un instante no alineado a bloque
    size_t total = onset + (size_t)24 * DSP_GOERTZEL_N;
    vector<double> n = nz.gen(total, mix);
    double A = sqrt(2.0 * pow(10.0, snr_db / 10.0) * SIGMA_N * SIGMA_N);
    if (mode == 3) A /= sqrt(2.0);                                              // misma potencia total de tonos
    double ph1 = 2 * PI * (nz.rng() % 1000) / 1000.0, ph2 = 2 * PI * (nz.rng() % 1000) / 1000.0;
    for (int k = 0; k < DSP_NCHAINS; k++) { result[k] = -1; false_alarm[k] = 0; }
    for (size_t i = 0; i < total; i++) {
        double x = DC_BIAS + SIGMA_N * n[i];
        if (i >= onset) {
            if (mode == 1 || mode == 3) x += A * sin(2 * PI * dsp_tone_hz[0] * i / FS + ph1);
            if (mode == 2 || mode == 3) x += A * sin(2 * PI * dsp_tone_hz[1] * i / FS + ph2);
        }
        if (dsp_pipeline_push(&p, quant12(x)))
            for (int k = 0; k < DSP_NCHAINS; k++) {
                dsp_event_t ev;
                if (dsp_det_update(&det[k], &p.blk[k], &ev)) {
                    if (i < onset + DSP_GOERTZEL_N) false_alarm[k]++;
                    else if (result[k] < 0) result[k] = ev.field;
                }
            }
    }
}

/* ------------------------------------------------------------ main ----- */
int main(int argc, char** argv) {
    string noisedir = "noise", outdir = "out"; int trials = 200; unsigned seed = 12345;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--noise") && i + 1 < argc) noisedir = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--trials") && i + 1 < argc) trials = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atoi(argv[++i]);
    }
    dsp_global_init();
    Noise nz; nz.rng.seed(seed);
    const char* files[2] = { "factory1.wav", "volvo.wav" };
    vector<double>* dst[2] = { &nz.a, &nz.b };
    for (int i = 0; i < 2; i++) {
        vector<double> w; double sr;
        if (!load_wav(noisedir + "/" + files[i], w, sr)) { fprintf(stderr, "No pude leer %s/%s\n", noisedir.c_str(), files[i]); return 1; }
        *dst[i] = resample(w, sr, FS); unit_rms(*dst[i]);
        printf("NoiseX-92 %-13s %.0f Hz -> %.0f Hz, %.1f s\n", files[i], sr, FS, dst[i]->size() / FS);
    }

    /* ---- Respuestas en frecuencia de los coeficientes que van en la Pico ---- */
    {
        ofstream f(outdir + "/response.csv"); f << "f_hz,fir_db,iir_db\n";
        for (double fr = 0; fr <= FS / 2; fr += 2.5) {
            double w = 2 * PI * fr / FS; cd z1 = polar(1.0, -w), z2 = z1 * z1, hf = 0, hi = 0;
            for (int n = 0; n < DSP_FIR_TAPS; n++) hf += (double)dsp_fir_h[n] * polar(1.0, -w * n);
            for (int t = 0; t < DSP_NTONES; t++) {
                cd h = 1;
                for (int q = 0; q < DSP_IIR_NSEC; q++) { const float* c = dsp_iir_sos[t * DSP_IIR_NSEC + q]; h *= ((double)c[0] + (double)c[1] * z1 + (double)c[2] * z2) / (1.0 + (double)c[3] * z1 + (double)c[4] * z2); }
                hi += h;
            }
            f << fr << "," << db(norm(hf)) << "," << db(norm(hi)) << "\n";
        }
    }

    /* ---- Perfilado espectral: tono 700 Hz a SNR 0 dB sobre AWGN + 2 NoiseX ---- */
    {
        size_t N = (size_t)(FS * 30); vector<double> noise = nz.gen(N, 1);
        double A = sqrt(2.0 * SIGMA_N * SIGMA_N);          // SNR = 0 dB
        vector<double> xs[3][3];                            // [cadena][mezcla/solo tono/solo ruido]
        dsp_dc_t dc[3][3] = {}; dsp_fir_t fir[3] = {}; dsp_iir_t iir[3] = {};
        for (int s = 0; s < 3; s++) for (int c = 0; c < 3; c++) xs[c][s].resize(N);
        for (size_t i = 0; i < N; i++) {
            double tone = A * sin(2 * PI * dsp_tone_hz[0] * i / FS), nn = SIGMA_N * noise[i];
            double in[3] = { tone + nn, tone, nn };
            for (int s = 0; s < 3; s++) {
                float xd = dsp_dc_step(&dc[0][s], quant12(DC_BIAS + in[s]));
                xs[0][s][i] = xd; xs[1][s][i] = dsp_fir_step(&fir[s], xd); xs[2][s][i] = dsp_iir_step(&iir[s], xd);
            }
        }
        const size_t skip = (size_t)FS;                     // descarta el transitorio (1 s)
        vector<double> P[3][3];
        for (int c = 0; c < 3; c++) for (int s = 0; s < 3; s++) P[c][s] = welch(xs[c][s], skip);
        ofstream f(outdir + "/spectra.csv"); f << "f_hz,raw_db,fir_db,iir_db\n";
        for (size_t k = 0; k < P[0][0].size(); k++) f << k * FS / 2048 << "," << db(P[0][0][k]) << "," << db(P[1][0][k]) << "," << db(P[2][0][k]) << "\n";

        ofstream m(outdir + "/profile_metrics.txt");
        auto both = [&](const char* fmt, auto... a) { char buf[512]; snprintf(buf, sizeof buf, fmt, a...); fputs(buf, stdout); m << buf; };
        both("\n=== Perfilado espectral (tono %.0f Hz, SNR de entrada 0 dB, AWGN + factory1 + volvo) ===\n", dsp_tone_hz[0]);
        double snr_in = db(power(xs[0][1], skip) / power(xs[0][2], skip));
        both("SNR de banda ancha a la entrada del filtro: %+.2f dB\n", snr_in);
        const char* nm[3] = { "sin filtro", "FIR", "IIR" };
        for (int c = 1; c < 3; c++) {
            double snr_out = db(power(xs[c][1], skip) / power(xs[c][2], skip));
            both("%-4s SNR de salida %+.2f dB  -> ganancia de SNR %+.2f dB ; ganancia del tono %+.2f dB\n", nm[c], snr_out, snr_out - snr_in,
                 db(power(xs[c][1], skip) / power(xs[0][1], skip)));
        }
        struct Band { const char* n; double a, b; } bands[] = { { "100-600 Hz (bajas, motores)", 100, 600 }, { "800-900 Hz (entre tonos)", dsp_tone_hz[0] + 100, dsp_tone_hz[1] - 100 },
                                                               { "1150-3900 Hz (altas)", dsp_tone_hz[1] + 150, 3900 } };
        both("Atenuacion del ruido (PSD media del ruido solo, entrada -> salida):\n");
        for (auto& b : bands) {
            double r = band_power(P[0][2], b.a, b.b);
            both("  %-30s FIR %6.1f dB   IIR %6.1f dB\n", b.n, db(r) - db(band_power(P[1][2], b.a, b.b)), db(r) - db(band_power(P[2][2], b.a, b.b)));
        }
        both("Componentes espectrales dominantes fuera de banda (mezcla, relativo al pico del tono, a mas de 100 Hz de ambos tonos de comando):\n");
        for (int c = 0; c < 3; c++) {
            double pk = 0, oob = 0, fo = 0;
            for (size_t k = 0; k < P[c][0].size(); k++) { double fr = k * FS / 2048; if (fabs(fr - dsp_tone_hz[0]) < 30) pk = max(pk, P[c][0][k]); }
            for (size_t k = 5; k < P[c][0].size(); k++) { double fr = k * FS / 2048; if (fabs(fr - dsp_tone_hz[0]) > 100 && fabs(fr - dsp_tone_hz[1]) > 100 && P[c][0][k] > oob) { oob = P[c][0][k]; fo = fr; } }
            both("  %-10s pico fuera de banda a %6.1f Hz : %+6.1f dB respecto al tono\n", nm[c], fo, db(oob) - db(pk));
        }
        both("Distorsion armonica (tono solo, 2do y 3er armonico respecto al fundamental):\n");
        for (int c = 0; c < 3; c++) {
            auto at = [&](double f0) { double mx = 0; for (size_t k = 0; k < P[c][1].size(); k++) if (fabs(k * FS / 2048 - f0) < 25) mx = max(mx, P[c][1][k]); return mx; };
            both("  %-10s 2f: %+7.1f dBc   3f: %+7.1f dBc\n", nm[c], db(at(2 * dsp_tone_hz[0])) - db(at(dsp_tone_hz[0])), db(at(3 * dsp_tone_hz[0])) - db(at(dsp_tone_hz[0])));
        }
    }

    /* ---- Curva de estres SNR vs tasa de acierto ---- */
    {
        const double snrs[] = { 10, 5, 0, -5, -8, -10, -12, -15 };
        ofstream f(outdir + "/stress.csv"); f << "scenario,chain,snr_db,trials,correct,wrong,miss,correct_rate\n";
        printf("\n=== Curva de estres: tasa de acierto (%d pruebas por punto, tono de 1 s, ruido ambiente RMS=%.2f) ===\n", trials, SIGMA_N);
        for (int mix = 0; mix < 3; mix++) {
            const char* scn = mix == 0 ? "AWGN" : mix == 1 ? "AWGN+NoiseX" : "NoiseX";
            Calib cal = calibrate(nz, mix);
            printf("\nEscenario %s   umbrales calibrados (A700/A1000):", scn);
            for (int k = 0; k < DSP_NCHAINS; k++) printf("  %s=%.4f/%.4f", CHAIN[k], cal.det[k].thr[0], cal.det[k].thr[1]);
            printf("\n  SNR[dB] |");
            for (int k = 0; k < DSP_NCHAINS; k++) printf(" %-10s ok%% wrong%%|", CHAIN[k]);
            printf("\n");
            for (double snr : snrs) {
                Rates r[DSP_NCHAINS];
                for (int t = 0; t < trials; t++) {
                    int mode = 1 + (t & 1), res[DSP_NCHAINS], fa[DSP_NCHAINS];
                    run_trial(nz, mix, cal, mode, snr, res, fa);
                    for (int k = 0; k < DSP_NCHAINS; k++) { r[k].n++; if (res[k] == mode) r[k].ok++; else if (res[k] > 0) r[k].wrong++; else r[k].miss++; }
                }
                printf("  %+6.0f  |", snr);
                for (int k = 0; k < DSP_NCHAINS; k++) {
                    printf(" %-10s %5.1f %5.1f |", "", 100.0 * r[k].ok / r[k].n, 100.0 * r[k].wrong / r[k].n);
                    f << scn << "," << CHAIN[k] << "," << snr << "," << r[k].n << "," << r[k].ok << "," << r[k].wrong << "," << r[k].miss << "," << (double)r[k].ok / r[k].n << "\n";
                }
                printf("\n");
            }
            /* Falsas alarmas: ruido solo, 300 s */
            int fa_total[DSP_NCHAINS] = {}; int amb_accept[DSP_NCHAINS] = {};
            const int NF = 60;                                  // 60 pruebas de ~5 s
            for (int t = 0; t < NF; t++) { int res[DSP_NCHAINS], fa[DSP_NCHAINS]; run_trial(nz, mix, cal, 0, 0, res, fa); for (int k = 0; k < DSP_NCHAINS; k++) fa_total[k] += fa[k] + (res[k] > 0); }
            for (int t = 0; t < trials; t++) { int res[DSP_NCHAINS], fa[DSP_NCHAINS]; run_trial(nz, mix, cal, 3, 5, res, fa); for (int k = 0; k < DSP_NCHAINS; k++) amb_accept[k] += (res[k] > 0); }
            double secs = NF * (44 * DSP_GOERTZEL_N) / FS;
            printf("  Falsas alarmas (solo ruido, %.0f s):", secs);
            for (int k = 0; k < DSP_NCHAINS; k++) printf("  %s=%d (%.1f/h)", CHAIN[k], fa_total[k], fa_total[k] * 3600.0 / secs);
            printf("\n  Dos tonos de igual amplitud (debe rechazar): comandos aceptados de %d:", trials);
            for (int k = 0; k < DSP_NCHAINS; k++) printf("  %s=%d", CHAIN[k], amb_accept[k]);
            printf("\n");
            for (int k = 0; k < DSP_NCHAINS; k++) f << scn << "," << CHAIN[k] << ",false_alarms," << fa_total[k] << ",,,," << fa_total[k] * 3600.0 / secs << "\n";
        }
    }

    /* ---- Benchmark en la PC (referencia; los numeros de la Pico salen del firmware) ---- */
    {
        const size_t N = 400000; vector<float> x(N); mt19937 g(1); normal_distribution<float> nd(0, 0.1f);
        for (auto& v : x) v = nd(g);
        ofstream f(outdir + "/bench_host.txt");
        auto both = [&](const char* fmt, auto... a) { char buf[512]; snprintf(buf, sizeof buf, fmt, a...); fputs(buf, stdout); f << buf; };
        both("\n=== Benchmark en la PC (x86, -O2). La Pico 2 W se mide con el comando 'b' del firmware ===\n");
        both("%-26s %12s %10s %12s\n", "Bloque", "ns/muestra", "MAC/muestra", "RAM estado");
        volatile float sink = 0;
        auto timeit = [&](const char* name, size_t state_bytes, auto&& fn) {
            dsp_mac_count = 0; auto t0 = chrono::high_resolution_clock::now(); float acc = 0;
            for (size_t i = 0; i < N; i++) acc += fn(x[i]);
            auto t1 = chrono::high_resolution_clock::now(); sink = acc;
            both("%-26s %12.1f %10.1f %9zu B\n", name, chrono::duration<double, nano>(t1 - t0).count() / N, (double)dsp_mac_count / N, state_bytes);
        };
        dsp_dc_t dc = {}; dsp_fir_t fir = {}; dsp_iir_t iir = {}; dsp_pipeline_t pl; dsp_pipeline_init(&pl);
        timeit("bloqueo DC", sizeof dc, [&](float v) { return dsp_dc_step(&dc, v); });
        timeit("FIR multibanda (M=256)", sizeof fir, [&](float v) { return dsp_fir_step(&fir, v); });
        timeit("banco IIR (2x2 biquads)", sizeof iir, [&](float v) { return dsp_iir_step(&iir, v); });
        timeit("tuberia completa", sizeof pl, [&](float v) { dsp_pipeline_push(&pl, v); return 0.0f; });
        both("Coeficientes en flash: FIR %zu B, IIR %zu B, ventana Hann %zu B\n", sizeof dsp_fir_h, sizeof dsp_iir_sos, sizeof(float) * DSP_GOERTZEL_N);
    }
    printf("\nListo. CSVs en %s/\n", outdir.c_str());
    return 0;
}
