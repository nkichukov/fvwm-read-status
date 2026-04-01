CC = gcc
CFLAGS = -Wall -Wextra -O2
LDLIBS = -lcjson

TARGET = fvwm-read-status

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)
	strip $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
