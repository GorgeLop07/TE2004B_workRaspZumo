%% DEMO_DSP  Recorrido completo del modulo DSP, paso a paso, con figuras en matlab/out/
%  Ejecutar:  >> demo_dsp          (necesita ../host/noise/factory1.wav y volvo.wav)
%  Cambia SNR_DB o NOISE_KIND abajo y vuelve a correr para ver como se comporta cada filtro.
clear; close all; rng(7);
SNR_DB     = -3;     % SNR de banda ancha de los tonos del escenario
NOISE_KIND = 1;      % 0 = AWGN, 1 = AWGN + 2 NoiseX-92, 2 = solo NoiseX-92
here = fileparts(mfilename('fullpath'));  outdir = fullfile(here, 'out');  if ~exist(outdir,'dir'), mkdir(outdir); end
save_fig = @(f, name) exportgraphics(f, fullfile(outdir, name), 'Resolution', 130);

P = dsp_params();  D = design_all(P);
fprintf('Fs = %d Hz, tonos = %s Hz\n', P.Fs, mat2str(P.tones));

%% 1. Diseno (REQ-AUD-04 / 05)
fprintf('\n== FIR multibanda ==\n  M = %d (%d coef.), banda de paso = tono +/- %g Hz\n', P.firM, P.firM+1, P.firHalfBW);
fprintf('  Ancho de transicion Hamming ~ 3.3 Fs/M = %.1f Hz ; retardo de grupo = M/(2Fs) = %.1f ms\n', 3.3*P.Fs/P.firM, 1000*P.firM/(2*P.Fs));
fprintf('  Ganancia de cada banda antes de normalizar: %s\n', mat2str(D.G, 4));
fprintf('\n== Banco IIR (Butterworth N=%d -> pasabanda, bilineal con prewarping) ==\n', P.lpOrder);
for k = 1:numel(P.tones)
    d = D.iir{k};
    fprintf('  Tono %4.0f Hz: Omega1=%.2f Omega2=%.2f B=%.2f  Omega0,k=%.3f rad/s  centro digital=%.2f Hz  radio max de polos=%.4f (%s)\n', ...
        P.tones(k), d.omega1, d.omega2, d.B, d.omega0, d.w0digital, d.maxPoleRadius, ternary(d.maxPoleRadius < 1, 'ESTABLE', 'INESTABLE'));
end

f  = 0:2.5:P.Fs/2;  w = 2*pi*f/P.Fs;
Hf = fir_response(D.h, w);
Hi = zeros(size(w));  for k = 1:numel(P.tones), Hi = Hi + sos_response(D.iir{k}.sos, w); end
fig = figure('Position', [50 50 1200 420]);
for p = 1:2
    subplot(1,2,p); plot(f, 20*log10(abs(Hf)+1e-12), f, 20*log10(abs(Hi)+1e-12), 'LineWidth', 1.1); grid on;
    ylim([-100 5]); xlabel('f [Hz]'); ylabel('|H| [dB]');
    if p == 1, xlim([0 4000]); title('Respuesta en frecuencia'); legend('FIR multibanda', 'Banco IIR', 'Location', 'southeast');
    else,      xlim([500 1200]); title('Zoom alrededor de los tonos'); end
end
save_fig(fig, 'm1_respuesta.png');

fig = figure('Position', [50 50 1200 460]);
subplot(1,3,1); stem(0:P.firM, D.h, '.'); grid on; xlabel('n'); ylabel('h[n]'); title('FIR: h[n] simetrica (fase lineal)');
subplot(1,3,2); th = linspace(0,2*pi,400); plot(cos(th), sin(th), 'k-', 'LineWidth', .5); hold on; axis equal; grid on;
pl = [D.iir{1}.poles, D.iir{2}.poles];  zr = [D.iir{1}.zeros, D.iir{2}.zeros];
plot(real(zr), imag(zr), 'bo', 'MarkerSize', 9); plot(real(pl), imag(pl), 'rx', 'MarkerSize', 9, 'LineWidth', 1.5);
xlabel('Re'); ylabel('Im'); title('Banco IIR: polos (x) y ceros (o)');
subplot(1,3,3); zf = roots(D.h); plot(cos(th), sin(th), 'k-', 'LineWidth', .5); hold on; axis equal; grid on;
plot(real(zf), imag(zf), 'bo', 'MarkerSize', 4); plot(0, 0, 'rx', 'MarkerSize', 9, 'LineWidth', 1.5);
xlabel('Re'); ylabel('Im'); title(sprintf('FIR: %d ceros, %d polos en z=0', numel(zf), numel(zf)));
save_fig(fig, 'm2_polos_ceros.png');

%% 2. Los filtros "a mano" == filter()   (prueba de equivalencia)
xt = 0.1*randn(1, 3000);
chk = @(a, b) max(abs(a - b));
fprintf('\n== Equivalencia muestra-a-muestra (bufer circular / DF-II) vs vectorizado ==\n');
fprintf('  bloqueo DC: %.2e   FIR: %.2e   banco IIR: %.2e   (diferencia maxima)\n', ...
    chk(filters_ref('dc', xt, P.dcR), filters_fast('dc', xt, P.dcR)), ...
    chk(filters_ref('fir', xt, D.h), filters_fast('fir', xt, D.h)), ...
    chk(filters_ref('iir', xt, D.sos, P.lpOrder), filters_fast('iir', xt, D.sos, P.lpOrder)));

%% 3. Escenario: ruido -> tono 700 Hz -> ruido -> tono 1000 Hz  (REQ-AUD-03)
secs = 4;  Ns = secs*P.Fs;  t = (0:Ns-1)/P.Fs;
A = sqrt(2*10^(SNR_DB/10))*P.sigmaN;
noise = P.sigmaN*gen_noise(Ns, NOISE_KIND, load_noisex(P.Fs));
tone  = A*sin(2*pi*P.tones(1)*t).*(t >= 1 & t < 2) + A*sin(2*pi*P.tones(2)*t).*(t >= 3 & t < 4);
x = quant12(P.dcBias + noise + tone);                          % lo que veria el ADC de 12 bits
fprintf('\n== Escenario: SNR de banda ancha %+g dB, ruido tipo %d ==\n', SNR_DB, NOISE_KIND);
C = run_chains(x, P, D);

fig = figure('Position', [50 50 1200 600]);
sel = t >= 0.95 & t < 1.25;
names = {'Contaminada (sin DC)', 'Salida FIR', 'Salida banco IIR'};  flds = {'raw', 'fir', 'iir'};
for i = 1:3
    subplot(3,1,i); plot(t(sel), C.(flds{i})(sel)); grid on; ylabel('amplitud'); title(names{i});
    xline(1.0, 'r:');
end
xlabel('t [s]  (el tono de 700 Hz entra en t = 1 s; el FIR retrasa 16 ms)');
save_fig(fig, 'm3_forma_de_onda.png');

%% 4. Perfilado espectral (REQ-AUD-09): tono 700 Hz a SNR 0 dB, 30 s
Np = 30*P.Fs;  tp = (0:Np-1)/P.Fs;  nzp = gen_noise(Np, 1, load_noisex(P.Fs));
ton = sqrt(2)*P.sigmaN*sin(2*pi*P.tones(1)*tp);   nn = P.sigmaN*nzp;
S = {quant12(P.dcBias + ton + nn), quant12(P.dcBias + ton), quant12(P.dcBias + nn)};   % mezcla / solo tono / solo ruido
Cp = cellfun(@(s) run_chains(s, P, D), S);
skip = P.Fs;                                                   % descarta el transitorio (1 s)
pw = @(sig) mean(sig(skip+1:end).^2);
fprintf('\n== Perfilado espectral (tono %g Hz, SNR de entrada 0 dB) ==\n', P.tones(1));
snrIn = 10*log10(pw(Cp(2).raw)/pw(Cp(3).raw));
fprintf('  SNR a la entrada del filtro: %+.2f dB\n', snrIn);
for nm = {'fir', 'iir'}
    so = 10*log10(pw(Cp(2).(nm{1}))/pw(Cp(3).(nm{1})));
    fprintf('  %-4s SNR de salida %+.2f dB -> ganancia de SNR %+.2f dB\n', upper(nm{1}), so, so - snrIn);
end
fig = figure('Position', [50 50 1100 780]);
for i = 1:3
    [fw, ps] = welch_psd(Cp(1).(flds{i}), P.Fs, skip);
    subplot(3,1,i); plot(fw, 10*log10(ps + 1e-30), 'LineWidth', .9); grid on; ylim([-215 -20]); ylabel('PSD [dB]');
    xline(P.tones, 'r:'); title(names{i});
    if i == 3, xlabel('f [Hz]  (rojo: tonos de comando)'); end
end
save_fig(fig, 'm4_espectros.png');
[fa, pa] = welch_psd(Cp(3).raw, P.Fs, skip);
for nm = {'fir', 'iir'}
    [~, pf] = welch_psd(Cp(3).(nm{1}), P.Fs, skip);
    bands = [P.tones(1)+100, P.tones(2)-100; 100 600; P.tones(2)+150 3900];  bn = {'entre tonos', '100-600 Hz', '1150-3900 Hz'};
    for b = 1:3
        m = fa >= bands(b,1) & fa <= bands(b,2);
        fprintf('  %s: atenuacion del ruido %-12s %5.1f dB\n', upper(nm{1}), bn{b}, 10*log10(mean(pa(m))/mean(pf(m))));
    end
end

%% 5. Goertzel y validacion (REQ-AUD-07 / 08)
thr = calibrate_thr(P, D, load_noisex(P.Fs), NOISE_KIND);
fprintf('\n== Deteccion ==\n  Umbrales calibrados (A700/A1000): sin filtro %.4f/%.4f, FIR %.4f/%.4f, IIR %.4f/%.4f\n', thr.raw, thr.fir, thr.iir);
[mc, ~] = goertzel_blocks(C.fir(1:20*P.N), C.raw(1:20*P.N), P.tones, P.Fs, P.N, true);
[mf, ~] = goertzel_blocks(C.fir(1:20*P.N), C.raw(1:20*P.N), P.tones, P.Fs, P.N, false);
fprintf('  Goertzel recursivo vs bin DFT directo: diferencia maxima %.2e\n', max(abs(mc(:) - mf(:))));
fig = figure('Position', [50 50 1200 780]);  tb = ((1:floor(Ns/P.N))*P.N - P.N/2)/P.Fs;
for i = 1:3
    [mag, praw] = goertzel_blocks(C.(flds{i}), C.raw, P.tones, P.Fs, P.N);
    [ev, info] = detector_run(mag, praw, P, thr.(flds{i}));
    subplot(3,1,i); plot(tb, mag(1,:), 'o-', tb, mag(2,:), 's-'); hold on; grid on;
    yline(thr.(flds{i})(1), 'b:'); yline(thr.(flds{i})(2), 'r:');
    for e = 1:size(ev,1), xline((ev(e,1)*P.N)/P.Fs, 'k-', sprintf('CMD campo %d', ev(e,2)), 'LabelVerticalAlignment', 'bottom'); end
    ylabel('amplitud'); title(sprintf('%s  ->  %d comando(s)', names{i}, size(ev,1))); legend('A_{700}', 'A_{1000}', 'Location', 'northeast');
    fprintf('  %-22s comandos aceptados: %s\n', names{i}, mat2str(ev(:,2).'));
end
xlabel('t [s]   (verdad: campo 1 en 1-2 s, campo 2 en 3-4 s)');
save_fig(fig, 'm5_goertzel_deteccion.png');
fprintf('\nFiguras en %s\n', outdir);

function s = ternary(c, a, b), if c, s = a; else, s = b; end, end
