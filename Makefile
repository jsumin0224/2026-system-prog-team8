CC     = gcc
CFLAGS = -Wall -Wextra -g

DB_SRC     = database/account.c database/file_lock.c database/transaction.c
SERVER_SRC = server/server.c server/ipc.c
ATM_SRC    = server/atm_stub.c

bank: $(SERVER_SRC) $(ATM_SRC) $(DB_SRC) server/ipc.h database/account.h
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) $(ATM_SRC) $(DB_SRC)

test: bank
	sh tests/test_server.sh

clean:
	rm -f bank accounts.dat transactions.log

.PHONY: test clean
