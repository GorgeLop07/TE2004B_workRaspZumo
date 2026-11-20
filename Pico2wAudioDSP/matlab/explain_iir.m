%% EXPLAIN_IIR  REQ-AUD-05 paso a paso con numeros: del prototipo analogico al banco de biquads.
%  Ejecutar:  >> explain_iir     (figura en out/m8_iir_pasos.png)
%  Se usa el tono de 700 Hz. Todas las respuestas se calculan evaluando las funciones de transferencia,
%  sin ninguna funcion de diseno de MATLAB.
clear; close all;
P = dsp_params();  Fs = P.Fs;  Ts = 1/Fs;  f0 = P.tones(1);  bw = P.iirBW;  N = P.lpOrder;
here = fileparts(mfilename('fullpath'));

%% PASO 0: prototipo analogico pasabajas normalizado (Omega_c = 1 rad/s)
p = exp(1j*pi*(2*(1:N) + N - 1)/(2*N));                       % polos Butterworth, semiplano izquierdo
HLP = @(s) 1 ./ prod(s(:).' - p(:), 1).';                      % H_LP(s) = 1/prod(s - p_i)  (ganancia 1 en DC)
fprintf('PASO 0  Prototipo Butterworth N=%d, |H(j1)|^2 = 1/2 (-3 dB en Omega=1):  polos = %s\n', N, mat2str(p, 4));

%% PASO 1 (a): pasabajas -> pasabanda.  s -> (s^2 + O0^2)/(B s)
w1 = 2*pi*(f0 - bw/2)/Fs;  w2 = 2*pi*(f0 + bw/2)/Fs;
O1 = (2/Ts)*tan(w1/2);  O2 = (2/Ts)*tan(w2/2);                 % prewarping de los bordes -3 dB
O0 = sqrt(O1*O2);  B = O2 - O1;
fprintf('PASO 1  Bordes %g y %g Hz -> prewarp Omega1=%.2f, Omega2=%.2f rad/s\n        Omega_0,k = sqrt(Omega1*Omega2) = %.3f rad/s ( = %.2f Hz analogicos),  B = %.2f rad/s\n', ...
        f0 - bw/2, f0 + bw/2, O1, O2, O0, O0/(2*pi), B);
HBP = @(s) HLP((s.^2 + O0^2)./(B*s));                          % definicion de la transformacion

%% PASO 2 (b): discretizacion bilineal  s = (2/Ts)(1 - z^-1)/(1 + z^-1)
% Polos del pasabanda: raices de s^2 - p B s + O0^2 = 0 (dos por cada polo del prototipo)
D = design_iir_bandpass(f0, bw, Fs, N);
fprintf('PASO 2  Bilineal: z = (1 + s Ts/2)/(1 - s Ts/2).  Polos digitales |z| = %s  -> todos < 1: ESTABLE\n', mat2str(abs(D.poles(1:2:end)), 4));
fprintf('        Ceros: s=0 -> z=+1 y s=inf -> z=-1 (en cada biquad), sos =\n'); disp(D.sos);

%% PASO 3: por que PREWARPING.  La bilineal comprime frecuencias:  Omega = (2/Ts) tan(w/2)
% Para ver el efecto, rehacemos el diseno usando los bordes SIN prewarp (Omega = 2 pi f):
O1n = 2*pi*(f0 - bw/2);  O2n = 2*pi*(f0 + bw/2);  O0n = sqrt(O1n*O2n);  Bn = O2n - O1n;
fprintf('PASO 3  Sin prewarping: Omega_0 = %.1f rad/s -> el centro digital cae en %.1f Hz (error %.1f Hz) y el ancho es %.1f Hz en vez de %g Hz\n        Con prewarping (Omega_0 = %.1f rad/s) el centro cae en %.2f Hz\n', ...
        O0n, 2*atan(O0n*Ts/2)/(2*pi)*Fs, 2*atan(O0n*Ts/2)/(2*pi)*Fs - f0, (2*atan(O2n*Ts/2) - 2*atan(O1n*Ts/2))/(2*pi)*Fs, bw, O0, D.w0digital);

%% PASO 4 (c): invarianza al impulso  z = e^{sTs}  (para compararla)
% H_BP(s) = (B s)^N / prod_k (s - s_k);  residuo r_k = (B s_k)^N / prod_{j!=k}(s_k - s_j);  H(z) = sum Ts r_k /(1 - e^{s_k Ts} z^-1)
sk = [];  for q = p(imag(p) >= -1e-12)
    disc = sqrt((q*B)^2 - 4*O0^2);  sk = [sk, (q*B + disc)/2, (q*B - disc)/2, conj((q*B + disc)/2), conj((q*B - disc)/2)]; %#ok<AGROW>
end
sk = sk(1:2*N);  rk = zeros(size(sk));
for k = 1:numel(sk), rk(k) = (B*sk(k))^N / prod(sk(k) - sk([1:k-1, k+1:end])); end
Hii = @(w) arrayfun(@(ww) sum(Ts*rk ./ (1 - exp(sk*Ts)*exp(-1j*ww))), w);
fr = 1:2:4000;  wr = 2*pi*fr/Fs;
Hb  = abs(sos_response(D.sos, wr));                            % bilineal con prewarp (ya normalizada)
Hi  = abs(Hii(wr));   Hi = Hi/abs(Hii(2*pi*f0/Fs));            % invarianza al impulso, normalizada en el tono
Ha  = abs(HBP(1j*2*pi*fr));  Ha = Ha/abs(HBP(1j*2*pi*f0));     % analogico "ideal" de referencia

fprintf('PASO 4  Atenuacion respecto al tono en 3900 Hz (cerca de Nyquist): bilineal %.1f dB | impulso %.1f dB | analogico %.1f dB\n', ...
    -20*log10(Hb(fr==3901)), -20*log10(Hi(fr==3901)), -20*log10(Ha(fr==3901)));
fprintf('        Ganancia en 0 Hz:   bilineal %.1f dB | impulso %.1f dB  (la bilineal tiene un cero exacto en z=1)\n', ...
    20*log10(abs(sos_response(D.sos, 0)) + 1e-12), 20*log10(abs(Hii(0)) / abs(Hii(2*pi*f0/Fs)) + 1e-12));
fprintf('        Polos impulso |e^{s Ts}| = %s (tambien estables, pero SIN ceros garantizados en z=+-1)\n', mat2str(abs(exp(sk*Ts)), 4));

%% Figura
fig = figure('Position', [50 50 1250 750]);
Om = logspace(-1.5, 1.5, 500);
subplot(2,2,1); semilogx(Om, 20*log10(abs(arrayfun(@(o) HLP(1j*o), Om))), 'LineWidth', 1.2); grid on; ylim([-60 3]); xline(1, 'r--');
xlabel('\Omega [rad/s]'); ylabel('|H_a(j\Omega)| [dB]'); title('0) Prototipo Butterworth pasabajas, \Omega_c = 1');
subplot(2,2,2); plot(fr, 20*log10(Ha + 1e-12), 'LineWidth', 1.2); grid on; ylim([-60 3]); xlim([0 4000]); xline(f0, 'r:');
xlabel('f [Hz]'); ylabel('|H_{BP}(j\Omega)| [dB]'); title('1) Analogico tras pasabajas \rightarrow pasabanda (centro \Omega_{0,k})');
subplot(2,2,3); plot(fr, 20*log10(Hb + 1e-12), fr, 20*log10(Hi + 1e-12), 'LineWidth', 1.2); grid on; ylim([-80 3]); xlim([0 4000]); xline(f0, 'r:');
xlabel('f [Hz]'); ylabel('|H(e^{j\omega})| [dB]'); legend('bilineal + prewarping', 'invarianza al impulso', 'Location', 'south'); title('2-4) Dos formas de discretizar');
subplot(2,2,4); th = linspace(0, 2*pi, 400); plot(cos(th), sin(th), 'k-', 'LineWidth', .5); hold on; axis equal; grid on;
plot(real(D.zeros), imag(D.zeros), 'bo', 'MarkerSize', 9); plot(real(D.poles), imag(D.poles), 'rx', 'MarkerSize', 10, 'LineWidth', 1.5);
plot(real(exp(sk*Ts)), imag(exp(sk*Ts)), 'm+', 'MarkerSize', 8); xlabel('Re'); ylabel('Im'); title('Plano z: bilineal (x, ceros o) vs impulso (+)');
exportgraphics(fig, fullfile(here, 'out', 'm8_iir_pasos.png'), 'Resolution', 130);
