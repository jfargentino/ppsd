function [dr, y0, m, v, c] = drift(data_file, remove_outliers, biased)

    if nargin < 2
        remove_outliers = 0;
    end
    if nargin < 3
        biased = 0;
    end

    data = dlmread(data_file);
    [r,c] = size(data);
    if r < 4
        return
    end
    Tref = data(:, 1);
    offsets = data(:, 2);
    idr = data(:, 5);
    idr(1) = 0;
    

    if (remove_outliers > 0)
        L = length(offsets);
        [k, m, v] = _outliers(offsets, remove_outliers);
        Tref = Tref(k);
        offsets = offsets(k);
        printf('Keeping %.2f%% of data\n', 100.0 * length(k) / L);
        [k, m, v] = _outliers(idr, remove_outliers);
        idr = idr(k);
    end

    m = round(mean(offsets));
    v = round(var(offsets, biased));
    n = Tref - Tref(1);
    c = round(cov(offsets, n, biased));
    dr = round(c / var(n));
    y0 = m - dr*mean(n);

    if nargout == 0
        figure
        %figure(1, 'WindowState', 'fullscreen')
        
        subplot(2, 1, 1);
        linreg = dr * n + y0;
        %d = offsets - linreg;
        %dm = mean(d);
        %sd2m = sqrt(mean(d.*d));
        plot(n, [offsets, linreg]);
        grid on;
        xlim([0, n(end)]);
        ylim([min(offsets), max(offsets)])
        ylabel('PPS offset ns')
        s = sprintf('OFFSET: mean=%d; std dev=%d; covar=%d; drift=%d', ...
                    m, sqrt(v), c, dr);
        title(s)
    
        subplot(2, 1, 2);
        plot(idr);
        grid on;
        xlim([0, length(idr-1)]);
        ylim([min(idr), max(idr)])
        xlabel('s')
        ylabel('Instantaneous drift ppb')
        s = sprintf('DRIFT: mean=%.3f; sdtdev=%.3f', mean(idr), sqrt(var(idr)));
        title(s)
    
        % TODO substitute .off with .png
        png_file = [data_file, '.png'];
        saveas(1, png_file)
    end

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
function [k, m, v] = _outliers(x, rate)
    if (nargin < 2)
        rate = 3;
    end
    m = mean(x);
    v = var(x);
    stddev = sqrt(v);
    k = find(abs(x - m) <= rate*stddev);
