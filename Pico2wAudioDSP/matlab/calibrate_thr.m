function thr = calibrate_thr(P, D, NZ, kind)
%CALIBRATE_THR  Calibra los umbrales con ruido ambiente (sin tonos), como hace la Pico al arrancar:
%   20 bloques de asentamiento + P.calBlocks bloques para media y sigma.  thr.raw/.fir/.iir = [1 x ntones].
    len = (20 + P.calBlocks + 2)*P.N;
    x   = quant12(P.dcBias + P.sigmaN*gen_noise(len, kind, NZ));
    C   = run_chains(x, P, D);
    for nm = {'raw','fir','iir'}
        mag = goertzel_blocks(C.(nm{1}), C.raw, P.tones, P.Fs, P.N);
        cal = mag(:, 21:20+P.calBlocks);
        thr.(nm{1}) = max(P.ampFloor, mean(cal,2) + P.thrK*std(cal,0,2));
    end
end
