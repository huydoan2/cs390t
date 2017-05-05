CC=clang++
#CFLAGS			+= -gdwarf-2
CFLAGS          +=		$$(pkg-config --cflags starpu-1.2)
LDFLAGS         +=      $$(pkg-config --libs starpu-1.2)

OBJ1 = sample_sort.o
PROG1 = sample_sort

all : $(PROG1)

%.o: %.cpp $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(PROG1): $(OBJ1)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	$(RM) *.o	
	$(RM) $(PROG1)

