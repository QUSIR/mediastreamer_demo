target = libortp.a
includes=-I ./ortp
flags=-DHAVE_CONFIG_H
ortp_objects = *.c

CC = arm-linux-gcc

ortp.o : $(ortp_objects)
	$(CC) -c $(ortp_objects) $(includes) $(flags)
	ar rcs $(target) *.o
	cp $(target) ../ 
	
clean:
	rm *.o
