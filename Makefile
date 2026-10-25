.PHONY: build test bench clean

build:
	$(MAKE) -C engine all

test:
	$(MAKE) -C engine run

bench:
	$(MAKE) -C engine run

clean:
	$(MAKE) -C engine clean

