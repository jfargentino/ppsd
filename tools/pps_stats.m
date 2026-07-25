function [m, s, d] = pps_stats(XY, winlen)

if (ischar(XY))
    XY = pps_read(XY);
end
if (nargin < 2 ) | (winlen < 1)
    m = mean(XY(:,2));
    s = sqrt(var(XY(:,2)));
    d = cov(XY(:,1), XY(:,2)) / var(XY(:,1));
    return
end

% crude implementation, eat CPU
[r,c] = size(XY);
m = zeros(r,1);
s = zeros(r,1);
d = zeros(r,1);
if (0)
zXY = [zeros(winlen-1, 2); XY(:, 1:2)];
for k = 1:r
    K = k : k + winlen - 1;
    m(k) = mean(zXY(K,2));
    s(k) = sqrt(var(zXY(K,2)));
    d(k) = cov(zXY(K,1), zXY(K,2)) / var(zXY(K,1));
end
else
for k = 2:winlen
    K = 1:k;
    m(k) = mean(XY(K,2));
    s(k) = sqrt(var(XY(K,2)));
    d(k) = cov(XY(K,1), XY(K,2)) / var(XY(K,1));
end
for k = winlen+1:r
    K = k-winlen+1:k;
    m(k) = mean(XY(K,2));
    s(k) = sqrt(var(XY(K,2)));
    d(k) = cov(XY(K,1), XY(K,2)) / var(XY(K,1));
end
end

if (nargout == 0)
    plot(XY(:, 1), [XY(:, 2), m]);
    xlim([min(XY(:, 1)), max(XY(:, 1))]);
    grid on
    clear m, s, d
end
