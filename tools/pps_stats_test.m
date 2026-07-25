function err = pps_stats_test(filename, winlen)

xy = pps_read(filename);
[m, s, d] = pps_stats(xy, winlen);
merr = xy(:, 3) - m;
serr = xy(:, 4) - s;
derr = xy(:, 5) - d;
err = [merr, serr, derr];
fileout = sprintf('%s.%d', filename, winlen);
dlmwrite(fileout, [xy(:, 1:2), m, s, d], ',');

if (nargout == 0)
    subplot(3, 1, 1)
    plot(xy(:, 1), [xy(:, 3), m]);
    xlim([min(xy(:, 1)), max(xy(:, 1))]);
    grid on
    subplot(3, 1, 2)
    plot(xy(:, 1), [xy(:, 4), s]);
    xlim([min(xy(:, 1)), max(xy(:, 1))]);
    grid on
    subplot(3, 1, 3)
    plot(xy(:, 1), [xy(:, 5), d]);
    xlim([min(xy(:, 1)), max(xy(:, 1))]);
    grid on
    clear err
end
