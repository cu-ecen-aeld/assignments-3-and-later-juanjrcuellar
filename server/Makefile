SRC := aesdsocket.c
TARGET = aesdsocket
CC = gcc

ifeq ($(CROSS_COMPILE), aarch64-none-linux-gnu-)
CC = aarch64-none-linux-gnu-gcc
endif

all: $(TARGET)
default: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -g -Wall -Werror $(SRC) -o $(TARGET)
	@echo "Compiled with $(CC)"

clean:
	rm -f *.o $(TARGET) *.elf *.map
