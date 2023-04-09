#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <fcntl.h>

int main() {
    FILE *fptr = fopen("myfile", "w");
    char buff[255];
    fprintf(fptr, "testing");
    fclose(fptr);
    fptr = fopen("myfile", "r");
    fscanf(fptr, "%s", buff);
    printf("%s\n", buff);
    fclose(fptr);
}