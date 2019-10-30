all: clean httpd

httpd: httpd.c
	gcc -o httpd httpd.c

clean:
	rm httpd
