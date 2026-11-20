function R = stress_test(nTrials, kinds)
%STRESS_TEST  Curva de estres SNR vs tasa de acierto (Entregable 3), igual a host/bench.cpp.
%   R = stress_test(200)           % 200 pruebas por punto, 3 escenarios
%   Mide aciertos para: sin filtro / FIR / IIR, con ruido AWGN, AWGN+NoiseX y NoiseX.
%   Los generadores aleatorios difieren de los de C, asi que los porcentajes coinciden en lo
%   estadistico, no dato por dato.
    if nargin < 1, nTrials = 100; end
    if nargin < 2, kinds = [0 1 2]; end
    P = dsp_params();  D = design_all(P);  NZ = load_noisex(P.Fs);  rng(12345);
    snrs = [10 5 0 -5 -8 -10 -12 -15];  flds = {'raw', 'fir', 'iir'};  nmK = {'AWGN', 'AWGN+NoiseX', 'NoiseX'};
    R.snrs = snrs;  R.rate = zeros(numel(kinds), 3, numel(snrs));  R.wrong = R.rate;
    for ki = 1:numel(kinds)
        kind = kinds(ki);  thr = calibrate_thr(P, D, NZ, kind);
        fprintf('\nEscenario %s   (%d pruebas por punto)\n  SNR[dB] |  sin filtro   FIR    IIR   (aciertos %%)\n', nmK{kind+1}, nTrials);
        for si = 1:numel(snrs)
            ok = zeros(1,3);  bad = zeros(1,3);
            for tr = 1:nTrials
                mode = 1 + mod(tr,2);                                % campo 1 o 2 alternados
                onset = 20*P.N + randi(P.N) - 1;  total = onset + 24*P.N;
                A = sqrt(2*10^(snrs(si)/10))*P.sigmaN;
                nz = P.sigmaN*gen_noise(total, kind, NZ);
                i0 = (0:total-1);
                x  = P.dcBias + nz + A*sin(2*pi*P.tones(mode)*i0/P.Fs + 2*pi*rand).*(i0 >= onset);
                C  = run_chains(quant12(x), P, D);
                for c = 1:3
                    [mag, praw] = goertzel_blocks(C.(flds{c}), C.raw, P.tones, P.Fs, P.N);
                    ev = detector_run(mag, praw, P, thr.(flds{c}));
                    ev = ev(ev(:,1)*P.N - 1 >= onset + P.N, :);      % eventos antes del tono = falsa alarma, no cuentan
                    if ~isempty(ev), if ev(1,2) == mode, ok(c) = ok(c) + 1; else, bad(c) = bad(c) + 1; end, end
                end
            end
            R.rate(ki,:,si) = 100*ok/nTrials;  R.wrong(ki,:,si) = 100*bad/nTrials;
            fprintf('  %+6.0f  |   %6.1f   %6.1f %6.1f\n', snrs(si), R.rate(ki,:,si));
        end
    end
    fig = figure('Position', [50 50 1300 420]);
    for ki = 1:numel(kinds)
        subplot(1, numel(kinds), ki);
        plot(snrs, squeeze(R.rate(ki,1,:)), 'o-', snrs, squeeze(R.rate(ki,2,:)), 'o-', snrs, squeeze(R.rate(ki,3,:)), 'o-', 'LineWidth', 1.2);
        grid on; set(gca, 'XDir', 'reverse'); ylim([-3 103]); xlabel('SNR de entrada [dB]'); title(nmK{kinds(ki)+1});
        if ki == 1, ylabel('Comandos correctos [%]'); legend('sin filtro', 'FIR', 'IIR', 'Location', 'southwest'); end
    end
    here = fileparts(mfilename('fullpath'));
    exportgraphics(fig, fullfile(here, 'out', 'm6_estres_snr.png'), 'Resolution', 130);
end
