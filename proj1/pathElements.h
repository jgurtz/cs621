/*
 * Dynamic array of c-strings for holding elements of user-given path
 * 
 * Note this just allocates on every addition, which may be wasteful but
 * is ok here because the amount of path elements will usually be small (<10)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PathElements {
    char **elementArr;
    int elementCount;   /* init to zero */
};
 
void append_pathElement(struct PathElements*, char*);
void free_pathElements(struct PathElements*);

void append_pathElement(struct PathElements *pe, char *el) {

    if (pe->elementCount == 0) {
        pe->elementArr = (char **)malloc( sizeof(char *) );
    }
    pe->elementArr[ pe->elementCount ] = (char *)malloc( strlen(el) + 1 );

    pe->elementArr[ pe->elementCount ] = strndup(el, (size_t)strlen(el));
    pe->elementCount++;
}

// Probably will not use this, maybe if we make a long-running shell thing
void free_pathElements(struct PathElements *pe) {
    /*
     * This function frees only the individual data elements. Fully free
     * the data structure with -- free(elementArray); -- in the calling
     *  routine. Wrapper function?
     */

    for(int i=pe->elementCount; i>0; i--) {
        free(pe->elementArr[i]);
        pe->elementCount--;
    }
}
