To run:

install vtune,
make sure using nix shell (regular dramhit setup),
then,

gcc -std=c++11 -pthread -lnuma writeback.cpp -o writeback -I/opt/intel/oneapi/vtune/latest/include -L/opt/intel/oneapi/vtune/latest/lib64 -littnotify -ldl -lstdc++

./writeback


