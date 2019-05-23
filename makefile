#pre 4.2 version of the GNU Make:
nogcc := $(shell g++ --version 2>/dev/null 1>&2 ; echo $$?)
ifeq ($(nogcc),0)
CXX := g++
else
CXX := clang++
endif
noldconfig := $(shell ldconfig --version 2>/dev/null 1>&2; echo $$?)
ifeq ($(noldconfig), 0)
SYMLINK_COMMAND = ldconfig -l $(binarydir)/$(OUT_FILE)
else
noldconfig := $(shell /sbin/ldconfig --version 2>/dev/null 1>&2; echo $$?)
ifeq ($(noldconfig), 0)
SYMLINK_COMMAND = /sbin/ldconfig -l $(binarydir)/$(OUT_FILE)
endif
endif
ifeq ($(SYMLINK_COMMAND),)
SYMLINK_COMMAND = ln -s $(OUT_FILE) $(binarydir)/$(SONAME)
endif


CXXFLAGS := -std=c++14 -Wall -Wextra -Wpedantic -fPIC

includedir := include
sourcedir := src
objectdir := build
binarydir := bin
DEPDIR := .d
$(shell mkdir -p $(DEPDIR))

OBJECTS := taskcoroutines.o saveandswitch_asm.o sink_asm.o unsink_asm.o cleanup_asm.o mymemcpy_asm.o
objects_fullpath := $(OBJECTS:%=$(objectdir)/%)
OUT_FILE := libtaskcoroutines.so.0.1
SONAME := libtaskcoroutines.so.0
DNDEBUG_ = -DNDEBUG #trailing space

DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
# Options Controlling the Preprocessor:
# -M(M(D)): instead of outputting the result of preprocessing, output a rule suitable for make describing the dependencies of the main source file. The preprocessor outputs one make rule containing the object file name for that source file, a colon, and the names of all the included files, including those coming from -include or -imacros command-line options
# -MP: an empty rule for header files. Scenario: a main file no longer depends on a header and this header is deleted, make will generate an error about a missing prerequisite before trying to compile anything
# -MF file: overrides the default dependency output filename
# -MT target: change the target of the rule emitted by dependency generation (without it the target contains the directory, e.g. build/taskcoroutines.o, and won't be used)

POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d
# We do this in a separate step so that failures during the compilation won’t leave a corrupted dependency file
# (is it even possible?)

vpath %.o $(objectdir)
vpath %.cpp $(sourcedir)
vpath %.s $(sourcedir)
vpath $(OUT_FILE) $(binarydir)
vpath $(SONAME) $(binarydir)

$(SONAME) : $(OUT_FILE)
	$(SYMLINK_COMMAND)

$(OUT_FILE) : $(OBJECTS) | $(binarydir)
	$(CXX) --shared -Wl,-soname=$(SONAME),--no-undefined $(objects_fullpath) -o $(binarydir)/$@

# The order in which pattern rules appear in the makefile is important since this is the order in which they are considered. Of equally applicable rules, only the first one found is used. The rules you write take precedence over those that are built in. Note however, that a rule whose prerequisites actually exist or are mentioned always takes priority over a rule with prerequisites that must be made by chaining other implicit rules !!!
%.o : %.cpp # cancel the built-in rule
%.o : %.cpp $(DEPDIR)/%.d | $(objectdir)
	$(CXX) -c $(DNDEBUG_)$(CXXFLAGS) $(DEPFLAGS) -I$(includedir) $< -o $(objectdir)/$@ && $(POSTCOMPILE)

%.o : %.s | $(objectdir)
	$(CXX) -c $(DEBUGFLAG) $< -o $(objectdir)/$@

$(objectdir) $(binarydir):
	mkdir -p $@

# Create a pattern rule with an empty recipe, so that make won’t fail if the dependency file doesn't exist (it's the case during the first attempt)
$(DEPDIR)/%.d: ;

.PRECIOUS: $(DEPDIR)/%.d
# Mark the dependency files precious to make, so they won’t be automatically deleted as intermediate files (despite that there's actually an empty recipe for them so make doesn't make them)
# https://www.gnu.org/software/make/manual/html_node/Chained-Rules.html

debug: $(SONAME)

# target specific variable (in effect for the target and for all of its prerequisites, and all their prerequisites)
debug: CXXFLAGS += -g
debug: DEBUGFLAG = -g
debug: DNDEBUG_ =

.PHONY: clean
clean:
	rm -f $(binarydir)/$(SONAME)* ; rm -f $(objectdir)/*.o

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(OBJECTS)))) # GNU make will attempt to rebuild the included makefile. If it is successfully rebuilt, GNU make will re-execute itself to read the new version
