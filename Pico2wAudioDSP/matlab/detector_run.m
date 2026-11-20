function [events, info] = detector_run(mag, praw, P, thr)
%DETECTOR_RUN  Validacion de comandos (REQ-AUD-08), identica a dsp_det_update() de la Pico.
%   mag  : [ntones x nblocks] amplitudes de Goertzel;  praw: [1 x nblocks] potencia sin filtrar.
%   thr  : umbrales por tono (si se omite, se calibran con los primeros P.calBlocks bloques).
%   Un bloque es valido si:  amplitud >= umbral  Y  el otro tono < ambig*mayor  Y  SNR >= snrMin.
%   Se acepta un comando tras P.minBlocks bloques validos CONSECUTIVOS del mismo tono;
%   luego se ignora P.holdoff bloques (reposo).
%   events: [bloque campo amplitud snr_dB];  info.reason(b) = 0 ok, 1 bajo umbral,
%   2 ambiguo, 3 SNR baja, 4 calibrando, 5 reposo.
    nb = size(mag,2);  nt = size(mag,1);
    reason = zeros(1,nb);  events = zeros(0,4);  start = 1;
    if nargin < 4 || isempty(thr)
        cal = mag(:, 1:P.calBlocks);
        thr = max(P.ampFloor, mean(cal,2) + P.thrK*std(cal,0,2));     % media + K sigma (std con n-1, como Welford)
        reason(1:P.calBlocks) = 4;  start = P.calBlocks + 1;
    end
    runField = 0; runLen = 0; runMag = 0; runSnr = 0; hold = 0;
    snrAll = nan(1,nb);
    for b = start:nb
        if hold > 0, hold = hold - 1; reason(b) = 5; continue; end
        [m, best] = max(mag(:,b));
        other = max([0; mag([1:best-1, best+1:nt], b)]);
        ptone = 0.5*m^2;  pnoise = praw(b) - ptone;
        if pnoise <= 1e-12, snr = 99; else, snr = 10*log10(ptone/pnoise + 1e-12); end
        snrAll(b) = snr;
        if     m < thr(best),            reason(b) = 1;
        elseif other >= P.ambig*m,       reason(b) = 2;
        elseif snr < P.snrMinDb,         reason(b) = 3;
        end
        if reason(b) ~= 0, runField = 0; runLen = 0; continue; end
        if runField == best, runLen = runLen + 1; runMag = runMag + m; runSnr = runSnr + snr;
        else, runField = best; runLen = 1; runMag = m; runSnr = snr; end
        if runLen >= P.minBlocks
            events(end+1,:) = [b, runField, runMag/runLen, runSnr/runLen];   %#ok<AGROW>
            runField = 0; runLen = 0; hold = P.holdoff;
        end
    end
    info.thr = thr;  info.reason = reason;  info.snr = snrAll;
end
