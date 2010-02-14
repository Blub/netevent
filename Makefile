CXX = g++
CFLAGS = -Wall
LDFLAGS = -g

SOURCES = main.cpp

all: build netevent

build:
	-mkdir build

build/%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $*.cpp -MMD -MF build/$*.d -MT $@

netevent: $(patsubst %.cpp,build/%.o,$(SOURCES))
	$(CXX) $(LDFLAGS) -o $@ $^

clean:
	-rm -rf build
	-rm -f netevent

-include build/*.d
