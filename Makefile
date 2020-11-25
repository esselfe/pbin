
CFLAGS = -std=c11 -Wall -Werror -D_GNU_SOURCE -O2
LDFLAGS = -pthread
PROGNAME = pbin

.PHONY: all clean

default: all

all: $(PROGNAME)

$(PROGNAME): pbin.c
	gcc $(CFLAGS) $(LDFLAGS) pbin.c -o $(PROGNAME)

clean:
	@rm -v $(PROGNAME) 2>/dev/null || true

