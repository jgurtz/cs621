/*
 *  jvol.c - a simple filesystem by Jason Gurtz-Cayla
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#include "container.h"      // contains data structures for sectors
#include "pathElements.h"   // A dynamic char array 
#include "userFile.h"       // Holds global state metadata of current open user file

#define BUF_SIZE 512                    // bytes
#define CONTAINER_SIZE (BUF_SIZE * 1000) // Sectors 0 - 99
#define CONTAINER_PERMS 00644           // wr--r--r-

#define CONTAINER_CREAT (O_CREAT | O_TRUNC | O_WRONLY) //overwrite allowed
#define CONTAINER_INIT (O_CREAT | O_EXCL | O_TRUNC | O_WRONLY)
#define CONTAINER_READ O_RDONLY
#define CONTAINER_WRITE O_WRONLY
#define CONTAINER_READWRITE O_RDWR

// CLI argument storage
struct Options {
    char* filename;
    int cmd;
    char* path;
    char* dst_path;
    char* src;
    char* output;
    int init;
    int cmdGiven;
};

/* Globals */
struct Options opt = {.filename=NULL, .init=0, .cmdGiven=0};
struct PathElements userPath = { .elementCount=0 };
struct PathElements userDstPath = { .elementCount=0 };
struct UserFile userFile = { .mode=' ', .name="         ", .rw_ptr=0 };
struct State currState = { .curr_sector=0, .free=0, .next_free=0, .arr_idx_sector=0, .arr_idx=0,
                           .file_first_sector=0, .file_last_sector_size=0 };

// User-looking functions : "file" is user data file or directory entry.
//  Only one file may be open at a time, state held in global struct userFile
void create_file(char);             // CREATE type, (name taken from global userPath struct)
void open_file(char, char*);        // OPEN mode, name (mode={I}nput, {O}utput [append], or {U}pdate [overwrite]) 
void close_file();                  // CLOSE 
void rm_file();                     // DELETE name (delete opt->path)
void read_file(int, short);         // READ file starting at given sector,up to given bytes of data in last sector and display on stdout
void write_2_file(int, int);        // WRITE writes to file iat sector at offset
void update_file(int, short);       // APPEND to file starting after given bytes of data in last sector.
void seek_file(int, double);        // SEEK base offset 
void ls_file();                     // like ls -l on the user-given path, calls ls_dir() on any input other than single user-file
void ls_dir(int);                   // Opens give sector and recurses to display contents

// FS Logic helper functions
int fileIdx_findUsed(struct Dir*);          // Search Dir->Idx for entries with .type != 'F' and return sector number (or -1)
int fileIdx_search(char*, struct Dir*);     // Search for string in Dir->Idx and return sector number (or -1)
int fileIdx_getArrIdx(struct Dir*);         // By all means return a free file index and store sector in state
int get2FreeSectors();                      // Returns free sector and stores that in currState
int getLastFree();                          // returns sector number of last free sector in container
int getFileSector();                        // returns sector num of last path element in userPath.elementArr
int getDirOfLastPathElementSector();        // returns sector num of next to last path element in userPath.elementArr
void append2FreeList();                     // Appends current block to end of free sector linked-list
int extendDir(int);                         // Creates a directory extention for given sector, return sector of extention
int extendFile(int);                        // Creates a file extention for given sector, return sector of extention
void reapDir(struct Dir*);                  // recursively returns a directory-type sector to end of free list
void reapFile(struct File*);                // recursively returns a file-type sector to end of free list

// CLI processing and UI
void usage(void);                               // prints help info
void handleArgs(int, char**);                   // GetOpt() processing
int parseCmd(char*);                            // String -> int mapping
void parsePath(struct PathElements*, char*);    // serialize a path string into an array with count

// Container file handling functions
int containerOpen(char*, int);                  // Open file with mode, creats file if necc.
void containerClose(int);                       // Close file descriptor
void containerInit();                           // Initialize container file

// Low-level data-handling functions
void sectorRead(char*, int, int);               // read into buffer, from filedescriptor, at sector offset
void sectorWrite(char*, int, int);              // write from buffer, to filedescriptor, at sector offset
void dir2buf(char*, struct Dir);                // Marshall Dir struct to buffer
void file2buf(char*, struct File);              // Marshall File struct to buffer
void buf2dir(char*, struct Dir*);               // Marshall buffer to Dir struct 
void buf2file(char*, struct File*);             // Marshall buffer to File struct

// Debugging & error handling functions
void die(int*, int);                            // close file handles and exit with given error code
void print_hex_memory(void*, int);


 /***********************************************************\
<                    Start Function Bodies                    > 
 \__________________________________________________________*/
int fileIdx_findUsed(struct Dir* d) {
    //  linear search fileIdx of directory-entry d for .type != 'F', returning linked sector number if found
    //  If not found, check for directory entry extension and recursively search
    //  If no directory extension (and not found) return -1

    for (int i=0; i<31; i++) {
        //DEBUG
        //printf("FindUsed: %d\t.type: %c\n", i, (char)d->Idx[i].type);

        if ( (char)&d->Idx[i].type != 'F' ) {
            //DEBUG
            //printf("Returning link: %d\n", d->Idx[i].link);
            return d->Idx[i].link;
        }
    }
    // Not found, check for directory extension and handle as needed
    if ( d->frwd != 0 ) {
        int fd = 0;         // File descriptor of container
        char buf[512] = {0};

        //DEBUG
        //printf("Checking dir extention at sector %d\n", d->frwd);
        fd = containerOpen(opt.filename, CONTAINER_READ);
        sectorRead( buf, fd, d->frwd );
        containerClose(fd);
        buf2dir(buf, d);

        return fileIdx_findUsed(d);
    }
    else {
        return -1;  // not found
    }
}

int fileIdx_search(char* pe, struct Dir* d) {
    // Given path-element name pe:
    //  linear search fileIdx of directory-entry d, returning linked sector number if found
    //  If not found, check for directory entry extension and recursively search
    //  If no directory extension (and not found) return -1
    // Populates:
    //  file_sector_type        // Found file is this sector type: D/U
    //  file_entry_idx;         // Found file is at this array index
    //  file_entry_idx_sector;  // Found file has dir entry in this sector

    //DEBUG
    //printf("Searching for %s\n", pe);

    for (int i=0; i<31; i++) {
        //DEBUG
        //printf("pe: %s\tIdx[%d].name: %s\t.type: %c\n", pe, i, (char *)d->Idx[i].name, (char)d->Idx[i].type);

        if ( strncmp( (char*)&d->Idx[i].name, pe, 9 ) == 0 ) {
            //DEBUG
            //printf("Returning link: %d\n", d->Idx[i].link);
            currState.file_sector_type = d->Idx[i].type;
            currState.file_entry_idx = i;
            currState.file_entry_idx_sector = currState.curr_sector;
            return d->Idx[i].link;
        }
    }
    // Not found, check for directory extension and handle as needed
    if ( d->frwd != 0 ) {
        int fd = 0;         // File descriptor of container
        char buf[512] = {0};

        //DEBUG
        //printf("Checking dir extention at sector %d\n", d->frwd);
        fd = containerOpen(opt.filename, CONTAINER_READ);
        sectorRead( buf, fd, d->frwd );
        containerClose(fd);
        buf2dir(buf, d);

        return fileIdx_search(pe, d);
    }
    else {
        currState.file_sector_type = ' ';
        currState.file_entry_idx = 0;
        currState.file_entry_idx_sector = 0;


        return -1;  // not found
    }
}

int fileIdx_getArrIdx(struct Dir* d) {
    /* Searchs loaded directory entry for a free file index. If full, will create
     *  a directory extention and return the zeroth entry. Uses "first free" method
     *  because files may be removed, leaving free entries in the middle of an extention
     *  chain
     *
     * Returns the free arr idx and updates these global state elements:
     *  int arr_idx_sector;     // Sector number having free directory entry index
     *  int arr_idx;            // number of free directory entry index
    */
    int fd = 0;         // File descriptor of container
    char buf[512] = {0};

    for (int i=0; i<31; i++) {

        if (d->Idx[i].type == 'F') {
            currState.arr_idx_sector = currState.curr_sector;
            currState.arr_idx = i;

            return i;
        }
    }
    // Not found, check for directory extension and recurse to continue search
    if ( d->frwd != 0 ) {
        //DEBUG
        //printf("Checking dir extention at sector %d\n", d->frwd);

        fd = containerOpen(opt.filename, CONTAINER_READ);
        sectorRead(buf, fd, d->frwd);
        containerClose(fd);
        buf2dir(buf, d);

        return fileIdx_getArrIdx(d);
    }
    else { // need to create dir extention
        int priorDir = currState.curr_sector;
        //DEBUG
        //printf("Creating dir extention at sector %d\n", currState.free);

        // chg frwd to currState.free
        d->frwd = currState.free;
        dir2buf(buf, *d);
        fd = containerOpen(opt.filename, CONTAINER_READWRITE);
        sectorWrite(buf, fd, currState.curr_sector);

        // Update currState.free sector
        sectorRead(buf, fd, currState.free);
        buf2dir(buf, d);
        currState.arr_idx_sector = currState.curr_sector;
        currState.arr_idx = 0;
        d->frwd = 0;
        d->back = priorDir;
        dir2buf(buf, *d);
        sectorWrite(buf, fd, currState.curr_sector);

        // update root sector
        sectorRead(buf, fd, 0);
        buf2dir(buf, d);
        d->free = currState.next_free;
        dir2buf(buf, *d);
        sectorWrite(buf, fd, currState.curr_sector);
        get2FreeSectors();

        // reset the free sector state and load extention sector
        sectorRead(buf, fd, currState.arr_idx_sector);
        buf2dir(buf, d);


        return currState.arr_idx;
    }
}

int get2FreeSectors() {
    //Updates global state with next two free sector numbers
    //Nice to have the 2nd when using a free sector to simplify updating root.free
    int fd = 0;                                 // File descriptor of container
    int orig_sector = currState.curr_sector;    // Restore this sector in ram when done
    char buf[512] = {0};
    struct Dir d;

    fd = containerOpen(opt.filename, CONTAINER_READ);
    sectorRead(buf, fd, 0);
    buf2dir(buf, &d);
    currState.free = d.free;

    sectorRead(buf, fd, currState.free);
    buf2dir(buf, &d);

    if (currState.free == 0 ) {
        //if free is zero, then there is no next_free
        // in this case, d.frwd would be root dir extention
        currState.next_free = 0;
    }
    else {
        currState.next_free = d.frwd;
    }

    // Restore original sector in ram
    sectorRead(buf, fd, orig_sector);
    buf2dir(buf, &d);
    containerClose(fd);

    return currState.free;
}

void ls_dir(int sector) {
    int fd = 0;
    struct Dir d;
    char buf[512] = {0};

    fd = containerOpen(opt.filename, CONTAINER_READ);
    sectorRead(buf, fd, sector);
    buf2dir(buf, &d);

    for (int i=0; i<31; i++) {

        switch (d.Idx[i].type) {
            case 'D':
                printf("\tDirectory\t%s\n", d.Idx[i].name);
                break;
            case 'U':
                printf("\tUserFile\t%s\n", d.Idx[i].name);
                break;
            case 'F': // no print
                break;
        }
    }

    if (d.frwd != 0) {
        ls_dir(d.frwd);
    }
    containerClose(fd);
}

void ls_file() {
    int fd = 0;
    struct Dir d;
    char buf[512] = {0};
    int containingDir, fileDir = 0;

    containingDir = getDirOfLastPathElementSector();
    fd = containerOpen(opt.filename, CONTAINER_READ);
    sectorRead(buf, fd, containingDir);
    containerClose(fd);
    buf2dir(buf, &d);

    if (userPath.elementCount == 0) {
        fileDir = 0; //no path given so assume root dir
    }
    else {
        fileDir = fileIdx_search(userPath.elementArr[ userPath.elementCount - 1 ], &d);
    }

    //  file_sector_type        // Found file is this sector type: D/U
    //  file_entry_idx;         // Found file is at this array index
    //  file_entry_idx_sector;  // Found file has dir entry in this sector
    printf("\tFileType\tFileName\n");  // Column header
    printf("\t^^^^^^^^\t^^^^^^^^\n");  // Column header

    //DEBUG
    //printf("ContainingDir: %d, Sector#: %d, sector type: %c\n", containingDir, fileDir, currState.file_sector_type);

    switch (currState.file_sector_type) {
        case 'U':
            printf("\tUserFile\t%s\n", userPath.elementArr[ userPath.elementCount - 1 ]);
            break;
        case 'D':
        default:    // for root dir type is undefined
            // ls_dir will recursively ls files and dirs
            ls_dir(fileDir);
            break;
    }
}

int extendDir(int sector) {
    int fd = 0;         // File descriptor of container
    char buf[512] = {0};
    int newSector = 0;
    struct Dir d; 


    newSector = get2FreeSectors();

    fd = containerOpen(opt.filename, CONTAINER_READWRITE);
    sectorRead(buf, fd, sector);
    buf2dir(buf, &d);

    // Point existing file to extention sector
    d.frwd = newSector;
    dir2buf(buf, d);
    sectorWrite(buf, fd, sector);

    // Create new empty extention
    d.back = sector;
    d.frwd=0x00000000;
    d.free=0xADDEADDE;
    d.filler=0xEFBEEFBE ;
    struct FileIDX file_idx[31];

    for (int i=0; i<31; i++) {
        file_idx[i].link = 0x00000000;
        file_idx[i].type = 'F';
        strncpy(file_idx[i].name, "         \0", 10);
        file_idx[i].size = 0x0000;
    }
    memcpy( &d.Idx, file_idx, sizeof(d.Idx) );
    dir2buf(buf, d);
    sectorWrite(buf, fd, newSector);

    return newSector;
}

int extendFile(int sector) {
    int fd = 0;         // File descriptor of container
    char buf[512] = {0};
    struct File f = { .data={0}, .back=0, .frwd=0 };
    struct Dir d;
    int newSector = 0;

    newSector = get2FreeSectors();

    fd = containerOpen(opt.filename, CONTAINER_READWRITE);
    sectorRead(buf, fd, sector);
    buf2file(buf, &f);

    // Point existing file to extention sector
    f.frwd = newSector;
    file2buf(buf, f);
    sectorWrite(buf, fd, sector);

    // Create new empty extention
    f.back = sector;
    f.frwd = 0;
    memset(f.data, 0, 504);
    file2buf(buf, f);
    sectorWrite(buf, fd, newSector);

    // Update root.free
    sectorRead(buf, fd, 0);
    buf2dir(buf, &d);
    d.free = currState.next_free;
    dir2buf(buf, d);
    sectorWrite(buf, fd, 0);


    return newSector;
}

void create_file(char type) {      // CREATE type, (name taken from global userPath)
    int fd = 0;         // File descriptor of container
    char buf[512] = {0};
    struct File f = { .data={0}, .back=0, .frwd=0 };
    struct FileIDX file_idx[31];    // for creating new file index
    struct Dir d;
    int origSector = currState.curr_sector;
    int dirSector = 0;  // holds link returned by search

    fd = containerOpen(opt.filename, CONTAINER_READWRITE);
    sectorRead(buf, fd, 0);
    buf2dir(buf, &d);
    get2FreeSectors();

    if (currState.free == 0) {
        printf("No free sectors!\n");
        exit(255);
    }
            
    for (int i=0; i<userPath.elementCount; i++) {
        //DEBUG
        //printf("Searching for %s in sector %d\n", userPath.elementArr[i], currState.curr_sector);

        dirSector = fileIdx_search( userPath.elementArr[i], &d ); 

        //printf("DirSector: %d\n", dirSector);

        if ( dirSector < 0 ) {
            // Not found; mkdir() or touch()
            int arr_idx = fileIdx_getArrIdx(&d);

            printf("Creating file %s in sector %d at arr_idx %d\n", userPath.elementArr[i], currState.free, arr_idx);
            file_idx[arr_idx].link = currState.free;

            if (type == 'D') {
                file_idx[arr_idx].type = 'D';
            }
            else {
                file_idx[arr_idx].type = 'U';
            }
            strncpy(file_idx[arr_idx].name, userPath.elementArr[i], 9 ); 
            file_idx[arr_idx].size = 0x0000;
            memcpy( &d.Idx[arr_idx], &file_idx[arr_idx], sizeof(d.Idx[arr_idx]) );

            dir2buf(buf, d);
            sectorWrite(buf, fd, currState.arr_idx_sector); //currState.curr_sector);

            // Load new dir for remaining
            sectorRead(buf, fd, currState.free);

            if (type == 'D') {
                buf2dir(buf, &d);
                d.frwd = 0;

                for (int i=0; i<31; i++) {
                    //snprintf(tmpStr, 10, "%d", i);
                    file_idx[i].link = 0x00000000;
                    file_idx[i].type = 'F';
                    strncpy(file_idx[i].name, "         \0", 10);
                    file_idx[i].size = 0x0000;
                }
                memcpy( &d.Idx, file_idx, sizeof(d.Idx) );
                dir2buf(buf, d);
            }
            else if (type == 'U') {
                file2buf(buf, f); // File struct pre-initialized so simple
            }
            else {
                printf("Somehow called create_file() with non-D/U type, exiting\n");
                exit(255);
            }
            sectorWrite(buf, fd, currState.curr_sector);

            // Update root dir .free
            sectorRead(buf, fd, 0);
            buf2dir(buf, &d);
            d.free = currState.next_free;
            dir2buf(buf, d);
            sectorWrite(buf, fd, 0);

            // load created file before ending
            sectorRead(buf, fd, currState.free);
            if (type == 'D') {
                buf2dir(buf, &d);
            }
            else if (type == 'U') {
                buf2file(buf, &f);
            }
        }
        else if ( dirSector == 0 ) {
            //issue as nothing should point to root sector
            printf("Link to root directory found searching for %s. Exiting\n", userPath.elementArr[i]);
        }
        else {
            // if at the last element, recreate per spec, else move on

            if (i == userPath.elementCount -1) {
                printf("Element already exists, recreating...\n");
                rm_file();
                
                if (type == 'D') {
                    create_file('D');
                }
                else if (type == 'U') {
                    create_file('U');
                }
            }
            else { // just load dir and go to next
                sectorRead(buf, fd, dirSector);
                buf2dir(buf, &d);
            }
        }
        get2FreeSectors();
    }
    containerClose(fd);
}

void open_file(char mode, char* name) {     // OPEN mode, name (mode={I}nput (overwrite), {O}utput [display], or {U}pdate [append]) 
    //TODO: for listing or updating user-file (type U) content
    int fd = 0;  //file descriptor
    struct Dir d;
    struct File f;
    char buf[BUF_SIZE] = {0};
    int sector, dirSector = 0;
    short size = 0;

    dirSector = getDirOfLastPathElementSector();

    fd = containerOpen(opt.filename, CONTAINER_READWRITE);
    sectorRead(buf, fd, dirSector);

    buf2dir(buf, &d);

    for (int i=0; i<31; i++) {

        if ( strncmp( d.Idx[i].name, name, 9 ) == 0 ) {
            size = d.Idx[i].size;
        }
    }
    
    if ( (sector = getFileSector()) == -1 ) {
        create_file('U');
        sector = getFileSector();
    }
    //sectorRead(sectBuf, fd_out, sector);
    //buf2file(sectBuf, &f);

    switch (mode) {
        case 'I':
            write_2_file(sector, 0);
            break;
        case 'O':
            read_file(sector, size);
            break;
        case 'A':
            sectorRead(buf, fd, sector);
            buf2file(buf, &f);
            
            if (f.frwd == 0) {
                //DEBUG
                printf("single sector file append!\n");
                printf("Sec: %d, oset: %d\n", sector, 0);
                write_2_file(sector, size + 1);
            }
            else {

                while (f.frwd != 0) {
                    sector = f.frwd;
                    sectorRead(buf, fd, sector);
                    buf2file(buf, &f);
                }
                //DEBUG
                printf("multi-sector file append!\n");
                printf("Sec: %d, oset: %d\n", sector, size+1);
                write_2_file(sector, size + 1);
            }
            break;
    }
}

void close_file() {                         // CLOSE 
    //TODO: simply re-init global struct userFile
}

void append2FreeList() {
    int fd = 0;         // File descriptor of container
    int block2append = currState.curr_sector;
    char buf[512] = {0};
    struct Dir d;
    fd = containerOpen(opt.filename, CONTAINER_READWRITE);

    // update original last-free sector
    sectorRead(buf, fd, currState.last_free);
    buf2dir(buf, &d);

    if (currState.last_free == 0) {
        // container is 100% used, start over by updating sectorZero.free
        d.free = block2append;
    }
    else {
        // append as normal
        d.frwd = block2append;
    }
    dir2buf(buf, d);
    //DEBUG
    //printf("App2Fre:\tCurrSector: %d, Blk2App: %d\n", currState.curr_sector, block2append);

    sectorWrite(buf, fd, currState.curr_sector);

    // Create new empty dir 
    d.back = 0x00000000;
    d.frwd = 0x00000000;
    d.free = 0xADDEADDE;
    d.filler = 0xEFBEEFBE;
    struct FileIDX file_idx[31];

    for (int i=0; i<31; i++) {
        file_idx[i].link = 0x00000000;
        file_idx[i].type = 'F';
        strncpy(file_idx[i].name, "         \0", 10);
        file_idx[i].size = 0x0000;
    }

    memcpy( &d.Idx, file_idx, sizeof(d.Idx) );
    dir2buf(buf, d);
    sectorWrite(buf, fd, block2append);

    currState.last_free = getLastFree();;

    containerClose(fd);
}

int getLastFree() {
    //returns sector number of last free sector in container
    int fd = 0;         // File descriptor of container
    int lastFree = 0;
    char buf[512] = {0};
    struct Dir d;

    fd = containerOpen(opt.filename, CONTAINER_READ);
    sectorRead(buf, fd, 0);
    buf2dir(buf, &d);

    if (d.free == 0) {
        containerClose(fd);
        return 0;
    }
    else {
        lastFree = d.free;
        //DEBUG
        //printf("1. lastFree: %d, d.free: %d, d.frwd: %d\n", lastFree, d.free, d.frwd);

        sectorRead(buf, fd, lastFree);
        buf2dir(buf, &d);
        //DEBUG
        //printf("2. lastFree: %d, d.free: %d, d.frwd: %d\n", lastFree, d.free, d.frwd);

        while (d.frwd != 0) {
            //DEBUG
            //printf("lastFree: %d, d.frwd: %d", lastFree, d.frwd);

            lastFree = d.frwd;
            sectorRead(buf, fd, d.frwd);
            buf2dir(buf, &d);

            //DEBUG
            //printf("free sec.: %d", lastFree);
        }
    }
    containerClose(fd);
    //DEBUG
    //printf("3. lastFree: %d, d.free: %d, d.frwd: %d\n", lastFree, d.free, d.frwd);

    return lastFree;
}

int getFileSector() {
    // returns sector num of last path element in userPath.elementArr
    int parentDir = 0;

    if (userPath.elementCount == 0) {
        return 0;   //no path given so assume root dir
    }
    parentDir = getDirOfLastPathElementSector();

    //DEBUG
    //printf("got parent dir sector: %d\n", parentDir);

    int fd = 0;     // File descriptor of container
    char buf[512] = {0};
    struct Dir d;

    fd = containerOpen(opt.filename, CONTAINER_READ);
    sectorRead(buf, fd, parentDir);
    buf2dir(buf, &d);

    return fileIdx_search(userPath.elementArr[ userPath.elementCount - 1 ], &d);
}

int getDirOfLastPathElementSector() {
    // returns sector num of next to last path element in userPath.elementArr
    int fd = 0;         // File descriptor of container
    char buf[512] = {0};
    struct Dir d;
    int dirSector = 0;  // holds link returned by search; -1 == not found

    if (userPath.elementCount == 0) {
        return 0; //no path given so assume root dir
    }

    fd = containerOpen(opt.filename, CONTAINER_READ);
    sectorRead(buf, fd, 0);
    buf2dir(buf, &d);

    for (int i=0; i<userPath.elementCount-1; i++) {
        dirSector = fileIdx_search( userPath.elementArr[i], &d ); 

        if ( dirSector < 0 ) {
            // Not found; user typo
            printf("File or directory %s not found in %s\n", userPath.elementArr[i], opt.path);
            containerClose(fd);
            exit(1);
        }
        else {
            sectorRead(buf, fd, dirSector);
            buf2dir(buf, &d);
            //dirSector = fileIdx_search( userPath.elementArr[i], &d ); 
        }
    }
    containerClose(fd);
    //DEBUG
    //printf("Container for element %s would be sector %d\n", userPath.elementArr[ userPath.elementCount-1 ], dirSector);

    return dirSector;
}

void reapDir(struct Dir* d) {
    int fd = 0;         // File descriptor of container
    int sector2free = 0;
    int origSector2free = currState.curr_sector;
    char buf[512] = {0};
    fd = containerOpen(opt.filename, CONTAINER_READWRITE);

    for (int i=0; i<31; i++) {
        sector2free = d->Idx[i].link; 
        switch (d->Idx[i].type) {
            case 'D':
                //Free dir entry
                d->Idx[i].type = 'F';
                d->Idx[i].link = 0;
                d->Idx[i].size = 0x0000;
                strncpy(d->Idx[i].name, "         \0", 10);
                dir2buf(buf, *d);
                sectorWrite(buf, fd, currState.curr_sector);

                //now deal with sub-dir
                sectorRead(buf, fd, sector2free);
                buf2dir(buf, d);
                reapDir(d);
                break;
            case 'U':
                //Free dir entry
                d->Idx[i].type = 'F';
                d->Idx[i].link = 0;
                d->Idx[i].size = 0x0000;
                strncpy(d->Idx[i].name, "         \0", 10);
                dir2buf(buf, *d);
                sectorWrite(buf, fd, currState.curr_sector);

                //now deal with file
                struct File f;
                sectorRead(buf, fd, sector2free);
                buf2file(buf, &f);
                reapFile(&f);
                break;
        }
    }

    if (d->frwd != 0) {
        sectorRead(buf, fd, d->frwd);
        buf2dir(buf, d);
        reapDir(d);
    }
    append2FreeList();
    sectorRead(buf, fd, origSector2free);
    buf2dir(buf, d);
    append2FreeList();
    containerClose(fd);
}

void reapFile(struct File* f) {
    int fd = 0;         // File descriptor of container
    char buf[512] = {0};
    fd = containerOpen(opt.filename, CONTAINER_READWRITE);

    if (f->frwd != 0) {
        sectorRead(buf, fd, f->frwd);
        buf2file(buf, f);
        reapFile(f);
    }
    append2FreeList();
    containerClose(fd);
}

void rm_file() {   // DELETE name (deletes last element of opt->path and subordinates)
    int fd = 0;         // File descriptor of container
    char buf[512] = {0};
    struct Dir d;
    int dirSector = 0;  // holds link returned by search
    int sector2free = 0;
    char* file2rm = userPath.elementArr[ userPath.elementCount - 1 ];
    int fileEntrySector = 0;    // sector num where directory entry of file2rm exists

    fd = containerOpen(opt.filename, CONTAINER_READWRITE);
    currState.last_free = getLastFree(); //Freed sectors appended to the end
    //DEBUG
    //printf("Found Last Free: %d\n", currState.last_free);

    dirSector = getDirOfLastPathElementSector();
    //DEBUG
    //printf("Found container Dir @ sector: %d\n", dirSector);

    sectorRead(buf, fd, dirSector);
    buf2dir(buf, &d);
    //DEBUG
    //printf("currSector: %d\n", currState.curr_sector);

    //  file_sector_type        // Found file is this sector type: D/U
    //  file_entry_idx;         // Found file is at this array index
    //  file_entry_idx_sector;  // Found file has dir entry in this sector
    sector2free = fileIdx_search(file2rm, &d);
    //DEBUG
    //printf("FileEntrySector: %d, at index %d\n", currState.file_entry_idx_sector, currState.file_entry_idx);

    sectorRead(buf, fd, currState.file_entry_idx_sector);
    buf2dir(buf, &d);

    switch (currState.file_sector_type) {
        case 'D':
            //Free dir entry
            d.Idx[ currState.file_entry_idx ].type = 'F';
            d.Idx[ currState.file_entry_idx ].link = 0;
            strncpy(d.Idx[ currState.file_entry_idx ].name, "         \0", 10);
            dir2buf(buf, d);
            sectorWrite(buf, fd, currState.file_entry_idx_sector);

            //now deal with sub-dir
            //DEBUG
            printf("Reaping sector: %d\n", sector2free);

            sectorRead(buf, fd, sector2free);
            buf2dir(buf, &d);
            reapDir(&d);
            break;
        case 'U':
            //Free dir entry
            d.Idx[ currState.file_entry_idx ].type = 'F';
            d.Idx[ currState.file_entry_idx ].link = 0;
            strncpy(d.Idx[ currState.file_entry_idx ].name, "         \0", 10);
            dir2buf(buf, d);
            sectorWrite(buf, fd, currState.file_entry_idx_sector);

            //now deal with file
            struct File f;
            sectorRead(buf, fd, sector2free);
            buf2file(buf, &f);
            reapFile(&f);
            break;
    }
    containerClose(fd);
}

void read_file(int sector, short size) {
    // cat...
    int fd;                     // file descriptors
    char buf[BUF_SIZE] = {0};
    struct File f;

    fd = containerOpen(opt.filename, CONTAINER_READ);
    sectorRead(buf, fd, sector);
    buf2file(buf, &f);

    while (f.frwd != 0) {
        
        for (int i=0; i<504; i++) {
            printf("%c", f.data[i]);
        }
        memset(buf, 0, BUF_SIZE);
        sectorRead(buf, fd, f.frwd);
        buf2file(buf, &f);
    }

    for (int i=0; i<size; i++) {
        printf("%c", f.data[i]);
    }
    containerClose(fd);
}

void write_2_file(int sector, int offset) { // WRITE n data (write n bytes of data)
    int fd_in, fd_out, bytes_wrote, bc_read = 0; // file descriptors, byte counter
    char wrote504 = '0';        // '1' indicates full sector was written so need to extendFile()
    char sectBuf[512] = {0};
    char dataBuf[504] = {0};
    struct File f;

    fd_in = open(opt.src, CONTAINER_READ);
    if (fd_in < 0) {
        dprintf(2, "Could not open file %s for reading; %s\n", opt.src, strerror(errno));
        die(&fd_in, 255);
    }

    fd_out = containerOpen(opt.filename, CONTAINER_READWRITE);
    sectorRead(sectBuf, fd_out, sector);
    buf2file(sectBuf, &f);

    if (offset > 0) {   //we are appending, need to fill out last sector
        memcpy( &dataBuf, &f.data, 504); // need to prime w/ existing data
        print_hex_memory(dataBuf, 504);

        bc_read = read(fd_in, dataBuf+(offset-2), (504-offset+2) );

        print_hex_memory(dataBuf, 504);
        //memcpy( &f.data, &dataBuf, bc_read);
        memcpy(&f.data, &dataBuf, 504);
        file2buf(sectBuf, f);
        //DEBUG
        printf("write_2_file, sector: %d offset: %d\n", sector, offset);

        sectorWrite(sectBuf, fd_out, sector);
        memset(dataBuf, 0, 504);

        wrote504 = ( bc_read == (504-offset+2) ) ? '1' : '0'; // if less, no more cp
        //DEBUG
        printf("1.bytes_read: %d, wrote504: %c\n", bc_read, wrote504);
        bytes_wrote = bc_read;
    }
    else { //overwrite
        bc_read = read(fd_in, &dataBuf, 504);
        memcpy( &f.data, &dataBuf, bc_read);
        file2buf(sectBuf, f);
        sectorWrite(sectBuf, fd_out, sector);
        memset(dataBuf, 0, 504);

        if (bc_read == 504) { // indicate we wrote a full sector
            wrote504 = '1';
        }
        else { // last read, need to update .size of dir entry after save
            wrote504 = '0';
        }
        bytes_wrote = bc_read;
    }
    //DEBUG
    printf("2.bytes_read: %d, wrote504: %c\n", bc_read, wrote504);

    if (wrote504 == '1') {  // if copying more is needed...

        while ( (bc_read = read(fd_in, dataBuf, 504)) > 0 ) {

            if (wrote504 == '1') {
                // need to extend sector and load it
                //DEBUG
                printf("Orig sector: %d\n", sector);
                sector = extendFile(sector);
                //DEBUG
                printf("New sector: %d\n", sector);
                sectorRead( sectBuf, fd_out, sector);
                buf2file(sectBuf, &f);
            }
            // Save the data
            memcpy( &f.data, &dataBuf, bc_read);
            file2buf(sectBuf, f);
            //DEBUG
            printf("Sector written: %d\n", sector);
            sectorWrite(sectBuf, fd_out, sector);
            memset(dataBuf, 0, 504);

            if (bc_read == 504) { // indicate we wrote a full sector
                wrote504 = '1';
            }
            else { // last read, need to update .size of dir entry after save
                wrote504 = '0';
            }
            // loop 2 cp moar
            bytes_wrote = bc_read;
        }
    }
    /*
    else {  // Save the data
        memcpy( &f.data, &dataBuf, bytes_wrote);
        file2buf(sectBuf, f);
        //DEBUG
        printf("Sector written: %d\n", sector);
        sectorWrite(sectBuf, fd_out, sector);
    }
    */
    //update dir entry
    printf("bytes_wrote: %d, wrote504: %c\n", bytes_wrote, wrote504);
    struct Dir d;
    sector = getDirOfLastPathElementSector();
    sectorRead(sectBuf, fd_out, sector);
    buf2dir(sectBuf, &d);

    for (int i=0; i<31; i++) {

        if ( strncmp( d.Idx[i].name, userPath.elementArr[ userPath.elementCount - 1 ], 9) == 0 ) {

            //DEBUG
            printf("DirUpdate bytes_wrote: %d, wrote504: %c\n", bc_read, wrote504);

            switch (bytes_wrote) {
                case 0: // edge case of input file exactly 504 bytes
                    d.Idx[i].size = 504;
                    break;
                default:
                    d.Idx[i].size = bytes_wrote;
                    break;
            }
            //DEBUG
            printf("writing file size: %d\n", d.Idx[i].size);

            break;  // finished looking through dir idx
        }
    }
    dir2buf(sectBuf, d);
    sectorWrite(sectBuf, fd_out, sector);
    containerClose(fd_out);
}

void seek_file(int base, double offset) {   // SEEK base offset 
    // base=-1: start of file; base=0: curr loc in file; base=1: EOF
}

void dir2buf(char* b, struct Dir d) {
    char **p = &b;

    memcpy(*p+0, &d.back, 4);
    memcpy(*p+4, &d.frwd, 4);
    memcpy(*p+8, &d.free, 4);
    memcpy(*p+12, &d.filler, 4);

    for (int i=0; i<31; i++) {
        //    base+( hdrOffset+(arrOffset)+fieldOffset ), StructSrc, len2cp)
        memcpy(*p+( 16+(i*16) ), &d.Idx[i].link, 4);
        memcpy(*p+( 16+(i*16)+4 ), &d.Idx[i].name, 9);    // Not copying the NUL term.
        memcpy(*p+( 16+(i*16)+4+9 ), &d.Idx[i].type, 1);
        memcpy(*p+( 16+(i*16)+4+9+1 ), &d.Idx[i].size, 2);
    }
}
void file2buf(char* b, struct File f) {
    char **p = &b;

    /* struct File
            char data[504];
            int back;
            int frwd;
     */
    memcpy(*p+0, &f.back, 4);
    memcpy(*p+4, &f.frwd, 4);
    memcpy(*p+8, &f.data, 504);
}
void buf2dir(char* b, struct Dir* d) {
    char **p = &b;

    //DEBUG
    //printf("buf2dir() running\n");
    //print_hex_memory(*p+0, 16);
    //print_hex_memory(*p+32, 16);

    memcpy(&d->back, *p+0, 4);
    memcpy(&d->frwd, *p+4, 4);
    memcpy(&d->free, *p+8, 4);
    memcpy(&d->filler, *p+12, 4);

    /* struct FileIDX Idx[31];
            int link;
            char name[9];
            char type;
            short int size;
    */
    for (int i=0; i<31; i++) {
        memcpy(&d->Idx[i].link, *p+( 16+(i*16) ), 4);
        memcpy(&d->Idx[i].name, *p+( 16+(i*16)+4 ), 9); //init gave terminating \0
        memcpy(&d->Idx[i].type, *p+( 16+(i*16)+4+9 ), 1);
        memcpy(&d->Idx[i].size, *p+( 16+(i*16)+4+9+1 ), 2);
    }

    //DEBUG
    /*
    for (int i=0; i<5; i++) {
        printf("d->Idx[%d].name: %s\t.type: %c\n", i, (char *)d->Idx[i].name, (char)d->Idx[i].type);
    }
    */
}
void buf2file(char* b, struct File* f) {
    char **p = &b;

    memcpy(&f->back, *p+0, 4);
    memcpy(&f->frwd, *p+4, 4);
    memcpy(&f->data, *p+8, 504);
}

int containerOpen(char* p, int m) {
    int fd = 0;
    /* m =
     * CONTAINER_CREAT create file, truncate, write-only, will overwrite existing file with same name
     * CONTAINER_INIT create file, truncate, write-only, will error if file exists
     * CONTAINER_READ read-only
     * CONTAINER_WRITE write-only
     * CONTAINER_READWRITE read/write
     */
    switch (m) {
        case CONTAINER_CREAT:
        case CONTAINER_INIT:
            fd = open(opt.filename, m, CONTAINER_PERMS);
        default:
            fd = open(opt.filename, m);
            break;
    }

    if (fd < 0) {
        dprintf(2, "Could not open container file %s with mode %d; %s\n", opt.filename, m, strerror(errno));
        die(&fd, 1);
    }
    return fd;
}

void containerClose(int fd) {
    //TODO: error handling
    close(fd);
}

void sectorRead(char* buf, int fd, int sector) {
    /*
     * zero's buffer in case of partial read.
     * takes given sector number (zero-based) and calculates byte offset
     * reads sector into given buffer
     * updates global currState.curr_sector
     */
    memset(buf, 0, BUF_SIZE);

    //DEBUG
    //printf("sectorRead sector num: %d\n", sector);

    int bytes_read = pread(fd, buf, BUF_SIZE, (sector*BUF_SIZE));
    if (bytes_read != BUF_SIZE) { /* read error happened... */
        dprintf(2, "Error occured reading sector at offset %d; %s\n", sector*BUF_SIZE, strerror(errno));
        die(&fd, 3);
    }
    currState.curr_sector = sector;
}

void sectorWrite(char* buf, int fd, int offset) {
    int bytes_written = 0;

    bytes_written = pwrite(fd, buf, BUF_SIZE, (offset*BUF_SIZE));
    if (bytes_written != BUF_SIZE) { /* write error happened... */
        dprintf(2, "Error occured writing sector at offset %d; %s\n", offset, strerror(errno));
        die(&fd, 3);
    }
}

void usage() {
    printf("jvol - manipulate an elementry filesystem in a file\n\n");
    printf("Usage: \n");
    printf("    jvol [-h] [-c cmd] -f filename [-p file]\n\n");
    printf("    -c command: {init, mkdir, touch, gulp, append, cat, ls, rm, cp, mv}.\n\n");
    printf("    -h print this help message; no operations are performed.\n\n");
    printf("    -i Input file to read data from.\n\n");
    printf("    -f operate on this container file.\n\n");
    printf("    -p operate on this file (path) with cmd given for -c arg.\n\n");
    printf("    -s Source file from environment.\n\n");
    printf("Behavior: \n");
    printf("    If -f option is given with no other options, container file will be created and initialized.\n");
    printf("        However, if the given filename already exists, it will NOT be overwritten and program\n");
    printf("        will exit with error. Init command will allow overwriting of existing container.\n\n");
    printf("    There are semantics with the cp, and mv commands; the presence of -s or -o options indicate\n");
    printf("        interaction with underlying filesystem and direction of data flow. The absence of -s or -o\n");
    printf("        demands two paths be supplied with -p option seperated by a comma (i.e. -p src/path,dest/path).\n\n");
}

int parseCmd(char* c) {
    //DEBUG
    //printf("Command: %s, length: %lu\n", c, strlen(c));
    // {init, mkdir, touch, gulp, append, cat, ls, rm, cp, mv}
    if ( strncmp("init", c, strlen(c)) == 0 ) {
        opt.init = 1;
        return 0;
    }
    //else if ( strncmp("mkdir", c, strlen(c)) == 0 ) {
    else if ( strcmp("mkdir", c) == 0 ) {
        return 1;
    }
    else if ( strncmp("touch", c, strlen(c)) == 0 ) {
        return 2;
    }
    else if ( strncmp("gulp", c, strlen(c)) == 0  ) {
        return 3;
    }
    else if ( strncmp("append", c, strlen(c)) == 0  ) {
        return 4;
    }
    else if ( strncmp("cat", c, strlen(c)) == 0 ) {
        return 5;
    }
    else if ( strncmp("ls", c, strlen(c)) == 0 ) {
        return 6;
    }
    else if ( strncmp("rm", c, strlen(c)) == 0 ) {
        return 7;
    }
    else if ( strncmp("cp", c, strlen(c)) == 0 ) {
        return 8;
    }
    else if ( strncmp("mv", c, strlen(c)) == 0 ) {
        return 9;
    }
    else {
        return 0;
    }
}

void handleArgs(int ac, char** av) {
    int c;
    int p_num = 1;  // Path number
    char* p_token;  // For string splitting


    while ( (c = getopt(ac, av, "h?c:f:i:p:s:") ) != -1) {
        switch(c) {
            case 'f':
                opt.filename = optarg;
                break;
            case 'c':
                opt.cmd = parseCmd(optarg);
                break;
            case '?':
            case 'h':
                usage();
                exit(0);
                break;
            case 'i':
                opt.src = optarg;
                break;
            case 'p':
                p_token = strtok(optarg, ",");

                while (p_token != NULL) {

                    switch (p_num) {
                        case 1: // Src path
                            opt.path = p_token;
                            break;
                        case 2: // Dst path
                            opt.dst_path = p_token;
                            break;
                    }
                    p_token = strtok(NULL, ",");
                    p_num++;
                }
                break;
            default:
                usage();
                exit(255);
        }
    }
}

void parsePath(struct PathElements* pe, char* path) {
    char* token = strtok(path, "/");

    while (token != NULL) {
        append_pathElement(pe, token);
        token = strtok(NULL, "/");
    }
}

void die(int* fd, int exit_code) {
    if (fd) {
        close(*fd);
    }
    exit(exit_code);
}

void print_hex_memory(void *mem, int sz) {
    int i;
    unsigned char *p = (unsigned char *)mem;

    for (i=1;i<=sz;i+=2) {
        printf("%02x%02x ", p[i-1], p[i]);

        if (i % 16 == 0) {
            printf("\n");
        }
    }
    printf("\n");
}

void containerInit() {
    char* filename = opt.filename;
    int fd =0;
    char buf[BUF_SIZE] = {0};
    /* Store file permisions of working dir, container file created with this
     * fstat(".", &file_stat);
     * fd_out = open(argv[2], O_CREAT | O_EXCL | O_TRUNC | O_WRONLY, file_stat.st_mode);
     */

    /* open() flags:
     * CONTAINER_CREAT create file, truncate, write-only, will overwrite existing file with same name
     * CONTAINER_INIT create file, truncate, write-only, will error if file exists
     */
    if (opt.init) {
        fd = containerOpen(opt.filename, CONTAINER_CREAT);
    }
    else {
        fd = containerOpen(opt.filename, CONTAINER_INIT);
    }
    /* Initial root directory entry (zeroth block)
     *
     * .back will always be zero since this is the origin
     * .frwd is zero unless sum of files/sub-dirs > 31. In that case frwd will point to extender directory block
     * .free points to first free block (free blocks are a linked list of dirs)
     * .filler is unused space and will appear as BEEFBEEF in a hex display
     *
     * .Idx is index of directory entries 
     *      .type is 'F' (free)
     *      .name is blank (9 spaces)
     *      .link is zero
     *      .size is zero
     */ 
    //DEBUG
    //char tmpStr[10];
    struct Dir directory = { .back=0x00000000, .frwd=0x00000000, .free=0x00000001, .filler=0xEFBEEFBE };
    struct FileIDX file_idx[31];

    for (int i=0; i<31; i++) {
        //snprintf(tmpStr, 10, "%d", i);
        file_idx[i].link = 0x00000000;
        file_idx[i].type = 'F';
        strncpy(file_idx[i].name, "         \0", 10);
        file_idx[i].size = 0x0000;
    }

    memcpy( &directory.Idx, file_idx, sizeof(directory.Idx) );

    memset(buf, 0, BUF_SIZE);   // Ensure clear buffer
    dir2buf(buf, directory);    // Marshall data to buffer
    sectorWrite( buf, fd, 0 );  // Write out the data to container
    /* Initial blocks (remaining blocks)
     *
     * Blocks are same as root (so we'll modify) except:
     * .free is unused (and set to display DEADDEAD in hex display)
     * .fwrd points to next block (linked-list of free blocks)
     */
    directory.free = 0xADDEADDE;
    directory.frwd = 0x00000001;
    //DEBUG
    //printf("CONT/BUF Size: %d\n", CONTAINER_SIZE/BUF_SIZE);

    for (int i=1; i<CONTAINER_SIZE/BUF_SIZE; i++) {

        if (i < CONTAINER_SIZE/BUF_SIZE-1) {
            directory.frwd++;
        }
        else {
            directory.frwd = 0x00000000;
        }
        memset(buf, 0, BUF_SIZE);
        dir2buf(buf, directory);
        sectorWrite( buf, fd, i );
    }
    containerClose(fd);
}

int main(int argc, char** argv) {
    char* srcPath, dstPath; // in case user specifies src and dst within the container
    handleArgs(argc, argv);

    switch (opt.cmd) {
        case 0: //"init":
            containerInit();
            break;
        case 1: //"mkdir":
            srcPath = malloc(strlen(opt.path));
            strncpy(srcPath, opt.path, strlen(opt.path)+1);
            parsePath(&userPath, srcPath);
            free(srcPath);
            create_file('D');
            break;
        case 2: //"touch":
            srcPath = malloc(strlen(opt.path));
            strncpy(srcPath, opt.path, strlen(opt.path)+1);
            parsePath(&userPath, srcPath);
            free(srcPath);

            create_file('U');
            break;
        case 3: //"gulp":
            srcPath = malloc(strlen(opt.path));
            strncpy(srcPath, opt.path, strlen(opt.path)+1);
            parsePath(&userPath, srcPath);
            free(srcPath);
            open_file( 'I', userPath.elementArr[ userPath.elementCount - 1 ] );
            break;
        case 4: //"append":
            srcPath = malloc(strlen(opt.path));
            strncpy(srcPath, opt.path, strlen(opt.path)+1);
            parsePath(&userPath, srcPath);
            free(srcPath);
            open_file( 'A', userPath.elementArr[ userPath.elementCount - 1 ] );
            break;
        case 5: //"cat":
            srcPath = malloc(strlen(opt.path));
            strncpy(srcPath, opt.path, strlen(opt.path)+1);
            parsePath(&userPath, srcPath);
            free(srcPath);
            open_file( 'O', userPath.elementArr[ userPath.elementCount - 1 ] );
            break;
        case 6: //"ls":
            parsePath(&userPath, opt.path);
            ls_file();
            break;
        case 7: //"rm":
            parsePath(&userPath, opt.path);
            rm_file();
            break;
        case 8: //"cp":
            parsePath(&userPath, opt.path);

            if (opt.dst_path) {
                parsePath(&userDstPath, opt.dst_path);
            }
            break;
        case 9: //"mv":
            parsePath(&userPath, opt.path);

            if (opt.dst_path) {
                parsePath(&userDstPath, opt.dst_path);
            }
            break;
        default:
            printf("Bug, all cases should be handled explicity in main()\n");
            exit(255);
    }
    //DEBUG
    int fr = get2FreeSectors();
        
    exit(0);
}
