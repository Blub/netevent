CXX = g++
CFLAGS = -Wall -pthread
LDFLAGS = -g -pthread

SOURCES = main.cpp reader.cpp write.cpp

ifneq ($(INOTIFY),"no")
	CFLAGS += -DWITH_INOTIFY
endif

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
