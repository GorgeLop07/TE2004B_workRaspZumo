function n = gen_noise(len, kind, NZ)
%GEN_NOISE  Ruido de RMS unitario.  kind 0: AWGN.  1: (AWGN + factory1 + volvo)/sqrt(3).  2: (factory1 + volvo)/sqrt(2).
%   Cada componente tiene la misma potencia; los NoiseX arrancan en un punto aleatorio (circular).
    g = randn(len, 1);
    if kind == 0, n = g.'; return; end
    ia = mod(randi(numel(NZ.a)) + (0:len-1)', numel(NZ.a)) + 1;
    ib = mod(randi(numel(NZ.b)) + (0:len-1)', numel(NZ.b)) + 1;
    a = NZ.a(ia);  b = NZ.b(ib);
    if kind == 1, n = ((g + a + b)/sqrt(3)).'; else, n = ((a + b)/sqrt(2)).'; end
end
