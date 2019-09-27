objs = recv.o init.o lib.o vfio.o stats.o pci.o

recv: $(objs)
	gcc -Wall -O2 -o recv $(objs)

recv.o: recv.c
	gcc -c recv.c

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

recv.o: init.h lib.h vfio.h stats.h pci.h ixgbe.h

.PHONY: clean
clean:
	rm -f recv $(objs)
