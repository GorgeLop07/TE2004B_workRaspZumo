function [f, psd] = welch_psd(x, Fs, skip)
%WELCH_PSD  PSD de Welch hecha a mano (Hann 2048, 50 % de traslape, FFT). Potencia lineal por Hz.
    if nargin < 3, skip = 0; end
    N = 2048;  w = (0.5 - 0.5*cos(2*pi*(0:N-1)'/N));  wp = sum(w.^2);
    x = x(:);  acc = zeros(N/2+1, 1);  cnt = 0;
    for s = skip+1 : N/2 : numel(x)-N+1
        seg = x(s:s+N-1);  seg = (seg - mean(seg)).*w;
        X = fft(seg);
        acc = acc + abs(X(1:N/2+1)).^2/(wp*Fs);
        cnt = cnt + 1;
    end
    psd = acc/cnt;  f = (0:N/2)'*Fs/N;
end
