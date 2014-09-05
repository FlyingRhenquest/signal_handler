OBJ=signal_handler.o
CPPFLAGS += -std=c++11

.cpp.o:
	g++ ${CPPFLAGS} -c $<

all: ${OBJ}

clean:
	rm -f *~ ${OBJ}
