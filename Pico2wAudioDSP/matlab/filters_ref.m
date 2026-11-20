function varargout = filters_ref(op, varargin)
%FILTERS_REF  Filtros MUESTRA A MUESTRA, como corren en la Pico (referencia didactica).
%   Ecuaciones en diferencias explicitas y bufer circular; nada de filter/conv.
%     y = filters_ref('dc',  x, R)             y[n] = x[n] - x[n-1] + R y[n-1]
%     y = filters_ref('fir', x, h)             FIR forma directa I, bufer circular
%     y = filters_ref('iir', x, sos, nsec)     banco IIR: tonos en paralelo, biquads (forma directa II) en cascada
    switch op
        case 'dc',  varargout{1} = dc_ref(varargin{:});
        case 'fir', varargout{1} = fir_ref(varargin{:});
        case 'iir', varargout{1} = iir_ref(varargin{:});
        otherwise,  error('op desconocida: %s', op);
    end
end

function y = dc_ref(x, R)
    y = zeros(size(x));  x1 = 0;  y1 = 0;
    for n = 1:numel(x)
        y(n) = x(n) - x1 + R*y1;
        x1 = x(n);  y1 = y(n);
    end
end

function y = fir_ref(x, h)
    N = numel(h);  buf = zeros(1, N);  pos = 0;          % pos = indice (base 0) donde cae la muestra nueva
    y = zeros(size(x));
    k = 0:N-1;
    for n = 1:numel(x)
        buf(pos+1) = x(n);                               % escribe x[n]; sobrescribe la mas vieja, sin desplazar memoria
        idx = mod(pos - k, N) + 1;                       % x[n-k] vive en buf[(pos-k) mod N]
        y(n) = h * buf(idx).';                           % N MAC
        pos = mod(pos + 1, N);
    end
end

function y = iir_ref(x, sos, nsec)
    nb = size(sos,1);  ntones = nb/nsec;
    w1 = zeros(1,nb);  w2 = zeros(1,nb);
    y = zeros(size(x));
    for n = 1:numel(x)
        acc = 0;
        for t = 1:ntones
            v = x(n);
            for q = 1:nsec
                i = (t-1)*nsec + q;  c = sos(i,:);
                w  = v - c(4)*w1(i) - c(5)*w2(i);        % w[n] = x - a1 w[n-1] - a2 w[n-2]
                v  = c(1)*w + c(2)*w1(i) + c(3)*w2(i);   % y[n] = b0 w[n] + b1 w[n-1] + b2 w[n-2]
                w2(i) = w1(i);  w1(i) = w;
            end
            acc = acc + v;
        end
        y(n) = acc;
    end
end
