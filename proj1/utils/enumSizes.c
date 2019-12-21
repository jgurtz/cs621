#include <stdio.h>

int main(int argc, char** argv) {
	printf("    short int is %2d bytes \n", (int)sizeof(short int));
	printf("          int is %2d bytes \n", (int)sizeof(int));
	printf("        int * is %2d bytes \n", (int)sizeof(int *));
	printf("     long int is %2d bytes \n", (int)sizeof(long int));
	printf("   long int * is %2d bytes \n", (int)sizeof(long int *));
	printf("   signed int is %2d bytes \n", (int)sizeof(signed int));
	printf(" unsigned int is %2d bytes \n", (int)sizeof(unsigned int));
	printf("\n");
	printf("        float is %2d bytes \n", (int)sizeof(float));
	printf("      float * is %2d bytes \n", (int)sizeof(float *));
	printf("       double is %2d bytes \n", (int)sizeof(double));
	printf("     double * is %2d bytes \n", (int)sizeof(double *));
	printf("  long double is %2d bytes \n", (int)sizeof(long double));
	printf("\n");
	printf("  signed char is %2d bytes \n", (int)sizeof(signed char));
	printf("         char is %2d bytes \n", (int)sizeof(char));
	printf("       char * is %2d bytes \n", (int)sizeof(char *));
	printf("unsigned char is %2d bytes \n", (int)sizeof(unsigned char));

	return 0;
}
