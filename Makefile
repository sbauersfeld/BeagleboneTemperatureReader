
default:
	gcc -g -Wall -Wextra -lmraa -o lab4c_tcp lab4c_tcp.c -lm
	gcc -g -Wall -Wextra -lmraa -lssl -o lab4c_tls lab4c_tls.c -lm -lcrypto

dist: default
	tar -czvf lab4c.tar.gz README lab4c_tcp.c lab4c_tls.c Makefile

clean:
	rm -f lab4c.tar.gz lab4c_tcp lab4c_tls
