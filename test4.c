/*
 * Test for concurrent writing 
 */

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <terminals.h>

void writer1(void *);
void writer2(void *);

char string1[] = "abcdefghijklmnopq\b\b\b\b\b\brstuvwxyz\n";
int length1 = sizeof(string1) - 1;

char string2[] = "\b\b\b\b0123\b\b\b\b\b\b\b456789\n";
int length2 = sizeof(string2) - 1;

int
main(int argc, char **argv)
{
    InitTerminalDriver();
    InitTerminal(1);

    if (argc > 1) HardwareOutputSpeed(1, atoi(argv[1]));
    if (argc > 2) HardwareInputSpeed(1, atoi(argv[2]));

    ThreadCreate(writer1, NULL);
    ThreadCreate(writer2, NULL);
    while(1);
    ThreadWaitAll();

    exit(0);
}

void
writer1(void *arg)
{
    int status;

    status = WriteTerminal(1, string1, length1);
    printf("Return from WriteTerminal in writer1\n");
    fflush(stdout);
    if (status != length1)
	fprintf(stderr, "Error: writer1 status = %d, length1 = %d\n",
	    status, length1);
}

void
writer2(void *arg)
{
    int status;

    status = WriteTerminal(1, string2, length2);
    printf("Return from WriteTerminal in writer2\n");
    fflush(stdout);
    if (status != length2)
	fprintf(stderr, "Error: writer2 status = %d, length2 = %d\n",
	    status, length2);
}
