OBJS = run_cppunit.o signal_handler_test.o
EXT_OBJ = ../signal_handler.o
EXT_CPP = ../signal_handler.cpp
EXE = run_cppunit
CFLAGS += -g --std=c++11 -I..
LFLAGS = -lcppunit -ldl -lpthread -lboost_system -lboost_thread

all: ${OBJS} ${EXT_OBJ}
	g++ -o ${EXE} ${OBJS} ${EXT_OBJ} ${LFLAGS}

${EXT_OBJ}: ${EXT_CPP}
	cd .. && make

.cpp.o:
	g++ -c ${CFLAGS} $<

clean:
	rm -f *~ ${EXE} ${OBJS} core
