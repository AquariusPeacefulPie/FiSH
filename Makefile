CC = gcc
CFLAGS = -Wall -D_DEFAULT_SOURCE -std=c99 -g 
LDFLAGS = -g
TARGET = cmdline_test fish

all: $(TARGET)

cmdline.o: cmdline.c cmdline.h
	$(CC) -fPIC -c -o $@ $<

#Dynamic library
libcmdline: cmdline.o
	$(CC) -shared $< -o libcmdline.so

#Linking rules
cmdline_test: cmdline_test.o libcmdline
fish: fish.o libcmdline

%: %.o libcmdline
	$(CC) ${LDFLAGS} -L${PWD} $< -lcmdline -o $@

#Compilation rules for ".c" files 
cmdline_test.o: cmdline_test.c cmdline.h
fish.o: fish.c cmdline.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

#Clean intermediate files
clean:
	rm -f *.o

#Clean each generated files
mrproper: clean
	rm -f $(TARGET) *.so