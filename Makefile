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

install: $(TARGET)
	install -D -m 755 $(TARGET) ${DESTDIR}${BINDIR}/$(TARGET)

uninstall:
	rm -f ${DESTDIR}${BINDIR}/$(TARGET)

.PHONY: all clean install
