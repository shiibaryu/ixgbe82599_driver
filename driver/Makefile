objs = app.o init.o lib.o vfio.o stats.o

app: $(objs)
	gcc -Wall -O2 -o app $(objs)

app.o: app.c
	gcc -c app.c

init.o: init.c
	gcc -c init.c

lib.o: lib.c
	gcc -c lib.c

vfio.o: vfio.c
	gcc -c vfio.c

stats.o: stats.c
	gcc -c stats.c

app.o: init.h lib.h vfio.h stats.h ixgbe.h

.PHONY: clean
clean:
	rm -f hello $(objs)
