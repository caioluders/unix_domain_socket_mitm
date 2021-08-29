CC=gcc

udsmitm: udsmitm.c
	$(CC) -pthread udsmitm.c -o udsmitm 
