all:
	mkdir -p build
	cd build && PATH=/opt/trinity/bin:$$PATH cmake -DCMAKE_BUILD_TYPE=Release .. && PATH=/opt/trinity/bin:$$PATH make -j$$(nproc)

clean:
	rm -rf build

.PHONY: all clean
