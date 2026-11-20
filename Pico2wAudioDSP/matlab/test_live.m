%% TEST_LIVE  Prueba sin pantalla ni audio de la logica en tiempo real.
%  1) detector_step == detector_run  (mismas decisiones)
%  2) guion: calibracion -> tono A -> silencio -> tono B -> A+B (debe rechazar) ; mide el tiempo por bloque
clear; close all; rng(3);
P = dsp_params();  D = design_all(P);  NZ = load_noisex(P.Fs);

% --- 1) equivalencia por bloques vs por lote ---
len = 160*P.N;  t = (0:len-1)/P.Fs;  A = 0.3;
x = quant12(P.dcBias + P.sigmaN*gen_noise(len, 1, NZ) + A*sin(2*pi*700*t).*(t > 4 & t < 6) + A*sin(2*pi*1000*t).*(t > 9 & t < 11));
C = run_chains(x, P, D);  [mag, praw] = goertzel_blocks(C.fir, C.raw, P.tones, P.Fs, P.N);
[evB, ~] = detector_run(mag, praw, P);
st = detector_step([], [], [], P);  evS = zeros(0, 4);
for b = 1:size(mag, 2)
    [st, ev] = detector_step(st, mag(:, b), praw(b), P);
    if ~isempty(ev), evS(end+1, :) = [b, ev]; end %#ok<SAGROW>
end
fprintf('detector_run : %s\ndetector_step: %s\n', mat2str(evB(:, 1:2).'), mat2str(evS(:, 1:2).'));
assert(isequal(evB(:, 1:2), evS(:, 1:2)), 'detector_step difiere de detector_run');
disp('OK: detector por bloques == detector por lote');

% --- 2) demo guionada ---
script = {0, 'off', 0; 3.7, 'A', 0; 6.0, 'off', 0; 7.0, 'B', 0; 9.5, 'off', 0; 10.5, 'both', 5; 13.0, 'off', 0; 14.0, 'A', -12; 16.0, 'off', 0};
eng = live_demo('NoAudio', true, 'NoFigure', true, 'Script', script, 'Duration', 17, 'Listen', 'fir');
ev = eng.evlog;
fprintf('\nEventos del motor (t, cadena 1=sin filtro 2=FIR 3=IIR, campo):\n'); disp(ev(:, [1 2 3]));
for c = 1:3
    f = cell2mat(ev(cell2mat(ev(:,2)) == c, 3)).';  fprintf('cadena %d -> campos %s\n', c, mat2str(f));
end
