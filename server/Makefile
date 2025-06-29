SRC := aesdsocket.c
TARGET = aesdsocket
CC = gcc
CFLAGS = -g -Wall -Werror

ifeq ($(CROSS_COMPILE), aarch64-none-linux-gnu-)
CC = aarch64-none-linux-gnu-gcc
endif

all: $(TARGET)
default: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
	@echo "Compiled with $(CC)"

clean:
	rm -f *.o $(TARGET) *.elf *.map
