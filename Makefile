flags := -Wall -Wextra -pedantic -std=c99

milo:
	$(CC) milo.c -o milo $(flags)

clean:
	rm -f milo
