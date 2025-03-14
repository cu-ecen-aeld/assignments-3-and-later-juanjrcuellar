SRC := writer.c
TARGET = writer
CC = gcc

ifeq ($(CROSS_COMPILE), aarch64-none-linux-gnu-)
CC = aarch64-none-linux-gnu-gcc
endif

all:
	$(CC) -g -Wall $(SRC) -o $(TARGET)
	@echo "Compiled with $(CC)"

clean:
	rm -f *.o $(TARGET) *.elf *.map
