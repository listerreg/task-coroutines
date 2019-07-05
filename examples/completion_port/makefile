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
OUT_FILE := libcompletionport.a
DNDEBUG_ = -DNDEBUG #trailing space

$(shell mkdir -p bin)
$(shell mkdir -p build)

vpath %.a bin
vpath %.o build
vpath %.cpp src

$(OUT_FILE) : completion_port.o
	ar -cvr bin/libcompletionport.a build/completion_port.o

completion_port.o : src/completion_port.cpp include/completion_port.h ../../include/taskcoroutines.h
	$(CXX) -c $(DNDEBUG_)$(CXXFLAGS) -Iinclude -I../../include $< -o build/$@

debug: $(OUT_FILE)
debug: CXXFLAGS += -g
debug: DNDEBUG_ =

.PHONY: clean
clean:
	rm -f bin/$(OUT_FILE) ; rm -f build/*.o
