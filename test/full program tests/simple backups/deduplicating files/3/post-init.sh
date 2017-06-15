mkdir -p generated/repo/1/2/3/4/5/6/7/8/9
mkdir -p generated/repo/3/58

yes "TEST" | head -c 1928 > generated/repo/1/2/3/4/foo.txt
yes "BEST" | head -c  731 > generated/repo/1/2/3/4/5/6/7/foo.txt

cp generated/repo/3/57/9fb6dabbce6708d6bf691bdc5127f25520b73x2d2a8x0 \
  generated/repo/3/58/

ln -s 2f435857947e0fb733aae5493bc51bf426d40x2d2a8x0 \
  generated/repo/6/4b/symlink

ln -s 2f435857947e0fb733aae5493bc51bf426d40x2d2a8x0 \
  generated/repo/6/4b/2f435857947e0fb733aae5493bc51bf426d40x2d2a8x1

ln -s ../ generated/repo/3/59
