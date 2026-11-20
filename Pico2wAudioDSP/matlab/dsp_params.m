function P = dsp_params()
%DSP_PARAMS  Parametros del modulo DSP, leidos de ../dsp/dsp_config.h.
%   Asi MATLAB y la Pico nunca se desincronizan: si cambias un tono o un orden
%   en dsp_config.h, aqui cambia solo.
    here = fileparts(mfilename('fullpath'));
    txt  = fileread(fullfile(here, '..', 'dsp', 'dsp_config.h'));
    num  = @(name) str2double(regexp(txt, ['#define\s+' name '\s+\(?\s*(-?[0-9.]+)f?'], 'tokens', 'once'));
    P.Fs        = num('DSP_FS_HZ');
    tok         = regexp(txt, 'DSP_TONE_HZ_INIT\s*\{([^}]*)\}', 'tokens', 'once');
    P.tones     = str2double(strsplit(strtrim(tok{1}), ','));          % [700 1000]
    P.firM      = num('DSP_FIR_M');
    P.firHalfBW = num('DSP_FIR_HALFBW_HZ');
    P.lpOrder   = num('DSP_IIR_LP_ORDER');
    P.iirBW     = num('DSP_IIR_BW_HZ');
    P.dcR       = num('DSP_DC_R');
    P.N         = num('DSP_GOERTZEL_N');
    P.calBlocks = num('DSP_CAL_BLOCKS');
    P.thrK      = num('DSP_THR_K');
    P.ampFloor  = num('DSP_AMP_FLOOR');
    P.ambig     = num('DSP_AMBIG_RATIO');
    P.snrMinDb  = num('DSP_SNR_MIN_DB');
    P.minBlocks = num('DSP_MIN_BLOCKS');
    P.holdoff   = num('DSP_HOLDOFF_BLOCKS');
    % Condiciones del banco de pruebas (iguales a host/bench.cpp)
    P.sigmaN    = 0.10;      % RMS del ruido ambiente (escala completa = 1)
    P.dcBias    = 0.03;      % polarizacion tipo Vcc/2 del MAX4466
end
