set -xe

cc mockup.c -o mockup -Wall -Wextra
cc test1.c -o test1
cc test2.c -o test2 -lSDL2
./mockup ./test1 -o test1folder
./mockup ./test2 -o test2folder
