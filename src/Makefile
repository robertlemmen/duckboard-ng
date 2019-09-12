TARGET:=duckboard

WARN?=-Wall -Wextra -Wcast-align -Wcast-qual -Wctor-dtor-privacy \
	-Wdisabled-optimization -Wformat=2 -Winit-self -Wlogical-op \
	-Wmissing-include-dirs -Wnoexcept -Woverloaded-virtual \
	-Wsign-promo -Wstrict-null-sentinel -Wstrict-overflow=5 -Wundef \
	-Wno-unused-parameter -Wreturn-type -Wno-ctor-dtor-privacy \
	-Wno-overloaded-virtual -Wno-undef
CXXFLAGS?=-g $(WARN) -std=c++17 -DBOOST_ALL_DYN_LINK 
CXXINCFLAGS?=-Iolib/include
LDFLAGS?=-Lolib/lib -Wl,-rpath=olib/lib \
	-pthread -lboost_thread -lboost_log \
	-lboost_log_setup -lboost_program_options \
	-lboost_system -lboost_filesystem 
CXX?=g++

GIT_HASH := $(shell git log -1 --pretty=format:g%h)
GIT_DIRTY := $(shell git describe --all --long --dirty | grep -q dirty && echo 'dirty' || true)
GIT_TAG := $(shell git describe --exact-match 2>/dev/null || true)
VERSION_STRING := $(if $(GIT_TAG),$(GIT_TAG),$(GIT_HASH))$(if $(GIT_DIRTY), (dirty),)

OBJECTS=$(subst .cc,.o,$(shell ls *.cc))

$(TARGET): $(OBJECTS)
	@cat version.cc.template | sed -e 's/\%VERSION_STRING\%/$(VERSION_STRING)/' > version.cc
	@echo "Linking $@..."
	@$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $^ version.cc
	@rm -f version.cc

%.o: %.cc
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(CXXINCFLAGS) -c $< -o $@
	@$(CXX) -MM -MT $@ $(CXXFLAGS) $(CXXINCFLAGS) -c $< > $*.d

.PHONY: clean

clean:
	@echo "Cleaning..."
	@rm -f version.cc
	@rm -f *.o *.d
	@rm -f $(TARGET)

-include *.d
