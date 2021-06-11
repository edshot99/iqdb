.PHONY: release debug clean docker

release: build/release
	cmake --build --preset release

debug: build/debug
	cmake --build --preset debug

build/release:
	cmake --preset release

build/debug:
	cmake --preset debug

clean:
	rm -rf build

docker:
	git archive HEAD | docker build - -t iqdb -f Dockerfile
