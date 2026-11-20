function eng = live_demo(varargin)
%LIVE_DEMO  Demostracion EN TIEMPO REAL: escuchas los tonos mientras se contaminan y se filtran.
%
%   >> live_demo                 % interfaz + audio por la salida predeterminada
%
%   Cada 50 ms (un bloque de Goertzel) el programa genera lo que captaria el microfono:
%       tono(s) de comando  +  AWGN  +  NoiseX-92 'factory1'  +  NoiseX-92 'volvo'     (REQ-AUD-03)
%   lo pasa por bloqueo DC -> FIR multibanda / banco IIR -> Goertzel -> validacion, y reproduce
%   el audio que elijas en "Escuchar":
%       Tono limpio     el tono solo (referencia: como "deberia" sonar)
%       Contaminada     lo que ve el filtro: tono + AWGN + 2 ruidos NoiseX-92
%       Salida FIR      lo que queda despues del FIR multibanda (el ruido casi desaparece)
%       Salida IIR      idem con el banco IIR
%   Con SNR bajo, el tono se oye "enterrado" en "Contaminada" y reaparece en "Salida FIR".
%
%   Opciones (pares nombre-valor):
%     'Listen'    'fir' (por defecto) | 'clean' | 'noisy' | 'iir'
%     'AudioFs'   frecuencia de la tarjeta de audio, 48000 por defecto (se interpola 8 kHz -> AudioFs)
%     'NoAudio'   true para correr sin sonido (solo pantalla)
%     'Duration'  segundos (Inf por defecto; cierra la ventana o pulsa Salir para terminar)
%     'Script'    {t_s, 'off'|'A'|'B'|'both', snr_dB; ...}  guion automatico (para pruebas sin manos)
%     'NoFigure'  true para no abrir ventana (modo prueba)
%     'SaveWav'   ruta .wav para guardar lo que sono
%     'Snapshot'  ruta .png: guarda una captura de la ventana al terminar
%
%   CUIDADO CON EL VOLUMEN: empieza bajo; el ruido NoiseX puede sonar fuerte.
    ip = inputParser;
    addParameter(ip, 'Listen', 'fir');   addParameter(ip, 'AudioFs', 48000);
    addParameter(ip, 'NoAudio', false);  addParameter(ip, 'Duration', Inf);
    addParameter(ip, 'Script', {});      addParameter(ip, 'NoFigure', false);
    addParameter(ip, 'SaveWav', '');     addParameter(ip, 'Visible', true);   addParameter(ip, 'Snapshot', '');
    parse(ip, varargin{:});  o = ip.Results;

    P = dsp_params();  D = design_all(P);
    fprintf('Cargando NoiseX-92 (factory1, volvo)...\n');
    NZ = load_noisex(P.Fs);
    eng = LiveEngine(P, D, NZ);  eng.setCtrl('listen', o.Listen);
    N = P.N;  tblock = N/P.Fs;

    % ---------------- audio ----------------
    adw = [];  audioOK = false;  vol = 0.35;  up = round(o.AudioFs/P.Fs);
    if ~o.NoAudio
        try
            adw = audioDeviceWriter('SampleRate', P.Fs*up);  audioOK = true;
            fprintf('Audio: %d Hz (8 kHz interpolado x%d). Si no oyes nada, revisa la salida del sistema.\n', P.Fs*up, up);
        catch err
            fprintf('No pude abrir el audio (%s). Sigo sin sonido.\n', err.message);
        end
    end
    prev = 0;  played = [];  underruns = 0;

    % ---------------- interfaz ----------------
    hasFig = ~o.NoFigure;  fig = []; H = struct();
    if hasFig
        fig = figure('Name', 'Demo DSP en tiempo real', 'NumberTitle', 'off', 'Position', [60 40 1250 760], ...
                     'Visible', ternary(o.Visible, 'on', 'off'), 'CloseRequestFcn', @(~,~) eng.setCtrl('quit', true));
        H.axSpec = axes(fig, 'Position', [0.05 0.56 0.60 0.38]);  H.axBar = axes(fig, 'Position', [0.05 0.09 0.28 0.37]);
        H.axWave = axes(fig, 'Position', [0.38 0.09 0.27 0.37]);
        fr = (0:1023)*P.Fs/2048;  H.fr = fr;  H.win = 0.5 - 0.5*cos(2*pi*(0:2047)/2048);
        H.lSpec = semilogy_like(H.axSpec, fr);
        H.lBar = bar(H.axBar, zeros(3, 2));  hold(H.axBar, 'on');
        H.lThr = plot(H.axBar, nan(3,2), nan(3,2), 'k_', 'MarkerSize', 22, 'LineWidth', 2);
        set(H.axBar, 'XTickLabel', {'Sin filtro', 'FIR', 'IIR'}); grid(H.axBar, 'on');
        legend(H.axBar, {sprintf('A_{%g Hz}', P.tones(1)), sprintf('A_{%g Hz}', P.tones(2))}, 'Location', 'northeast');
        title(H.axBar, 'Goertzel: amplitud por tono (rayas = umbral calibrado)');
        H.lWave = plot(H.axWave, (0:N*4-1)/P.Fs*1000, zeros(1, N*4)); grid(H.axWave, 'on'); xlabel(H.axWave, 't [ms]'); title(H.axWave, 'Lo que estas escuchando');
        H.txt = annotation(fig, 'textbox', [0.68 0.45 0.31 0.5], 'String', '', 'FontName', 'Consolas', 'FontSize', 9, 'EdgeColor', [.7 .7 .7], 'Interpreter', 'none', 'VerticalAlignment', 'top');
        % controles
        px = 0.68;  mk = @(varargin) uicontrol(fig, 'Units', 'normalized', varargin{:});
        mk('Style', 'text', 'Position', [px 0.405 0.3 0.025], 'String', 'Tono de comando (lo que "envia" el operador):', 'HorizontalAlignment', 'left');
        names = {'Silencio', 'A 700 Hz', 'B 1000 Hz', 'A+B (ambiguo)'};  keys = {'off', 'A', 'B', 'both'};
        for i = 1:4
            mk('Style', 'togglebutton', 'Position', [px + (i-1)*0.076 0.365 0.074 0.035], 'String', names{i}, 'Tag', ['tone_' keys{i}], ...
               'Value', i == 1, 'Callback', @(s,~) onTone(s, keys{i}));
        end
        mk('Style', 'text', 'Position', [px 0.325 0.3 0.025], 'String', 'SNR de banda ancha de los tonos [dB]:', 'HorizontalAlignment', 'left');
        H.snrLbl = mk('Style', 'text', 'Position', [px + 0.25 0.325 0.05 0.025], 'String', '0 dB');
        mk('Style', 'slider', 'Position', [px 0.30 0.30 0.025], 'Min', -15, 'Max', 15, 'Value', 0, 'SliderStep', [1/30 5/30], ...
           'Callback', @(s,~) onSnr(s));
        mk('Style', 'text', 'Position', [px 0.255 0.3 0.025], 'String', 'Fuentes de ruido (REQ-AUD-03):', 'HorizontalAlignment', 'left');
        nn = {'AWGN', 'NoiseX factory1', 'NoiseX volvo'};
        for i = 1:3
            mk('Style', 'checkbox', 'Position', [px + (i-1)*0.1 0.225 0.1 0.03], 'String', nn{i}, 'Value', 1, 'Callback', @(~,~) onNoise());
        end
        H.noiseBoxes = findobj(fig, 'Style', 'checkbox');  H.noiseBoxes = flipud(H.noiseBoxes);
        mk('Style', 'text', 'Position', [px 0.185 0.1 0.025], 'String', 'Escuchar:', 'HorizontalAlignment', 'left');
        lst = {'Salida FIR', 'Salida IIR', 'Contaminada (tono + 3 ruidos)', 'Tono limpio'};  lk = {'fir', 'iir', 'noisy', 'clean'};
        H.pop = mk('Style', 'popupmenu', 'Position', [px + 0.07 0.185 0.23 0.03], 'String', lst, 'Value', max(1, find(strcmp(lk, o.Listen), 1)), 'Callback', @(s,~) eng.setCtrl('listen', lk{s.Value}));
        mk('Style', 'text', 'Position', [px 0.145 0.1 0.025], 'String', 'Volumen:', 'HorizontalAlignment', 'left');
        mk('Style', 'slider', 'Position', [px + 0.07 0.147 0.23 0.025], 'Min', 0, 'Max', 1, 'Value', vol, 'Callback', @(s,~) setVol(s.Value));
        mk('Style', 'pushbutton', 'Position', [px 0.09 0.14 0.04], 'String', 'Recalibrar (sin tonos)', 'Callback', @(~,~) eng.recalibrate());
        mk('Style', 'pushbutton', 'Position', [px + 0.16 0.09 0.14 0.04], 'String', 'Salir', 'Callback', @(~,~) eng.setCtrl('quit', true));
    end

    % ---------------- bucle en tiempo real ----------------
    fprintf('Corriendo. Primero calibra ~%.0f s con SOLO ruido (no hagas nada); luego elige un tono.\n', (20 + P.calBlocks)*tblock);
    t0 = tic;  nb = 0;  busy = 0;  scriptIdx = 0;
    while ~eng.ctrl.quit && eng.nblk*tblock < o.Duration
        tb = tic;
        tnow = eng.nblk*tblock;
        while scriptIdx < size(o.Script, 1) && tnow >= o.Script{scriptIdx+1, 1}     % guion automatico
            scriptIdx = scriptIdx + 1;  eng.setCtrl('tone', o.Script{scriptIdx, 2});  eng.setCtrl('snr', o.Script{scriptIdx, 3});
        end
        [audio, info] = eng.step();
        for e = 1:size(info.events, 1)
            cn = {'sin filtro', 'FIR', 'IIR'};
            fprintf('[t=%5.2f s] *** COMANDO campo %d  (%s)  amp=%.3f  SNR~%.1f dB  [%s]\n', info.t, info.events(e,1), ...
                    ternary(info.events(e,1) == 1, '700 Hz', '1000 Hz'), info.events(e,2), info.events(e,3), cn{info.evChain(e)});
        end
        nb = nb + 1;
        % audio: 8 kHz -> AudioFs por interpolacion lineal (continua entre bloques)
        out = max(-1, min(1, 2*vol*audio));
        v = [prev, out];  dv = diff(v);  M = v(1:end-1) + dv.*((0:up-1)'/up);  frame = M(:);  prev = out(end);
        busy = busy + toc(tb);
        if audioOK, underruns = underruns + adw(frame); else, pause(max(0, tblock - toc(tb))); end
        if ~isempty(o.SaveWav), played = [played; frame]; end %#ok<AGROW>
        if hasFig && mod(nb, 2) == 0, updateDisplay(info, audio); end
    end
    fprintf('Fin. %d bloques, carga media de procesamiento %.1f %% del tiempo real (%.1f ms por bloque de %.0f ms), underruns de audio: %d\n', ...
            nb, 100*busy/(nb*tblock), 1000*busy/nb, 1000*tblock, underruns);
    if ~isempty(o.SaveWav) && ~isempty(played), audiowrite(o.SaveWav, played*0.9, P.Fs*up); fprintf('Audio guardado en %s\n', o.SaveWav); end
    if audioOK, release(adw); end
    if hasFig && isvalid(fig)
        if ~isempty(o.Snapshot), exportgraphics(fig, o.Snapshot, 'Resolution', 110); end
        delete(fig);
    end

    % ---------------- callbacks y dibujo (funciones anidadas) ----------------
    function onTone(src, key)
        eng.setCtrl('tone', key);
        for kk = {'off','A','B','both'}, b = findobj(fig, 'Tag', ['tone_' kk{1}]); set(b, 'Value', strcmp(kk{1}, key)); end
        src.Value = 1;
    end
    function onSnr(s), eng.setCtrl('snr', round(s.Value)); set(H.snrLbl, 'String', sprintf('%d dB', round(s.Value))); end
    function onNoise(), eng.setCtrl('noise', logical(cell2mat(get(H.noiseBoxes, 'Value')).')); end
    function setVol(x), vol = x; end

    function updateDisplay(info, audio)
        for c = 1:3
            X = eng.ring(c, :) .* H.win;  S = abs(fft(X));  S = 20*log10(2*S(1:1024)/sum(H.win) + 1e-9);
            set(H.lSpec(c), 'YData', S);
        end
        set(H.lBar(1), 'YData', eng.lastMag(1, :));  set(H.lBar(2), 'YData', eng.lastMag(2, :));
        set(H.lBar(1), 'XData', 1:3);
        yy = [eng.lastMag(:); 0.1];  ylim(H.axBar, [0 max(0.1, 1.25*max(yy))]);
        if eng.calibrated
            for c = 1:3, for t = 1:2, set(H.lThr(t), 'XData', (1:3) + (t - 1.5)*0.27, 'YData', arrayfun(@(cc) eng.det(cc).st.thr(t), 1:3)); end, end
        end
        a = audio(end-min(numel(audio), N*4)+1:end);  set(H.lWave, 'YData', [zeros(1, N*4 - numel(a)), a]);
        if eng.calibrated, st = 'LISTO: elige un tono'; else, st = sprintf('CALIBRANDO el ruido ambiente... %d %%', round(100*min(1, ((20 - eng.warm) + (P.calBlocks - eng.det(1).st.calLeft))/(20 + P.calBlocks)))); end
        rs = {'ok', 'bajo umbral', 'ambiguo', 'SNR baja', 'calibrando', 'reposo'};
        L = {sprintf('%s', st), sprintf('t = %.1f s   tono = %s   SNR = %d dB', info.t, info.tone, eng.ctrl.snr), ...
             sprintf('escuchando: %s', eng.ctrl.listen), sprintf('carga DSP: %.1f ms / bloque (limite %.0f ms)', 1000*busy/nb, 1000*tblock), ''};
        nm = {'sin filtro', 'FIR       ', 'IIR       '};
        for c = 1:3, L{end+1} = sprintf('%s: A700=%.3f A1000=%.3f  -> %s', nm{c}, eng.lastMag(1,c), eng.lastMag(2,c), rs{eng.lastReason(c)+1}); end %#ok<AGROW>
        L{end+1} = '';  L{end+1} = 'Ultimos comandos aceptados:';
        for e = max(1, size(eng.evlog,1)-5):size(eng.evlog,1)
            L{end+1} = sprintf(' t=%5.1f  [%s]  campo %d  SNR~%.0f dB', eng.evlog{e,1}, strtrim(nm{eng.evlog{e,2}}), eng.evlog{e,3}, eng.evlog{e,5}); end %#ok<AGROW>
        set(H.txt, 'String', L);
        drawnow limitrate;
    end
end

function L = semilogy_like(ax, fr)
    L(1) = plot(ax, fr, -120*ones(size(fr)), 'Color', [.55 .55 .55]); hold(ax, 'on');
    L(2) = plot(ax, fr, -120*ones(size(fr)), 'Color', [0 .45 .74], 'LineWidth', 1.2);
    L(3) = plot(ax, fr, -120*ones(size(fr)), 'Color', [.85 .33 .1], 'LineWidth', 1.2);
    p = dsp_params();  xline(ax, p.tones, 'r:');  grid(ax, 'on');
    xlim(ax, [0 2000]);  ylim(ax, [-120 0]);  xlabel(ax, 'f [Hz]'); ylabel(ax, 'amplitud [dBFS]');
    legend(ax, {'Contaminada (sin filtro)', 'Salida FIR', 'Salida IIR'}, 'Location', 'northeast'); title(ax, 'Espectro en vivo (ultimos 256 ms)');
end
function s = ternary(c, a, b), if c, s = a; else, s = b; end, end
