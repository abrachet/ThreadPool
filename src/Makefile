TARGET = test
WARNING = -Wall -Wshadow --pedantic -Wvla 
#-Wno-nullability-extension
DEFINES = -DDEBUG
GCC = clang -g -std=c11 $(WARNING)


SRCS = internal_thpool.c thpool.c worker.c main.c

OBJS = $(SRCS:%.c=%.o)

.c.o:
	$(GCC) $(DEFINES) -c $< -o $@

$(TARGET): $(OBJS)
	$(GCC) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $(TARGET)

clean:
	rm -f $(OBJS)
