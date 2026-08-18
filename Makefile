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
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	cp mdtree.1 $(DESTDIR)$(PREFIX)/share/man/man1/mdtree.1
	chmod 644 $(DESTDIR)$(PREFIX)/share/man/man1/mdtree.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/mdtree.1
