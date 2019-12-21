/*
 *  jcp.c - a simple cp by Jason Gurtz-Cayla
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#define BUF_SIZE 512

void usage(void);           /* prints help info */
void die(int*, int*, int);  /* close file handles and exit with given error code */

int main(int argc, char** argv) {
    char* file_rd, file_wr;                 /* filenames */
    int fd_in, fd_out, bc_read, bc_wrote;   /* file descriptors, byte counters */
    struct stat file_stat;                  /* File meta-info, used for perms */
    char buf[BUF_SIZE];                     /* Temp buffer for data copying */

    if (argc < 3) {
        usage();
        exit(1);
    }
    else if ( 2 == argc &&
              0 == strncmp(argv[1], "-h", 2) )
    {
        usage();
        exit(0);
    }
    else {
        fd_in = open(argv[1], O_RDONLY);
        if (fd_in < 0) {
            dprintf(2, "Could not open file %s for reading; %s\n", argv[1],
                strerror(errno));
            die(&fd_in, &fd_out, 1);
        }

        /* Store file permisions of origin file, in case destination file
         * needs creating
         */
        fstat(fd_in, &file_stat);

        /* creat(2) obsoleted by use of O_CREAT flag with open()
         * O_CREAT creates file if it doesn't exist, otherwise ignored
         * O_TRUNC ensures clear file if file is created, otherwise ignored
         * Third arg is the permisions to create file with, ignored if file exists
         */
        fd_out = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, file_stat.st_mode);
        if (fd_out < 0) {
            dprintf(2, "Could not open or create file %s for writing; %s\n",
                argv[2], strerror(errno));

            die(&fd_in, &fd_out, 1);
        }

        while ( (bc_read = read(fd_in, buf, BUF_SIZE)) > 0 ) {  /* copying... */
            bc_wrote = write(fd_out, buf, bc_read);

            if ( bc_wrote < 0) { /* write error happened... */
                dprintf(2, "Error occured writing to file %s; %s\n", argv[2],
                    strerror(errno));

                die(&fd_in, &fd_out, 3);
            }

            /* 
             * space added for printing
             */

            if (bc_wrote != bc_read) { /* something bad happened... */
                dprintf(2, "Wrote different amount of bytes to file %s than read from file %s. Copy is not good.; %s\n",
                    argv[2], argv[1],
                    strerror(errno));

                die(&fd_in, &fd_out, 255);
            }
        }

        if ( bc_read < 0) { /* read error happened... */
            dprintf(2, "Error occured reading from file %s; %s\n", argv[1],
                strerror(errno));

            die(&fd_in, &fd_out, 2);
        }
        exit(0);
    }
}

void die(int* fdi, int* fdo, int exit_code) {
    if (fdi) {
        close(*fdi);
    }
    if (fdo) {
        close(*fdo);
    }

    exit(exit_code);
}

void usage() {
    printf("jcp - copy a file to another\n\n");
    printf("Usage: \n");
    printf("    jcp [-h] origin_file destination_file\n\n");
    printf("    -h print this help message; files are ignored.\n\n");
    printf("Behavior: \n");
    printf("    If destination file exists, it will be overwritten.\n\n");
    printf("    If destination file doesn't exist, it will be created with the permissions of the origin.\n\n");
}
