CC=clang++
#CFLAGS			+= -gdwarf-2
#CFLAGS		+= -ggdb
CFLAGS          += $$(pkg-config --cflags starpu-1.2) -O3 
LDFLAGS         += $$(pkg-config --libs starpu-1.2)

OBJ1 = sample_sort.o
PROG1 = sample_sort

all : $(PROG1)

%.o: %.cpp $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $< 

$(PROG1): $(OBJ1)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) *.o	
	$(RM) $(PROG1)

