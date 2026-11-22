# mobile/net_client/Makefile
# test: starts the RAN MOBILE server (ran_mobile/Server) on a local port, runs the client
# integration test against it, then stops the server. Needs python3 only.
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1
PORT ?= 8197
# Short world idle timeout so the live test can let the session expire in seconds, not minutes.
IDLE_MS ?= 2500
PIDFILE := /tmp/net_client_server.pid
DB := /tmp/net_client_test.db
DB2 := /tmp/reconnect_test.db
SRC := api_client.cpp http_client.cpp
FLOW_SRC := api_client.cpp http_client.cpp world_state.cpp world_flow.cpp

all: test_net test_world test_map test_flow

test_net: test_api_client.cpp $(SRC) world_state.cpp world_state.h api_client.h http_client.h
	$(CXX) $(CXXFLAGS) -o $@ test_api_client.cpp $(SRC) world_state.cpp

test_world: test_world_state.cpp world_state.cpp world_state.h api_client.cpp http_client.cpp api_client.h http_client.h
	$(CXX) $(CXXFLAGS) -o $@ test_world_state.cpp world_state.cpp api_client.cpp http_client.cpp

test_map: test_world_map.cpp world_map.cpp world_map.h world_state.h
	$(CXX) $(CXXFLAGS) -o $@ test_world_map.cpp world_map.cpp

# World sync / reconnect / lifecycle state machine (no server, scripted transport).
test_flow: test_world_flow.cpp $(FLOW_SRC) world_flow.h world_state.h api_client.h http_client.h
	$(CXX) $(CXXFLAGS) -o $@ test_world_flow.cpp $(FLOW_SRC)

# Live reconnect test: needs a server whose world sessions expire quickly (--world-idle-s).
test_reconnect: test_reconnect.cpp $(FLOW_SRC) world_flow.h world_state.h api_client.h http_client.h
	$(CXX) $(CXXFLAGS) -o $@ test_reconnect.cpp $(FLOW_SRC)

test: test_net test_world test_map test_flow
	./test_world
	./test_map
	./test_flow
	@rm -f $(DB)
	@(cd ../../ran_mobile/Server && exec python3 -m ranserver --db $(DB) --host 127.0.0.1 --port $(PORT) >/dev/null 2>&1) & echo $$! > $(PIDFILE)
	@./test_net 127.0.0.1 $(PORT); rc=$$?; kill `cat $(PIDFILE)` 2>/dev/null; rm -f $(PIDFILE); exit $$rc

# Reconnect/lifecycle test against a real server (world sessions expire after IDLE_MS).
test-reconnect: test_reconnect
	@rm -f $(DB2)
	@(cd ../../ran_mobile/Server && exec python3 -m ranserver --db $(DB2) --host 127.0.0.1 --port $(PORT) \
	    --world-idle-s $$(python3 -c "print($(IDLE_MS)/1000.0)") >/dev/null 2>&1) & echo $$! > $(PIDFILE)
	@sleep 0.5
	@./test_reconnect 127.0.0.1 $(PORT) $(IDLE_MS); rc=$$?; kill `cat $(PIDFILE)` 2>/dev/null; rm -f $(PIDFILE); exit $$rc

clean:
	rm -f test_net test_world test_map test_flow test_reconnect

.PHONY: all test test-reconnect clean
