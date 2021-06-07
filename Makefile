.PHONY: release debug clean docker

all: release

debug: build/debug
	cmake --build build/debug -j $(shell nproc) --verbose -- --no-print-directory

release: build/release
	cmake --build build/release -j $(shell nproc) -- --no-print-directory

build/debug:
	cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug

build/release:
	cmake -B build/release -DCMAKE_BUILD_TYPE=Release

clean:
	rm -rf build

docker:
	git archive HEAD | docker build - -t iqdb -f Dockerfile
