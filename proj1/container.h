struct FileIDX {
    int link;
    char name[10];  // 9 + NULL
    char type;
    short int size;
};

struct Dir {
    int back;
    int frwd;
    int free;
    int filler;
    struct FileIDX Idx[31];
};

struct File {
    char data[504];
    int back;
    int frwd;
};

struct State {
    int curr_sector;            // Sector number in ram
    int free;                   // number of next free sector
    int next_free;              // free sector after current one
    int last_free;              // last free sector in container
    int arr_idx_sector;         // Sector number having free directory entry index
    int arr_idx;                // number of free directory entry index
    int file_first_sector;      // Entry-sector number of file
    int file_last_sector_size;  // How many data bytes in a file's last sector
    int file_entry_idx;         // Found file at this array index
    int file_entry_idx_sector;  // Found file has dir entry in this sector
    int numFree;                // How many free sectors in container
    char file_sector_type;      // curr_sector is: 'U', user file, or 'D' == Dir
};
