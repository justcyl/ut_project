NAME := libco
all: $(NAME)-64.so $(NAME)-32.so
CFLAGS += -U_FORTIFY_SOURCE

SRCS   := $(shell find . -maxdepth 1 -name "*.c")
DEPS   := $(shell find . -maxdepth 1 -name "*.h") $(SRCS)
CFLAGS += -O1 -std=gnu11 -ggdb -Wall -Werror -Wno-unused-result -Wno-unused-value -Wno-unused-variable

.PHONY: all test clean

$(NAME)-64: $(DEPS) # 64bit binary
	gcc -m64 $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS) -fno-stack-protector
	
$(NAME)-32: $(DEPS) # 32bit binary
	gcc -m32 $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS) -fno-stack-protector
 
$(NAME)-64.so: $(DEPS) # 64bit shared library
	gcc -fPIC -shared -m64 $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS) -fno-stack-protector

$(NAME)-32.so: $(DEPS) # 32bit shared library
	gcc -fPIC -shared -m32 $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS) -fno-stack-protector

clean:
	rm -f $(NAME)-64 $(NAME)-32 $(NAME)-64.so $(NAME)-32.so