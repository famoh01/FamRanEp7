CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O0 -Iinclude

test: tests/test_path
	./tests/test_path

tests/test_path: tests/test_path.cpp src/path.cpp include/ran/platform/platform.h
	$(CXX) $(CXXFLAGS) -o $@ tests/test_path.cpp src/path.cpp

clean:
	rm -f tests/test_path

.PHONY: test clean
