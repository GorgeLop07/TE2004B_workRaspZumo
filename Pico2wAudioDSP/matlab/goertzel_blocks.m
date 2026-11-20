function [mag, praw] = goertzel_blocks(y, xraw, tones, Fs, N, explicit)
%GOERTZEL_BLOCKS  Amplitud de cada tono por bloques de N muestras, ventana Hann (REQ-AUD-07).
%   Goertzel:  s0 = xw + c s1 - s2 ,  c = 2 cos(2 pi f/Fs)
%              |X|^2 = s1^2 + s2^2 - c s1 s2     (magnitud del bin DFT a la frecuencia f)
%   Hann: suma de la ventana = N/2  =>  A = 4|X|/N.
%   explicit = true  -> recursion de Goertzel muestra a muestra (como en la Pico)
%   explicit = false -> el mismo bin DFT calculado como producto punto (equivalente, mas rapido)
%   praw: potencia media de xraw (senal sin filtrar) por bloque, para estimar la SNR.
    if nargin < 6, explicit = false; end
    nb  = floor(numel(y)/N);
    Y   = reshape(y(1:nb*N), N, nb);
    Xr  = reshape(xraw(1:nb*N), N, nb);
    win = 0.5*(1 - cos(2*pi*(0:N-1)'/N));
    mag = zeros(numel(tones), nb);
    for t = 1:numel(tones)
        w = 2*pi*tones(t)/Fs;
        if explicit
            c = 2*cos(w);
            for b = 1:nb
                s1 = 0; s2 = 0;
                for n = 1:N
                    s0 = Y(n,b)*win(n) + c*s1 - s2;  s2 = s1;  s1 = s0;
                end
                mag(t,b) = 4*sqrt(max(s1^2 + s2^2 - c*s1*s2, 0))/N;
            end
        else
            e = exp(-1j*w*(0:N-1)');
            mag(t,:) = 4*abs(((Y .* win).' * e)).'/N;
        end
    end
    praw = mean(Xr.^2, 1);
end
