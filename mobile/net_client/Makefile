# mobile/net_client/Makefile
# test: starts the RAN MOBILE server (ran_mobile/Server) on a local port, runs the client
# integration test against it, then stops the server. Needs python3 only.
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1
PORT ?= 8197
PIDFILE := /tmp/net_client_server.pid
DB := /tmp/net_client_test.db
SRC := api_client.cpp http_client.cpp

all: test_net

test_net: test_api_client.cpp $(SRC) api_client.h http_client.h
	$(CXX) $(CXXFLAGS) -o $@ test_api_client.cpp $(SRC)

test: test_net
	@rm -f $(DB)
	@(cd ../../ran_mobile/Server && exec python3 -m ranserver --db $(DB) --host 127.0.0.1 --port $(PORT) >/dev/null 2>&1) & echo $$! > $(PIDFILE)
	@./test_net 127.0.0.1 $(PORT); rc=$$?; kill `cat $(PIDFILE)` 2>/dev/null; rm -f $(PIDFILE); exit $$rc

clean:
	rm -f test_net

.PHONY: all test clean
