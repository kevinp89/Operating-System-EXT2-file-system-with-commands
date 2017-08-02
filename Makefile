CFLAGS = -Wall -g

all: ext2_cp ext2_mkdir ext2_ln ext2_rm ext2_restore ext2_checker

ext2_% : ext2_%.c
	gcc $(CFLAGS) -o $@ $<

clean:
	rm -f *.o ext2_cp ext2_mkdir ext2_ln ext2_rm ext2_restore ext2_checker
