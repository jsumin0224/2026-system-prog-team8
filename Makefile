CC = gcc
CFLAGS = -Wall -Wextra -g -D_POSIX_C_SOURCE=200809L -Iclient -Iserver -Idatabase -Imonitoring

DB_SRCS = database/account.c database/file_lock.c database/transaction.c
MON_SRCS = monitoring/signal_handler.c monitoring/timer.c monitoring/monitor.c monitoring/admin.c
MON_MAIN_SRC = monitoring/monitor_main.c
SRV_CORE_SRCS = server/server.c server/ipc.c
STUB_SRCS = $(SRV_CORE_SRCS) server/atm_stub.c $(DB_SRCS) $(MON_SRCS)
FULL_SRCS = $(SRV_CORE_SRCS) client/client.c client/terminal.c $(DB_SRCS) $(MON_SRCS)

STUB_OBJS = $(STUB_SRCS:.c=.o)
FULL_OBJS = $(FULL_SRCS:.c=.o)
MON_MAIN_OBJ = $(MON_MAIN_SRC:.c=.o)

STUB_TARGET = atm_stub_server
FULL_TARGET = atm_server
MON_TARGET = atm_monitor

.PHONY: all stub full monitor clean

all: stub

stub: $(STUB_TARGET)

full: $(FULL_TARGET)

monitor: $(MON_TARGET)

$(STUB_TARGET): $(STUB_OBJS)
	$(CC) $(CFLAGS) -o $@ $(STUB_OBJS)

$(FULL_TARGET): $(FULL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(FULL_OBJS)

$(MON_TARGET): $(MON_MAIN_OBJ) $(MON_SRCS:.c=.o)
	$(CC) $(CFLAGS) -o $@ $(MON_MAIN_OBJ) $(MON_SRCS:.c=.o)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(STUB_OBJS) $(FULL_OBJS) $(MON_MAIN_OBJ) $(STUB_TARGET) $(FULL_TARGET) $(MON_TARGET) accounts.dat transactions.log
