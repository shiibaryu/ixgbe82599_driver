objs = through.o init.o lib.o vfio.o stats.o pci.o

through: $(objs)
	gcc -Wall -O2 -g -o through $(objs) -lrt

through.o: through.c
	gcc -g -c -lrt through.c 

init.o: init.c
	gcc -c init.c

lib.o: lib.c
	gcc -c lib.c

vfio.o: vfio.c
	gcc -c vfio.c

stats.o: stats.c
	gcc -c stats.c

pci.o: pci.c
	gcc -c pci.c

through.o: init.h lib.h vfio.h stats.h pci.h ixgbe.h

.PHONY: clean
clean:
	rm -f app $(objs)
