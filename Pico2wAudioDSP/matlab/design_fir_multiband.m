function [h, G] = design_fir_multiband(tones, Fs, M, halfbw)
%DESIGN_FIR_MULTIBAND  FIR multibanda por el metodo de la ventana (REQ-AUD-04).
%   Para cada tono se toma la respuesta al impulso ideal de una banda [w1,w2]
%   retardada M/2 (fase lineal), se enventana con Hamming y se SUMAN las bandas:
%        h_k[n] = ( sin(w2 m) - sin(w1 m) ) / (pi m),   m = n - M/2
%        h_k[M/2] = (w2 - w1)/pi
%        h[n]  = sum_k  h_k[n] w[n] / G_k ,   G_k = |H_k(e^{j w_k})|
%   G_k normaliza a ganancia 1 en el centro de cada banda.
    n = 0:M;  m = n - M/2;
    w = 0.54 - 0.46*cos(2*pi*n/M);                 % Hamming
    h = zeros(1, M+1);  G = zeros(size(tones));
    for k = 1:numel(tones)
        w1 = 2*pi*(tones(k) - halfbw)/Fs;
        w2 = 2*pi*(tones(k) + halfbw)/Fs;
        hk = (sin(w2*m) - sin(w1*m)) ./ (pi*m);
        hk(m == 0) = (w2 - w1)/pi;                 % limite en m = 0
        hk = hk .* w;
        G(k) = abs(sum(hk .* exp(-1j*2*pi*tones(k)/Fs*n)));
        h = h + hk / G(k);
    end
end
