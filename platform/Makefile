CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O0 -Iinclude

UNIX_SRCS = src/path.cpp src/common_filesystem.cpp src/posix/timer_posix.cpp

test: tests/test_path tests/test_backends
	./tests/test_path
	./tests/test_backends

tests/test_path: tests/test_path.cpp src/path.cpp include/ran/platform/platform.h
	$(CXX) $(CXXFLAGS) -o $@ tests/test_path.cpp src/path.cpp

tests/test_backends: tests/test_backends.cpp $(UNIX_SRCS) include/ran/platform/backends.h include/ran/platform/platform.h
	$(CXX) $(CXXFLAGS) -o $@ tests/test_backends.cpp $(UNIX_SRCS)

clean:
	rm -f tests/test_path tests/test_backends

.PHONY: test clean
