CC=gcc

udsmitm: udsmitm.c
	$(CC) -pthread -static udsmitm.c -o udsmitm 
