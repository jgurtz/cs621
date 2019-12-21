/*
 *  wcc.c - a clone of "wc -c" by Jason Gurtz-Cayla
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define BUF_SIZE 512

void usage(void);
long getByteCount(int*);

int main(int argc, char** argv) {
    char* file;     /* filename */
    int fd;         /* File descriptor */
    long total = 0; /* total bytes read */
    int valid = 0;  /* number of valid files */
    long grand = 0; /* grand total of all files */

    if (argc == 1) { /* read from stdin */
        fd = 0; /* open/close uneeded with stdin */
        total = getByteCount( &fd );
        printf("%8ld\n", total);
    }
    else if ( 2 == argc &&
              0 == strncmp(argv[1], "-h", 2) )
    {
        usage();
        exit(0);
    }
    else { /* read from files */

        for (int i = 1; i < argc; i++) {
            file = argv[i];
            fd = open(file, O_RDONLY);

            if (fd < 0) { 
                dprintf(2, "Could not open file %s; %s\n", file, strerror(errno));
            }
            else {
                total = getByteCount( &fd );
                close(fd);
                grand += total;
                printf("%8ld %s\n", total, file);
                valid++;
            }
        }

        if (valid > 1) { /* if more than one file, display combined total as well */
            printf("%8ld total\n", grand);
        }
        exit(0);
    }
}

long getByteCount(int* f) {
    return lseek(*f, 0, 2); /* Seek from beginning to end of file */
}

void usage() {
    printf("wcc - Count number of single-byte characters\n\n");
    printf("Usage: \n");
    printf("    wcc [-h] [file ...]\n\n");
    printf("    -h Print this help message, files ignored\n\n");
    printf("If no files are specified, standard input is read and no file name is displayed.\n");
    printf("Input will be accepted until receiving EOF (usually ^D).\n\n");
    printf("If more than one valid file is given, also displays total number of characters\nin all files.\n\n");
}
