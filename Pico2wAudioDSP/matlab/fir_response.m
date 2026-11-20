function H = fir_response(h, w)
%FIR_RESPONSE  H(e^{jw}) = sum h[n] e^{-jwn}, evaluada directamente (w en rad/muestra).
    n = 0:numel(h)-1;
    H = zeros(size(w));
    for i = 1:numel(w), H(i) = sum(h .* exp(-1j*w(i)*n)); end
end
