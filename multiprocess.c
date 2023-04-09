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

#define MAXPATH 260 // use 260 to be safe (given is 250)
#define MAXTASK 64
#define DEBUG 0     // Set to 1 for debugging messages, 0 for none (set to 0 before submission!)

struct proc_data {
    int ID;         // Ranges from 0 to N-1
    char *search_string;
    char *task_queue;
    char *busy_procs;
};

void Queue_Enqueue(char *task_queue, char *abspath) {
    FILE * fptr = fopen(task_queue, "a");
    int fd = fileno(fptr);
    flock(fd, LOCK_EX);
    fprintf(fptr, "%s", abspath);
    flock(fd, LOCK_UN);
    fclose(fptr);
}

void Queue_Dequeue(char *task_queue, char *taskpath) {
    FILE * fptr = fopen(task_queue, "r");
    int fd = fileno(fptr);
    flock(fd, LOCK_EX);
    fread(taskpath, 1, MAXPATH, fptr);
    flock(fd, LOCK_UN);
    fclose(fptr);
}

int isEmpty(char *task_queue) {

    FILE * fptr = fopen(task_queue, "r");


    printf("TEST");
    int origoffset = fseek(fptr, 0, SEEK_CUR);
    printf("%d \n", origoffset);
    fseek(fptr, 0, SEEK_END);
    int filesize = ftell(fptr);
    fseek(fptr, origoffset, SEEK_SET);
    printf("proc test B\n");
    if (filesize == 0) {

        fclose(fptr);
        return 1;
    }
    printf("TESTfdsf");
    fclose(fptr);
    return 0;
}

// Utility function: Checks if path is a regular file or a directory.
// Returns 1 if ISDIR, returns 0 if ISREG
int isDir(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

// Child process here can both act as a consumer and a producer
void routine(struct proc_data *data, int N) {
    printf("=== Process %d is starting ===\n", data->ID);

    int ID = data->ID;
    char *search_string = data->search_string;
    char *task_queue = data->task_queue;
    char *busy_procs = data->busy_procs;

    char taskpath[MAXPATH];
    int busies;

    //fcntl(tqfd, F_SETFL, O_NONBLOCK);

    while (1) {

        FILE * fptr = fopen(busy_procs, "r+");
        int fd = fileno(fptr);
        printf("%d test\n", ID);

        while (1) {
            flock(fd, LOCK_EX);
            fseek(fptr, 0, SEEK_SET);
            fscanf(fptr, "%d", &busies);
            busies--;
            printf("%d!!\n", busies);
            fseek(fptr, 0, SEEK_SET);
            fprintf(fptr, "%d", busies);
            fflush(fptr);
            if (busies == 0) {
                                printf("proc exiting\n");
                exit(0);
            }
            flock(fd, LOCK_UN);
            sleep(1);
            flock(fd, LOCK_EX);
            fseek(fptr, 0, SEEK_SET);
            fscanf(fptr, "%d", &busies);
            busies++;
            fseek(fptr, 0, SEEK_SET);
            fprintf(fptr, "%d", busies);
            fflush(fptr);
            flock(fd, LOCK_UN);
        }
        fclose(fptr);
        printf("%d test\n", ID);
        Queue_Enqueue(task_queue, taskpath);

        flock(1, LOCK_EX);
        printf("[%d] DIR %s\n", ID, taskpath);
        flock(1, LOCK_UN);
        
        // IDEA: Current Working Directory is a per-process construct unlike that of threads.
        // Hence, no need to worry about chdir()-related race condition
        chdir(taskpath);
        DIR *dirp = opendir(".");
        char pathbuff[MAXPATH];   // use with constructing abspath corr to next lower dir or a regular file in curr dir
        char origcwd[MAXPATH];
        getcwd(origcwd, MAXPATH);  // store orig

        // Prepare grepbuff for when 
        // BUFFSIZE anticipates max length of abspath + variable length of search_string
        //      + the 25 aux chars in grepbuff
        int BUFFSIZE = MAXPATH + strlen(data->search_string) + 25; 
        char *grepbuff = (char *) malloc(sizeof(char) * (BUFFSIZE));
        struct dirent *d;
        // readdir here operates on clean dirp which was locked previously
        // This makes the lines below threadsafe
        while ((d = readdir(dirp)) != NULL) {   // loop until end of dir stream is reached
            if (DEBUG) printf("[%d] cwd %s | d->d_name %s\n", ID, getcwd(NULL,0), d->d_name);
            // Ignore self . and parent .. directories
            if (strncmp(d->d_name, ".", MAXPATH) == 0 || strncmp(d->d_name, "..", MAXPATH) == 0) {
                continue;
            }

            // Prepare abspath of next lower dir or regular file
            strncpy(pathbuff, origcwd, MAXPATH);  // revert pathbuff to clean copy of cwd
            strcat(pathbuff, "/");
            strncat(pathbuff, d->d_name, MAXPATH);
            assert(strlen(pathbuff) <= 250);

            // Do work on task.
            // Use abspath pathbuff with isDir, not relpath d->d_name, simply for consistency.
            // Previous motive was to avoid chdir()-related race conditions.
            flock(1, LOCK_EX);printf("[%d] Got here\n", ID);flock(1, LOCK_UN);
            if (isDir(pathbuff)) {
                Queue_Enqueue(task_queue, pathbuff);
                flock(1, LOCK_EX);
                printf("[%d] ENQUEUE %s\n", ID, pathbuff);
                flock(1, LOCK_UN);
            } else {
                // Search using absolute path!
                // Don't use relative path d->d_name as it's vulnerable to change in cwd.
                // NOTE: grep "search_string" "file_name"
                snprintf(grepbuff, BUFFSIZE, "grep \"%s\" \"%s\" > /dev/null", data->search_string, pathbuff);
                if (DEBUG) printf("[%d] grepbuff after snprintf: \"%s\"\n", ID, grepbuff);
                int check = system(grepbuff)/256;   // execute grep (issue with system() ret so divide by 256)
                flock(1, LOCK_EX);
                if (check == 0) {
                    printf("[%d] PRESENT %s\n", ID, pathbuff);
                } else if (check == 1) {
                    printf("[%d] ABSENT %s\n", ID, pathbuff);
                } else {
                    printf("[%d] ERROR %s\n", ID, pathbuff); // should NOT appear
                }
                flock(1, LOCK_UN);
            }
        }
        free(grepbuff); // don't forget to free grepbuff
        closedir(dirp); // don't forget to close directory
    }
}

int main(int argc, char *argv[]) {

    int N = atoi(argv[1]);
    char rootpath[MAXPATH];
    strncpy(rootpath, argv[2], MAXPATH);
    char *search_string = argv[3];

    // Initializations
    struct proc_data pdata;
    pdata.search_string = search_string;

    // Create named pipe for task_queue, essentially an Unbounded Buffer.
    // Processes will perform looping read and write on the pipe.
    // After parent creates all its children, it will finally write rootpath into pipe, beginning the routine.
    char * task_queue = "task_queue";
    unlink(task_queue);

    // Convert rootpath first to absolute path if neeeded
    if (rootpath[0] != '/') {
        char origcwd[MAXPATH];
        getcwd(origcwd, MAXPATH);       // NOTE: getcwd() copies a null-terminated string to buff; store orig
        chdir(rootpath);                // switch to rel rootpath
        getcwd(rootpath, MAXPATH);      // get abspath equivalent of rel rootpath
        chdir(origcwd);                 // restore orig
    }
    Queue_Enqueue(task_queue, rootpath);

    // Create shared file for busy_procs
    char *busy_procs = "busy_procs";
    unlink(busy_procs);
    FILE *bpfp = fopen(busy_procs, "w");
    fclose(bpfp);
    pdata.busy_procs = busy_procs;

    // Disable stdout buffering (supplement with flocks inside routine)
    setvbuf(stdout, NULL, _IONBF, 0);

    // Launch Child Processes
    // If N = 1, make the pipe nonblocking inside routine for uninterrupted execution
    for (int i = 0; i < N; i++) {
        if (fork() == 0) {
            pdata.ID = i;
            routine(&pdata, N);
        }
    }

    // Wait for Child Processes
    for (int i = 0; i < N; i++) {
        wait(NULL);
    }
    return 0;
}