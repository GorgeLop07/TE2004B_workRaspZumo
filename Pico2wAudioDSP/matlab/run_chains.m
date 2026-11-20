function C = run_chains(x, P, D, fast)
%RUN_CHAINS  Pasa x por: bloqueo DC -> {sin filtro, FIR, banco IIR}.   D = disenos (design_all).
%   Devuelve C.raw (ya sin DC), C.fir, C.iir.  fast=false usa los filtros muestra a muestra.
    if nargin < 4, fast = true; end
    if fast, F = @filters_fast; else, F = @filters_ref; end
    xd    = F('dc',  x(:).', P.dcR);
    C.raw = xd;
    C.fir = F('fir', xd, D.h);
    C.iir = F('iir', xd, D.sos, P.lpOrder);
end
