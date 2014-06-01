RACKET_PATH = /home/twobit/Programming/scheme/racket
RACR_PATH = /home/twobit/Programming/scheme/racr

CRACR = -I$(RACR_PATH)/racr-c/include -I$(RACKET_PATH)/racket/include
LRACR = -lm -lpthread -ldl -lffi $(RACR_PATH)/racr-c/build/racr.o
LRACR += $(RACKET_PATH)/racket/src/build/racket/libracket.a
LRACR += $(RACKET_PATH)/racket/src/build/racket/libmzgc.a


CF = -Wall -O2
LF = -pthread

all: server worker

server: Makefile obj/server.o
	gcc $(LF) $(LRACR) obj/server.o -o $@

obj/server.o: Makefile src/server.c
	@mkdir -p obj
	gcc $(LC) $(CRACR) src/server.c -c -o $@


worker: Makefile obj/worker.o
	gcc $(LF) obj/worker.o -o $@

obj/worker.o: Makefile src/worker.c
	@mkdir -p obj
	gcc $(LC) src/worker.c -c -o $@

bytecode:
	@WD="$$PWD" &&\
	cd .. &&\
	cd $(RACR_PATH)/racr-c && ./bin/make_bc "`echo $$WD/bytecode`"

clean:
	rm -rf obj server worker

.PHONY: clean bytecode