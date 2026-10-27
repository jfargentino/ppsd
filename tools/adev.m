function [adevs, taus] = adev(offset_ns, tau_0)

%function [adevs, taus] = adev(unix_timestamps, tau_0)
    % CALCULATE_PPS_ALLAN_DEV Computes Overlapping Allan Deviation from 1 PPS Unix timestamps
    %
    % Inputs:
    %   unix_timestamps - Vector of Unix epochs (seconds with fractional floats)
    %   tau_0           - Base sampling interval (Default = 1.0 for 1 PPS)
    
    if nargin < 2
        tau_0 = 1.0;
    end
    
    % Ensure data is a column vector
    %timestamps = double(unix_timestamps(:));
    %N = length(timestamps);
    
    % 1. Convert Timestamps to Phase Error x(t)
    % Strips out the steady linear 1Hz clock progression to extract raw jitter
    %time_indices = (0:N-1)';
    %ideal_grid = timestamps(1) + (time_indices * tau_0);
    %x = timestamps - ideal_grid; 
    
    % 2. Define tau intervals (m-factors) using powers of 2 up to N/4
    N = length(offset_ns);
    max_m_power = floor(log2(N / 4));
    m_factors = 2.^(0:max_m_power)';
    
    num_taus = length(m_factors);
    taus = zeros(num_taus, 1);
    adevs = zeros(num_taus, 1);
    x_s = offset_ns * 1e-9;
    % 3. Calculate Overlapping Allan Variance
    for k = 1:num_taus
        m = m_factors(k);
        tau = m * tau_0;
        limit = N - 2 * m;
        
        % Vectorized double-difference formula:
        % x_s(k + 2m) - 2*x_s(k + m) + x_s(k)
        double_diff = x_s(1+2*m:end)-2*x_s(1+m:end-m)+x_s(1:end-2*m);
        
        % Compute Allan Variance and standard deviation
        sum_squares = sum(double_diff.^2);
        allan_var = sum_squares / (2.0 * limit * (tau^2));
        
        taus(k) = tau;
        adevs(k) = sqrt(allan_var);
    end

    if nargout == 0
        loglog(taus, adevs);
        grid on;
        xlabel('Averaging time (s)');
        xlim([1, max(taus)]);
        ylabel('Allan dev');
        clear taus;
        clear adevs;
    end

