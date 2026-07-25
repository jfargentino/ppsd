function [Kout, y] = outliers3 (x, thresh)

    if nargin < 2
        thresh = 3 * sqrt(var(x))
    end

    Kout = [];
    y = x;
    for k = 2 : 1 : length(x)-1
        if (abs(x(k) - x(k-1)) > thresh) && (abs(x(k) - x(k+1)) > thresh)
            Kout = [ Kout; k ];
            y(k) = ( y(k-1) + y(k+1) ) / 2;
        end
    end

    %y = x;
    %y(Kout) = ( y(Kout-1) + y(Kout+1) ) / 2;

