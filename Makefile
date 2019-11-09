all: clean httpd

httpd: httpd.c
	gcc -Wall -pthread -o httpd httpd.c

clean:
	rm httpd
