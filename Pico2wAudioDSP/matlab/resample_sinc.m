function y = resample_sinc(x, srIn, srOut)
%RESAMPLE_SINC  Remuestreo por sinc enventanada (Blackman), escrito a mano.
%   Pasabajas a 0.45*srOut (anti-aliasing) evaluado en los instantes de salida:
%       y(m) = sum_n x(n) * 2 fc sinc(2 fc (t_m - n)) * w((t_m - n)/L),  t_m = m*srIn/srOut
%   (se evita resample() de MATLAB porque disena un filtro internamente.)
    x = x(:);  ratio = srIn/srOut;
    fc = 0.45/ratio;                          % ciclos / muestra de entrada
    L  = ceil(16/(2*fc));                     % 16 cruces por cero a cada lado
    M  = floor((numel(x)-1)/ratio);
    y  = zeros(M,1);  chunk = 20000;  j = -L+1:L;
    for s = 1:chunk:M
        m  = (s:min(s+chunk-1, M))' - 1;
        t  = m*ratio;
        n  = floor(t) + j;                    % [chunk x 2L] muestras de entrada vecinas
        u  = t - n;
        a  = 2*pi*fc*u;
        sn = ones(size(a));  nz = abs(a) > 1e-12;  sn(nz) = sin(a(nz))./a(nz);
        w  = 0.42 + 0.5*cos(pi*u/L) + 0.08*cos(2*pi*u/L);
        valid = n >= 0 & n <= numel(x)-1;
        n(~valid) = 0;
        K = 2*fc*sn.*w.*valid;
        y(m+1) = sum(K .* x(n+1), 2);
    end
end
