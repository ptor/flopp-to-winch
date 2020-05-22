LD = $(CC)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

CFLAGS	= -Wall -g

.PHONY: depend

flopp-to-winch: flopp-to-winch.c
	$(LD) $(LDFLAGS) -o $@ $^

clean:
	$(RM) flopp-to-winch flopp-to-winch.o
