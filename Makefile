#### RVM Library Makefile

CFLAGS  = -std=c++11 -Wall -g -I.
LFLAGS  =
CC      = g++
RM      = /bin/rm -rf
AR      = ar rc
RANLIB  = ranlib

LIBRARY = librvm.a

LIB_SRC = rvm.cpp

LIB_OBJ = $(patsubst %.cpp,%.o,$(LIB_SRC))

%.o: %.cpp
	$(CC) -c $(CFLAGS) $< -o $@

all: $(LIBRARY) abort basic multi multi-abort truncate

$(LIBRARY): $(LIB_OBJ)
	$(AR) $(LIBRARY) $(LIB_OBJ)
	$(RANLIB) $(LIBRARY)

abort: testcases/abort.o $(LIBRARY)
	$(CC) -o $@ $^

basic: testcases/basic.o $(LIBRARY)
	$(CC) -o $@ $^

multi: testcases/multi.o $(LIBRARY)
	$(CC) -o $@ $^

multi-abort: testcases/multi-abort.o $(LIBRARY)
	$(CC) -o $@ $^

truncate: testcases/truncate.o $(LIBRARY)
	$(CC) -o $@ $^

clean:
	$(RM) $(LIBRARY) $(LIB_OBJ)
