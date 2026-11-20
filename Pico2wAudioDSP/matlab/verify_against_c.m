%% VERIFY_AGAINST_C  Comprueba que el diseno de MATLAB == los coeficientes que se compilan en la Pico.
%  Lee ../dsp/dsp_coeffs.h (generado por host/gen_coeffs.cpp) y compara; tambien compara la respuesta
%  en frecuencia con host/out/response.csv si existe. Debe dar diferencias ~1e-7 (float32 vs double).
here = fileparts(mfilename('fullpath'));
P = dsp_params();  D = design_all(P);
txt = fileread(fullfile(here, '..', 'dsp', 'dsp_coeffs.h'));
nums = @(s) str2double(regexp(s, '[-+]?\d+\.?\d*(?:e[-+]?\d+)?(?=f)', 'match'));

blk = regexp(txt, 'dsp_fir_h\[DSP_FIR_TAPS\]\s*=\s*\{(.*?)\};', 'tokens', 'once');
hC  = nums(blk{1});
blk = regexp(txt, 'dsp_iir_sos\[[^\]]*\]\[5\]\s*=\s*\{(.*?)\n\};', 'tokens', 'once');
sosC = reshape(nums(blk{1}), 5, []).';

eh = max(abs(hC(:) - D.h(:)));   es = max(abs(sosC(:) - D.sos(:)));
fprintf('FIR : %d coeficientes   max|h_C - h_MATLAB|   = %.2e\n', numel(hC), eh);
fprintf('IIR : %d biquads       max|sos_C - sos_MATLAB| = %.2e\n', size(sosC,1), es);
assert(eh < 1e-6 && es < 1e-6, 'El diseno de MATLAB NO coincide con el de la Pico');

rf = fullfile(here, '..', 'host', 'out', 'response.csv');
if exist(rf, 'file')
    T = readmatrix(rf, 'NumHeaderLines', 1);
    w  = 2*pi*T(:,1).'/P.Fs;                                       % fila
    Hf = 20*log10(abs(fir_response(D.h, w)) + 1e-30);
    Hi = zeros(size(w));  for k = 1:numel(P.tones), Hi = Hi + sos_response(D.iir{k}.sos, w); end
    Hi = 20*log10(abs(Hi) + 1e-30);
    kf = T(:,2).' > -90;  ki = T(:,3).' > -90;                     % bajo -90 dB manda el ruido de float32 de C
    df = max(abs(Hf(kf) - T(kf,2).'));  di = max(abs(Hi(ki) - T(ki,3).'));
    fprintf('Respuesta en frecuencia vs response.csv (donde |H| > -90 dB): FIR %.4f dB, IIR %.4f dB (diferencia maxima)\n', df, di);
    assert(df < 0.05 && di < 0.05, 'La respuesta en frecuencia no coincide con la de C');
end
disp('OK: MATLAB y la Pico usan los mismos filtros.');
