
SDK = /opt/intel/oneapi/vtune/latest/sdk

all:
	g++ writeback.cpp -I $(SDK)/include/ $(SDK)/lib64/libittnotify.a -ldl -lpthread -lnuma
