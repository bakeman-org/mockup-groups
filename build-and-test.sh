set -xe

git clean -fxd
cc mockup.c -o mockup -Wall -Wextra -Werror
cc test1.c -o test1
cc test2.c -o test2 -lSDL2

./mockup ./test1 -o test1folder
./mockup ./test2 -o test2folder
# with suffix
./mockup test1 test2 -x .bash -f
./mockup test1 test2 -x .bash -f -P

echo "=================================TEST DONE======================================"


git clean -fxd
cc mockup.c -o mockup -Wall -Wextra -Werror
cc test1.c -o test1
cc test2.c -o test2 -lSDL2

./mockup test1 test2 -x .bash -f -D

echo "=================================TEST DRYRUN DONE======================================"

git submodule update --init
