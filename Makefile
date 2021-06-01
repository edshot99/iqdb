.PHONY: release debug clean docker

all: debug

debug:
	cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug
	cmake --build build/debug --verbose

release:
	cmake -B build/release -DCMAKE_BUILD_TYPE=Release
	cmake --build build/release --verbose

clean:
	rm -rf build

docker:
	git archive HEAD | docker build - -t iqdb -f Dockerfile
