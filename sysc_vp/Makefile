.PHONY: all clean

all: sysc_vp

sysc_vp:
	@echo "Building the SystemC VP"
	@mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j24

clean:
	@echo "Cleaning the build directory"
	@if [ -d "build" ]; then rm -rf build; fi
