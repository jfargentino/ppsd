#if (defined(PNG_FILE))
#    set terminal png
#    set output 'TOTO.png'
set terminal png
set output sprintf('%s.png', DRIFT_FILE)

set datafile separator ','

set xdata time # tells gnuplot the x axis is time data
set timefmt "%s" ## specify our time string format "%Y-%m-%dT%H:%M:%S"
#set format x "%H:%M:%S" # otherwise it will show only MM:SS

set grid ls 100
set ylabel "PPS offset (ns)"
set y2tics # enable second axis
set ytics nomirror # dont show the tics on that side
set y2label "drift (ppb)"

#plot '1768742056.off' using 1:3 with lines, '' using 1:4 with lines axis x1y2
plot DRIFT_FILE using 1:3 with lines, '' using 1:4 with lines axis x1y2
#plot '1768742056.off' using 1:4 with lines
