target = libmediastreamer.a
includes=-I./mediastreamer -I../ortp/ortp -I../ortp -I../.. -I.
flags= -nostdlib -DNO_FFMPEG
#flags= -nostdlib -lc -lm -ldl -DVIDEO_ENABLED -DNO_FFMPEG
ms_objects = *.c

CC = arm-linux-gcc
	
mediastreamer.o : $(ms_objects)
	$(CC) -c $(ms_objects) $(includes) $(flags)
	ar rcs $(target) *.o
	cp $(target) ../ 
	
clean:
	rm *.o
