BACKWR=backwr.o sysdep.o

.c.o:
	gcc -O -g -c $*.c

backwr:	$(BACKWR)
	gcc -o backwr $(BACKWR)

$(BACKWR): backup.h
