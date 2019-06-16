FLAGS=-c -g -Wall -ansi -std=gnu99 -pedantic -errors

all: BibakBOXServer BibakBOXClient
	
BibakBOXServer: BibakBOXServer.o
	gcc BibakBOXServer.o -o BibakBOXServer -pthread
BibakBOXServer.o: BibakBOXServer.c
	gcc $(FLAGS) BibakBOXServer.c

BibakBOXClient: BibakBOXClient.o
	gcc BibakBOXClient.o -o BibakBOXClient
BibakBOXClient.o: BibakBOXClient.c
	gcc $(FLAGS) BibakBOXClient.c



clean:
	rm -rf *.o BibakBOXServer BibakBOXClient