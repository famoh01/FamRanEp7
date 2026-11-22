CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O0 -Iinclude

RENDER_SRCS = src/fvf.cpp src/null_renderer.cpp

test: tests/test_renderer
	./tests/test_renderer

tests/test_renderer: tests/test_renderer.cpp $(RENDER_SRCS) include/ran/render/*.h
	$(CXX) $(CXXFLAGS) -o $@ tests/test_renderer.cpp $(RENDER_SRCS)

clean:
	rm -f tests/test_renderer

.PHONY: test clean
