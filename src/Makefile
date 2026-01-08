all: client server

client: client.c
	gcc client.c -o client -lm

server: server.c
	gcc server.c -o server -lm

test:
	@bash test.sh

clean-test:
	@rm -f server_test.log server.pid test_output.csv

clean: clean-test
	@rm -f client server manager

.PHONY: test clean-test clean