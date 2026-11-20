function [st, ev, reason, snr] = detector_step(st, mag, praw, P)
%DETECTOR_STEP  Version por bloques (con estado) de detector_run.m / dsp_det_update() de la Pico.
%   st = detector_step([], [], [], P) devuelve el estado inicial (calibrando P.calBlocks bloques).
%   Luego, por cada bloque:  [st, ev, reason, snr] = detector_step(st, mag, praw, P)
%   ev = [campo amplitud snr_dB] cuando se acepta un comando (si no, []).
%   reason: 0 ok, 1 bajo umbral, 2 ambiguo, 3 SNR baja, 4 calibrando, 5 reposo.
    if isempty(st)
        st = struct('calLeft', P.calBlocks, 'cal', zeros(numel(P.tones), 0), 'thr', inf(numel(P.tones),1), ...
                    'runField', 0, 'runLen', 0, 'runMag', 0, 'runSnr', 0, 'hold', 0);
        ev = []; reason = 4; snr = nan; return;
    end
    ev = []; snr = nan; nt = numel(mag);
    if st.calLeft > 0
        st.cal(:, end+1) = mag(:);  st.calLeft = st.calLeft - 1;
        if st.calLeft == 0
            st.thr = max(P.ampFloor, mean(st.cal, 2) + P.thrK*std(st.cal, 0, 2));
        end
        reason = 4; return;
    end
    if st.hold > 0, st.hold = st.hold - 1; reason = 5; return; end
    [m, best] = max(mag);
    other = max([0; mag([1:best-1, best+1:nt]).']);
    ptone = 0.5*m^2;  pnoise = praw - ptone;
    if pnoise <= 1e-12, snr = 99; else, snr = 10*log10(ptone/pnoise + 1e-12); end
    reason = 0;
    if     m < st.thr(best),      reason = 1;
    elseif other >= P.ambig*m,    reason = 2;
    elseif snr < P.snrMinDb,      reason = 3;
    end
    if reason ~= 0, st.runField = 0; st.runLen = 0; return; end
    if st.runField == best, st.runLen = st.runLen + 1; st.runMag = st.runMag + m; st.runSnr = st.runSnr + snr;
    else, st.runField = best; st.runLen = 1; st.runMag = m; st.runSnr = snr; end
    if st.runLen >= P.minBlocks
        ev = [st.runField, st.runMag/st.runLen, st.runSnr/st.runLen];
        st.runField = 0; st.runLen = 0; st.hold = P.holdoff;
    end
end
