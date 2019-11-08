all: clean httpd

httpd: httpd.c
	gcc -pthread -o httpd httpd.c

clean:
	rm httpd
