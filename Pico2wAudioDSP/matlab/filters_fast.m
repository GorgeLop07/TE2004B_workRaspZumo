function varargout = filters_fast(op, varargin)
%FILTERS_FAST  Los mismos filtros, vectorizados, para barridos largos.
%   filter(b,a,x) evalua la MISMA ecuacion en diferencias (no disena nada); su
%   equivalencia con filters_ref se comprueba en demo_dsp.m / verify_against_c.m.
    switch op
        case 'dc',  [x, R] = deal(varargin{:});            varargout{1} = filter([1 -1], [1 -R], x);
        case 'fir', [x, h] = deal(varargin{:});            varargout{1} = filter(h, 1, x);
        case 'iir', [x, sos, nsec] = deal(varargin{:});
            nb = size(sos,1);  y = zeros(size(x));
            for t = 1:nb/nsec
                v = x;
                for q = 1:nsec
                    c = sos((t-1)*nsec + q, :);
                    v = filter(c(1:3), [1 c(4:5)], v);
                end
                y = y + v;
            end
            varargout{1} = y;
        otherwise, error('op desconocida: %s', op);
    end
end
