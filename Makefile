flags := -Wall -Wextra -pedantic -std=c99 -g

kilo: kilo.c
	$(CC) kilo.c -o kilo $(flags)

clean:
	rm -f kilo
