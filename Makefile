CXX=g++
CXXFLAGS=-Wall -Wextra -O2
TARGET=mdtree

SRCS=src/main.cpp src/utils.cpp src/parser.cpp
OBJS=$(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

PREFIX ?= /usr/local
DESTDIR ?=

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chmod +x $(DESTDIR)$(PREFIX)/bin/$(TARGET)
