%% EXPLAIN_ORDER_M  Justificacion matematica del orden M del FIR (REQ-AUD-04), con numeros reales.
%  Ejecutar:  >> explain_order_M     (figura en out/m7_orden_M.png)
%
%  1) Un FIR disenado por ventana NO pasa de 1 a 0 de golpe: tiene una zona de TRANSICION de ancho
%         df ~ K*Fs/M        (K = 3.3 Hamming, 3.1 Hann, 5.5 Blackman, 0.9 rectangular)
%     Mas orden M  =>  transicion mas estrecha.  Costo: retardo tau = M/(2 Fs), M+1 coeficientes, M+1 MAC/muestra.
%  2) Cada tono es una banda de paso F +/- B. Su transicion esta CENTRADA en el borde (F + B), o sea que ocupa
%         [F + B - df/2 ,  F + B + df/2].
%     El rechazo pleno (~53 dB en Hamming) solo empieza a  B + df/2  del tono.
%  3) Entre dos tonos separados Fsep queda una brecha entre bordes   g = Fsep - 2B.
%       a) NECESARIA  : las transiciones de las dos bandas no se traslapan:  df <= g       ->  M >= 3.3 Fs / g
%       b) DESEADA    : que quede una banda de rechazo REAL de al menos g/2 de ancho:
%                       df <= g/2                                                          ->  M >= 6.6 Fs / g
%  Con Fs = 8 kHz, tonos a 700 y 1000 Hz y B = 50 Hz:  g = 200 Hz,  M >= 132 (a)  y  M >= 264 (b)  ->  M = 256.
clear; close all;
P = dsp_params();  Fs = P.Fs;  B = P.firHalfBW;  F = P.tones;  K = 3.3;
here = fileparts(mfilename('fullpath'));
sep = F(2) - F(1);  g = sep - 2*B;  Dstop = g/2;                   % ancho de banda de rechazo deseada
Mnec = K*Fs/g;  Mdes = K*Fs/(g/2);

fprintf('== Donde sale M ==\n');
fprintf('Tonos %g y %g Hz (separacion %g Hz), bandas de +/-%g Hz  ->  brecha entre bordes g = %g Hz\n', F(1), F(2), sep, B, g);
fprintf('  (a) necesaria: df <= g      ->  M >= 3.3*Fs/g   = %.0f   (las transiciones solo se tocan; no hay rechazo real)\n', Mnec);
fprintf('  (b) deseada  : df <= g/2    ->  M >= 6.6*Fs/g   = %.0f   (queda una banda de rechazo de %g Hz entre los tonos)\n', Mdes, g/2);
fprintf('  Elegido M = %d (potencia de 2 mas cercana):  df = 3.3*Fs/M = %.1f Hz\n', P.firM, K*Fs/P.firM);
fprintf('     rechazo pleno desde %.1f Hz de cada tono  ->  banda de rechazo entre tonos = [%.0f, %.0f] Hz\n', B + K*Fs/P.firM/2, F(1) + B + K*Fs/P.firM/2, F(2) - B - K*Fs/P.firM/2);
fprintf('     retardo de grupo tau = M/(2Fs) = %.1f ms ; MAC/muestra = %d\n', 1000*P.firM/(2*Fs), P.firM+1);
fprintf('  Peor caso de REQ-AUD-01 (tonos a 150 Hz): g = 150 - 2B = %g Hz -> (a) M >= %.0f, (b) M >= %.0f\n', 150-2*B, K*Fs/(150-2*B), 2*K*Fs/(150-2*B));
for b = [50 25 15]
    fprintf('        con B = %2d Hz: g = %3d Hz -> (a) M >= %4.0f , (b) M >= %4.0f\n', b, 150-2*b, K*Fs/(150-2*b), 2*K*Fs/(150-2*b));
end
fprintf('  => con tonos a solo 150 Hz hay que reducir B (bandas mas angostas) o subir mucho M.\n');

%% Medicion: rechazo en el PEOR PUNTO de la banda de rechazo [F1+100, F2-100] segun M
Ms = [32 64 100 132 192 256 264 384 512 768];   gdl = 1000*Ms/(2*Fs);
fstop = (F(1)+2*B):0.5:(F(2)-2*B);  wst = 2*pi*fstop/Fs;
att = zeros(size(Ms));
for i = 1:numel(Ms)
    h = design_fir_multiband(F, Fs, Ms(i), B);
    att(i) = -20*log10(max(abs(fir_response(h, wst))));
end
fprintf('\n== Medido: rechazo en el PEOR punto entre %g y %g Hz (maximo de |H| en esa banda) ==\n', fstop(1), fstop(end));
fprintf('   M   :'); fprintf(' %6d', Ms); fprintf('\n   dB  :'); fprintf(' %6.1f', att); fprintf('\n   ms  :'); fprintf(' %6.1f', gdl); fprintf('   (retardo de grupo)\n');
fprintf('(el piso de ~50-55 dB es el limite de la ventana Hamming; subir M mas alla de ~%.0f no mejora el rechazo, solo el retardo y el costo)\n', Mdes);

fgrid = 500:1:1200;  wg = 2*pi*fgrid/Fs;
fig = figure('Position', [50 50 1250 420]);
subplot(1,3,1); plot(Ms, att, 'o-', 'LineWidth', 1.3); grid on; hold on;
xline(Mnec, 'r--', sprintf('M=%.0f (a)', Mnec)); xline(Mdes, 'g--', sprintf('M=%.0f (b)', Mdes)); xline(P.firM, 'k-', sprintf('M=%d', P.firM));
yline(53, 'b:', 'piso Hamming'); xlabel('M'); ylabel('rechazo, peor punto de 800-900 Hz [dB]'); title('Rechazo vs M');
subplot(1,3,2); idx = ismember(Ms, [64 132 256]);
Hs = zeros(3, numel(fgrid)); jj = 0;
for M = [64 132 256], jj = jj + 1; h = design_fir_multiband(F, Fs, M, B); Hs(jj,:) = 20*log10(abs(fir_response(h, wg)) + 1e-12); end
plot(fgrid, Hs.', 'LineWidth', 1.1); grid on; ylim([-80 5]); xline(F, 'r:'); xline([fstop(1) fstop(end)], 'k:');
xlabel('f [Hz]'); ylabel('|H| [dB]'); legend('M=64', 'M=132', 'M=256', 'Location', 'south'); title('Transicion segun M (negro: 800-900 Hz)');
subplot(1,3,3); yyaxis left; plot(Ms, K*Fs./Ms, 'o-'); ylabel('df = 3.3 Fs/M  [Hz]'); yline(g, 'r--', 'g'); yline(g/2, 'g--', 'g/2');
yyaxis right; plot(Ms, gdl, 's-'); ylabel('retardo M/(2Fs) [ms]'); grid on; xlabel('M'); title('Transicion vs retardo');
exportgraphics(fig, fullfile(here, 'out', 'm7_orden_M.png'), 'Resolution', 130);
