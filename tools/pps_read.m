function xy = pps_read(filename)

xy = [];
fid = fopen (filename, 'r');
line = fgetl(fid);
while (line != -1) || isempty(line)
    if (!isempty(line) && (line(1) != '#'))
        %xx =  sscanf(line, '%Lf,')
        xy = [xy; sscanf(line, '%Lf,')'];
    end
    line = fgetl(fid);
end

fclose(fid);

