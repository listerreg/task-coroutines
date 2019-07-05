#pre 4.2 version of the GNU Make:
nogcc := $(shell g++ --version 2>/dev/null 1>&2 ; echo $$?)
ifeq ($(nogcc),0)
CXX := g++
else
CXX := clang++
endif
ifndef STD
STD := c++14
endif
ifdef CONCEPTS
CONCEPT := -fconcepts
endif

CXXFLAGS := -std=$(STD) -Wall -Wextra -Wpedantic -fPIC $(CONCEPT)
DNDEBUG_ = -DNDEBUG #trailing space

#watch for the double dollar sign ($$) and the single quotes around the $ORIGIN
sample : sample.o completion_port/bin/libcompletionport.a ../bin/libtaskcoroutines.so.0
	$(CXX) -Lcompletion_port/bin -L../bin -Wl,-rpath,'$$ORIGIN/../bin' $< -lcompletionport -l:libtaskcoroutines.so.0 -lpthread -o $@

sample.o : main.cpp completion_port/include/completion_port.h ../include/taskcoroutines.h
	$(CXX) -c $(DNDEBUG_)$(CXXFLAGS) -Icompletion_port/include -I../include $< -o $@

../bin/libtaskcoroutines.so.0:
	$(MAKE) -C ../ $(SUBMAKEFLAGS)

completion_port/bin/libcompletionport.a:
	$(MAKE) -C completion_port/ $(SUBMAKEFLAGS)

debug: sample
debug: CXXFLAGS += -g
debug: DNDEBUG_ =
debug: SUBMAKEFLAGS += debug

.PHONY: clean
clean:
	rm -f sample*

.PHONY: all
all:
	$(MAKE) -C completion_port/ debug
	$(MAKE) debug
