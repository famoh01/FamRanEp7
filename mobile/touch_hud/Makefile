CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O0

test_hud: test_hud.cpp touch_hud.h
	$(CXX) $(CXXFLAGS) -o $@ test_hud.cpp

test: test_hud
	./test_hud

clean:
	rm -f test_hud

.PHONY: test clean
