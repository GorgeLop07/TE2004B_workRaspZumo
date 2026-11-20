function NZ = load_noisex(Fs, noiseDir, seconds)
%LOAD_NOISEX  Carga dos ruidos industriales de NoiseX-92, a Fs y con RMS unitario (REQ-AUD-03 b).
%   Usa factory1.wav y volvo.wav (maquinaria y cabina de vehiculo). Por velocidad se toman
%   `seconds` s del centro de cada archivo (por defecto 60 s).
    here = fileparts(mfilename('fullpath'));
    if nargin < 2 || isempty(noiseDir), noiseDir = fullfile(here, '..', 'host', 'noise'); end
    if nargin < 3, seconds = 60; end
    names = {'factory1.wav', 'volvo.wav'};
    NZ.a = [];  NZ.b = [];  NZ.names = names;
    for i = 1:2
        [w, sr] = audioread(fullfile(noiseDir, names{i}));
        w = w(:,1);  nseg = min(numel(w), round(seconds*sr));
        c0 = floor((numel(w)-nseg)/2) + 1;
        w = w(c0:c0+nseg-1);
        y = resample_sinc(w, sr, Fs);
        y = (y - mean(y))/sqrt(mean((y-mean(y)).^2));          % RMS unitario
        if i == 1, NZ.a = y; else, NZ.b = y; end
    end
end
