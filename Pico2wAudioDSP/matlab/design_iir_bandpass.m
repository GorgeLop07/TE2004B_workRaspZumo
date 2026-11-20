function D = design_iir_bandpass(f0, bw, Fs, N)
%DESIGN_IIR_BANDPASS  Pasabanda IIR desde prototipo analogico (REQ-AUD-05).
%   1) Butterworth pasabajas, Omega_c = 1:  p_i = exp(j*pi*(2i+N-1)/(2N))
%   2) Prewarping de los bordes -3 dB:       Omega = (2/Ts) tan(w/2)
%        Omega0^2 = Omega1*Omega2 ,  B = Omega2 - Omega1
%   3) Pasabajas -> pasabanda  s -> (s^2 + Omega0^2)/(B s):
%        cada polo p da dos polos, raices de  s^2 - p B s + Omega0^2 = 0
%   4) Bilineal  z = (1 + s Ts/2)/(1 - s Ts/2).  s=0 -> z=1, s=inf -> z=-1,
%      asi que cada biquad queda  g (1 - z^-2) / (1 + a1 z^-1 + a2 z^-2)
%   5) Ganancia: |H(e^{j w0})| = 1 en el tono.
%   D.sos = [b0 b1 b2 a1 a2] por fila (N biquads en cascada).
    Ts = 1/Fs;
    w1 = 2*pi*(f0 - bw/2)/Fs;   w2 = 2*pi*(f0 + bw/2)/Fs;
    O1 = (2/Ts)*tan(w1/2);      O2 = (2/Ts)*tan(w2/2);
    B  = O2 - O1;               O0 = sqrt(O1*O2);
    i  = 1:N;
    D.lpPoles = exp(1j*pi*(2*i + N - 1)/(2*N));
    D.omega1 = O1;  D.omega2 = O2;  D.B = B;  D.omega0 = O0;
    D.w0digital = 2*atan(O0*Ts/2) * Fs/(2*pi);       % [Hz] donde cae realmente el centro

    sos = zeros(0,5);  D.poles = [];  D.zeros = [];
    for p = D.lpPoles
        if imag(p) < -1e-12, continue; end           % su conjugado ya se cubre
        disc = sqrt((p*B)^2 - 4*O0^2);
        if abs(imag(p)) < 1e-12                       % polo real -> un solo biquad
            sa = (p*B + disc)/2;  if imag(sa) < 0, sa = (p*B - disc)/2; end
            roots_s = sa;
        else                                          % par complejo -> dos biquads
            roots_s = [(p*B + disc)/2, (p*B - disc)/2];
        end
        for s = roots_s
            zp = (1 + s*Ts/2)/(1 - s*Ts/2);
            sos(end+1,:) = [1 0 -1 -2*real(zp) abs(zp)^2];            %#ok<AGROW>
            D.poles = [D.poles, zp, conj(zp)];
            D.zeros = [D.zeros, 1, -1];
        end
    end
    g   = abs(sos_response(sos, 2*pi*f0/Fs));
    per = (1/g)^(1/size(sos,1));                       % reparte la ganancia entre secciones
    sos(:,1:3) = sos(:,1:3) * per;
    D.sos = sos;
    D.maxPoleRadius = max(abs(D.poles));
end
