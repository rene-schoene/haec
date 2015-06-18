RACKET_PATH = ../../racket
RACR_PATH = ../../2bt/racr
MQUAT_PATH = ../../racr-mquat

CRACR = -I$(RACR_PATH)/racr-c/include -I$(RACKET_PATH)/racket/include
LRACR = -lm -lpthread -ldl -lffi $(RACR_PATH)/racr-c/build/racr.o
LRACR += $(RACKET_PATH)/racket/src/build/racket/libracket.a
LRACR += $(RACKET_PATH)/racket/src/build/racket/libmzgc.a


CF = -Wall -O2 -Iinclude
LF = -pthread -lrt

all: server worker mquat.scm


SERVERSRC = src/main.c src/server.c src/cambri.c src/event.c src/worker.c
SERVEROBJ = $(SERVERSRC:src/%.c=obj/%.o)

server: Makefile $(SERVEROBJ) $(LRACR)
	gcc $(LF) $(LRACR) $(SERVEROBJ) -o $@


obj/%.o: src/%.c Makefile
	@mkdir -p obj
	gcc $(CF) $(CRACR) $< -c -o $@


worker: Makefile obj/worker_main.o
	gcc $(LF) obj/worker_main.o -o $@

obj/worker_main.o: Makefile src/worker_main.c
	@mkdir -p obj
	gcc $(CF) src/worker_main.c -c -o $@


bytecode: mquat.scm
	@cd $(MQUAT_PATH) && make racket
	WD="$$PWD" &&\
	cd $(RACR_PATH)/racr-c && ./bin/make_bc "`echo $$WD/bytecode`"

mquat.scm: $(MQUAT_PATH)/mquat.scm
	@sed -e '1,/;; --- /d' $(MQUAT_PATH)/mquat.scm > mquat.scm

clean:
	rm -rf obj server worker
	rm -f status.log event.log cambri.log

.PHONY: clean bytecode
