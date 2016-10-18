target = libsilk.a
includes=-I ./
flags=
silk_objects = *.c

CC = arm-linux-gcc

ortp.o : $(ortp_objects)
	$(CC) -c $(silk_objects) $(includes) $(flags)
	ar rcs $(target) *.o
	cp $(target) ../ 
	
clean:
	rm *.o
