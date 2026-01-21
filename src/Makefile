all: client server

client: client.c protocol.c utils.c
	gcc client.c protocol.c utils.c -o client

server: server.c protocol.c utils.c
	gcc server.c protocol.c utils.c -o server

test:
	@bash test.sh

clean-test:
	@rm -f server_test.log server.pid test_output.csv

clean: clean-test
	@rm -f client server manager
	@rm -r .msync

.PHONY: test clean-test clean