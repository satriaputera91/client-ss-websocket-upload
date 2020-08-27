GCC=gcc
CFLAGS=-g -Wall -c -fmessage-length=0

LIBS=-lwebsockets -lapa102 -lwiringPi -lcurl -luuid -lasound -ljson-c -lpthread -lpulse-simple -lpulse
PATHLIB=-L${PWD}/lib

ALL: client-ss-websocket client-ss-websocket-lite

client-ss-websocket: client-ss-websocket.o http-request.o client-socket.o
	${GCC} ${PATHLIB} -o client-ss-websocket client-ss-websocket.o http-request.o client-socket.o ${LIBS}

client-ss-websocket-lite: client-ss-websocket-lite.o http-request.o client-socket.o
	${GCC} ${PATHLIB} -o client-ss-websocket-lite client-ss-websocket-lite.o http-request.o client-socket.o ${LIBS}

client-ss-websocket-lite.o: src/client-ss-websocket-lite.c
	${GCC} ${CFLAGS} -o client-ss-websocket-lite.o src/client-ss-websocket-lite.c

client-ss-websocket.o: src/client-ss-websocket.c
	${GCC} ${CFLAGS} -o client-ss-websocket.o src/client-ss-websocket.c

http-request.o: src/http-request.c
	${GCC} ${CFLAGS} -o http-request.o src/http-request.c

client-socket.o: src/client-socket.c
	${GCC} ${CFLAGS} -o client-socket.o src/client-socket.c

clean:
	rm -rf *.o client-ss-websocket*
	

