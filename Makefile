CC=mipsel-openwrt-linux-gcc
STRIP=$(CROSS)strip
TARGET=keepinfo
CFLAGS= -fPIC -g -Wall -Iinclude
ALL: clean $(TARGET)
FILE=cJSON.o kepinfo.o module.o network.o request.o speed.o 
keepinfo: $(FILE) 
	$(CC) -o $(TARGET) $(FILE) $(LDFLAGS) $(CFLAGS) -DOPENWRT -I/home/zhangwei/exercise/linux_c/vstfun/keepinfo/lib/include -L/home/zhangwei/exercise/linux_c/vstfun/keepinfo/lib/ -lm -lpthread -lcrypto -lssl -lcurl -lmxml 
%.o:%.c
	$(CC) -c -o $@ $^ $(CFLAGS) -DOPENWRT -I/home/zhangwei/exercise/linux_c/vstfun/keepinfo/lib/include -L/home/zhangwei/exercise/linux_c/vstfun/keepinfo/lib/ 

clean: 
	rm -f *.o $(TARGET) 
