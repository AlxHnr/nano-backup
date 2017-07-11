mkdir -p generated/repo/1/2/3/4/5/6/7/8/9
mkdir -p generated/repo/3/58

yes "TEST" | head -c 1928 > generated/repo/1/2/3/4/foo.txt
yes "BEST" | head -c  731 > generated/repo/1/2/3/4/5/6/7/foo.txt

cp generated/repo/4/62/78792cb225b37de7a8bdedc8d5159ffa74933x2d2a8x0 \
  generated/repo/3/58/

ln -s 2296fc0422ea917ecd9a060a4220eb466798cx2d2a8x0 \
  generated/repo/f/09/symlink

ln -s 2296fc0422ea917ecd9a060a4220eb466798cx2d2a8x0 \
  generated/repo/f/09/2296fc0422ea917ecd9a060a4220eb466798cx2d2a8x1

ln -s ../ generated/repo/3/59
