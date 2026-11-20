function H = sos_response(sos, w)
%SOS_RESPONSE  H(e^{jw}) de una cascada de biquads (w en rad/muestra, vector o escalar).
    z1 = exp(-1j*w);  z2 = z1.^2;  H = ones(size(w));
    for q = 1:size(sos,1)
        c = sos(q,:);
        H = H .* (c(1) + c(2)*z1 + c(3)*z2) ./ (1 + c(4)*z1 + c(5)*z2);
    end
end
