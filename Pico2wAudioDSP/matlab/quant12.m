function y = quant12(x)
%QUANT12  Cuantiza como el ADC de 12 bits de la Pico (escala completa +-1) y recorta.
    y = round(max(-1, min(1, x))*2048)/2048;
end
