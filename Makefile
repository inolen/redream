.PHONY: debug release relwithdebinfo format test

default: relwithdebinfo

ifndef JOBS
JOBS = 1
endif
override JOBS := -j $(JOBS)

build/debug/Makefile:
	cmake -H. -Bbuild/debug -DCMAKE_BUILD_TYPE=DEBUG

build/release/Makefile:
	cmake -H. -Bbuild/release -DCMAKE_BUILD_TYPE=RELEASE

build/relwithdebinfo/Makefile:
	cmake -H. -Bbuild/relwithdebinfo -DCMAKE_BUILD_TYPE=RELWITHDEBINFO

debug: build/debug/Makefile
	make -C build/debug $(JOBS)

release: build/release/Makefile
	make -C build/release $(JOBS)

relwithdebinfo: build/relwithdebinfo/Makefile
	make -C build/relwithdebinfo $(JOBS)

format: build/relwithdebinfo/Makefile
	make -C build/relwithdebinfo format

test: build/debug/Makefile
	make -C build/debug all_test
