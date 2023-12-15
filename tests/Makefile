.PHONY: test libco

all: libco-test-64 libco-test-32

test: libco all
	@echo "==== TEST 64 bit ===="
	@LD_LIBRARY_PATH=../src ./libco-test-64
	@echo "==== TEST 32 bit ===="
	@LD_LIBRARY_PATH=../src ./libco-test-32

libco-test-64: main.c

libco:
	@cd ../src && make -s

libco-test-64: main.c
	gcc -I../src -L../src -m64 main.c -o libco-test-64 -lco-64

libco-test-32: main.c
	gcc -I../src -L../src -m32 main.c -o libco-test-32 -lco-32
clean:
	rm -f libco-test-*
