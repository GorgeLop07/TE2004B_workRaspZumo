function D = design_all(P)
%DESIGN_ALL  Disena el FIR y el banco IIR con los parametros de dsp_config.h.
    [D.h, D.G] = design_fir_multiband(P.tones, P.Fs, P.firM, P.firHalfBW);
    D.iir = cell(1, numel(P.tones));  D.sos = zeros(0,5);
    for k = 1:numel(P.tones)
        D.iir{k} = design_iir_bandpass(P.tones(k), P.iirBW, P.Fs, P.lpOrder);
        D.sos = [D.sos; D.iir{k}.sos];
    end
end
