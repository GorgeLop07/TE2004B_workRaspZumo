classdef LiveEngine < handle
%LIVEENGINE  Motor DSP por bloques de 50 ms (400 muestras a 8 kHz) para la demo en tiempo real.
%   Cada step() genera el bloque que "captaria" el ADC (tonos + AWGN + 2 NoiseX-92 segun los controles),
%   lo pasa por el bloqueo DC, el FIR multibanda y el banco IIR (con estado entre bloques), calcula
%   Goertzel y la validacion, y devuelve el audio a escuchar. Es la misma cadena de la Pico.
    properties
        P; D; NZ;
        ctrl = struct('tone', 'off', 'snr', 0, 'noise', [true true true], 'listen', 'fir', 'quit', false);
        n0 = 0;                     % indice absoluto de muestra (fase continua de los tonos)
        amp = [0 0];                % amplitud actual de cada tono (rampa para evitar clicks)
        ia = 1; ib = 1;             % punteros circulares a los NoiseX
        zdc = 0; zfir; ziir;        % estados de los filtros
        det;                        % detector por cadena (struct array 1x3)
        warm = 20;                  % bloques de asentamiento antes de calibrar
        nblk = 0;
        ring;                       % bufers de 2048 muestras: xd, fir, iir (para el espectro)
        lastMag = zeros(2,3); lastPraw = 0; lastReason = zeros(1,3); lastSnr = nan(1,3);
        evlog = {};                 % {t, cadena, campo, amp, snr}
        calibrated = false;
    end
    methods
        function o = LiveEngine(P, D, NZ)
            o.P = P; o.D = D; o.NZ = NZ;
            o.zfir = zeros(numel(D.h)-1, 1);
            o.ziir = cell(size(D.sos,1), 1);  for i = 1:size(D.sos,1), o.ziir{i} = zeros(2,1); end
            o.ia = randi(numel(NZ.a)); o.ib = randi(numel(NZ.b));
            o.ring = zeros(3, 2048);
            o.recalibrate();
        end
        function recalibrate(o)
            for c = 1:3, o.det(c).st = detector_step([], [], [], o.P); end
            o.warm = 20; o.calibrated = false;
        end
        function setCtrl(o, name, val), o.ctrl.(name) = val; end

        function [audio, info] = step(o)
            P = o.P; N = P.N; Fs = P.Fs;  k = (0:N-1);
            % ---- ruido: AWGN + NoiseX (los que esten activados), potencias iguales, RMS unitario ----
            comps = zeros(3, N);
            comps(1,:) = randn(1, N);
            comps(2,:) = o.NZ.a(mod(o.ia + k - 1, numel(o.NZ.a)) + 1).';
            comps(3,:) = o.NZ.b(mod(o.ib + k - 1, numel(o.NZ.b)) + 1).';
            o.ia = mod(o.ia + N - 1, numel(o.NZ.a)) + 1;  o.ib = mod(o.ib + N - 1, numel(o.NZ.b)) + 1;
            on = o.ctrl.noise;  if ~any(on), on = [true false false]; end
            noise = P.sigmaN * sum(comps(on,:), 1) / sqrt(nnz(on));
            % ---- tonos con rampa lineal de amplitud (sin clicks al cambiar) ----
            A = sqrt(2*10^(o.ctrl.snr/10)) * P.sigmaN;
            tgt = [0 0];
            if o.calibrated                                  % durante la calibracion no se permiten tonos
                switch o.ctrl.tone
                    case 'A', tgt = [A 0];
                    case 'B', tgt = [0 A];
                    case 'both', tgt = [A A]/sqrt(2);
                end
            end
            tone = zeros(1, N);
            for t = 1:2
                env = o.amp(t) + (tgt(t) - o.amp(t))*(k + 1)/N;
                tone = tone + env .* sin(2*pi*P.tones(t)*(o.n0 + k)/Fs);
            end
            o.amp = tgt;  o.n0 = o.n0 + N;
            x  = quant12(P.dcBias + noise + tone);             % lo que veria el ADC de 12 bits
            % ---- cadena DSP ----
            [xd, o.zdc] = filter([1 -1], [1 -P.dcR], x, o.zdc);
            [yf, o.zfir] = filter(o.D.h, 1, xd, o.zfir);
            yi = zeros(1, N);
            for t = 1:numel(P.tones)
                v = xd;
                for q = 1:P.lpOrder
                    i = (t-1)*P.lpOrder + q;  c = o.D.sos(i, :);
                    [v, o.ziir{i}] = filter(c(1:3), [1 c(4:5)], v, o.ziir{i});
                end
                yi = yi + v;
            end
            chains = {xd, yf, yi};
            for c = 1:3, o.ring(c, :) = [o.ring(c, N+1:end), chains{c}]; end
            % ---- Goertzel + validacion por cadena ----
            o.nblk = o.nblk + 1;  events = [];  evChain = [];
            for c = 1:3
                [mag, praw] = goertzel_blocks(chains{c}, xd, P.tones, Fs, N);
                o.lastMag(:, c) = mag;  o.lastPraw = praw;
                if o.warm > 0, continue; end
                [o.det(c).st, ev, o.lastReason(c), o.lastSnr(c)] = detector_step(o.det(c).st, mag, praw, P);
                if ~isempty(ev), events(end+1, :) = ev; evChain(end+1) = c; end %#ok<AGROW>
            end
            if o.warm > 0, o.warm = o.warm - 1; end
            o.calibrated = o.warm == 0 && o.det(1).st.calLeft == 0;
            tnow = o.nblk*N/Fs;
            for e = 1:size(events,1), o.evlog(end+1, :) = {tnow, evChain(e), events(e,1), events(e,2), events(e,3)}; end
            % ---- audio a escuchar ----
            clean = tone;
            switch o.ctrl.listen
                case 'clean', audio = clean;
                case 'noisy', audio = xd;
                case 'iir',   audio = yi;
                otherwise,    audio = yf;
            end
            info.events = events;  info.evChain = evChain;  info.t = tnow;
            info.tone = o.ctrl.tone;  info.clean = clean;  info.x = xd;  info.fir = yf;  info.iir = yi;
        end
    end
end
