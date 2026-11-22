# mobile/assets/Makefile
# Host tests for the manifest-verified placeholder asset loader, plus a check that the generated
# placeholder files on disk match the generator (and their manifest).
CXX ?= g++ -std=c++17 -Wall -Wextra -Werror -O1
ASSET_DIR := ../android/app/src/main/assets

test_assets: test_assets.cpp asset_manifest.cpp asset_manifest.h
	$(CXX) -o $@ test_assets.cpp asset_manifest.cpp

all: test_assets
	./test_assets

# Same as `all`, plus the real generated files in the APK asset directory.
test: test_assets
	python3 make_placeholder_assets.py --check
	./test_assets $(ASSET_DIR)

clean:
	rm -f test_assets

.PHONY: all test clean
