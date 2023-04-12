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
#define DEBUG 1     // Set to 1 for debugging messages, 0 for none (set to 0 before submission!)

struct proc_data {
    int ID;         // Ranges from 0 to N-1
    int tqfd;       // quick access to task_queue fd 
    int bpfd;       // quick access to busyThreads fd
    char *search_string;
};

// Equivalent to Bounded Buffer put()
// Pass absolute path here, func is will be responsible for constructing task
void Queue_Enqueue(int fd, char *abspath) {
    write(fd, abspath, MAXPATH);    // enqueue abspath
    fsync(fd);
}

// Equivalent to Bounded Buffer get()
// Must only be called when filesize > 0
int Queue_Dequeue(int fd, char *taskpath) {
    read(fd, taskpath, MAXPATH);
}

// Utility function: prints contents of Task Queue
void Queue_Log(int fd) {
    printf("=== QUEUE LOG START ===\n");
    int origoffset = (int) lseek(fd, 0, SEEK_CUR);  // store for later restoration
    lseek(fd, 2, SEEK_SET);     // set offset to first entry of file [0,1] = count\n, [2] = /
    char buff[MAXPATH];
    while (read(fd, buff, MAXPATH) != 0) {
        lseek(fd, 1, SEEK_CUR);     // add 1 to offset to accomodate the newline at the end of 260 chars
        if (buff[0] == '/') {
            printf("%s\n", buff);   // only print abspaths (avoids printing newlines)
        }
    }
    lseek(fd, origoffset, SEEK_SET);        // restore file offset before Queue_Log
    printf("=== QUEUE LOG END =====\n");
}

// if process returns from this func, exitTest is passed
void exitTest(int bpfd, int ID) {
    char bpbuff[1];
    flock(bpfd, LOCK_EX);
    // read, decrement, then forcefully write back
    lseek(bpfd, 0, SEEK_SET);
    read(bpfd, bpbuff, 1);
    bpbuff[0]--;
    lseek(bpfd, 0, SEEK_SET);
    write(bpfd, bpbuff, 1);
    fsync(bpfd);
    if (bpbuff[0] == '0') {
        printf("=== Process %d is exiting ===\n", ID);
        exit(0);
    }
    sleep(1);
    // read, increment, then forcefully write back
    lseek(bpfd, 0, SEEK_SET);
    read(bpfd, bpbuff, 1);
    bpbuff[0]++;
    lseek(bpfd, 0, SEEK_SET);
    write(bpfd, bpbuff, 1);
    fsync(bpfd);
    flock(bpfd, LOCK_UN);
}

// Utility function: Checks if path is a regular file or a directory.
// Returns 1 if ISDIR, returns 0 if ISREG
int isDir(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

// Child process here can both act as a consumer and a producer
void routine(struct proc_data *data) {
    printf("=== Process %d is starting ===\n", data->ID);

    int ID = data->ID;
    int tqfd = data->tqfd;
    int bpfd = data->bpfd;
    char *search_string = data->search_string;
    
    while (1) {
        
        flock(tqfd, LOCK_EX);
        if (Queue_Size(tqfd) == 0) {
            flock(tqfd, LOCK_UN);
            exitTest(bpfd, ID);
            sleep(1);   // sleep for a short time to give way for other procs.
            continue;
        }

        // Task can be successfully dequeued
        char taskpath[MAXPATH];
        Queue_Dequeue(tqfd, taskpath);   // act as a consumer
        flock(tqfd, LOCK_UN);

        flock(1, LOCK_EX);
        printf("[%d] DIR %s\n", ID, taskpath);
        flock(1, LOCK_UN);
        
        // IDEA: Current Working Directory is a per-process construct unlike that of threads.
        // Hence, no need to worry about chdir()-related race condition
        chdir(taskpath);
        DIR *dirp = opendir(".");
        // If realpath() is not available
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
            if (isDir(pathbuff)) {
                Queue_Enqueue(tqfd, pathbuff);    // act as a producer
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
    // argv preprocessing
    int N = atoi(argv[1]);
    // strncpy is sane because len(relpaths) < len(abspaths) <= MAXPATH (\0 inclusive)
    char rootpath[MAXPATH]; strncpy(rootpath, argv[2], MAXPATH);
    char *search_string = argv[3];

    if (DEBUG) printf("N=%d | rootpath=\"%s\" | search_string=\"%s\"\n", N, rootpath, search_string);

    // Initializations
    char debugBuff[MAXPATH];
    struct proc_data pdata;
    pdata.search_string = search_string;
    // Create named pipe for task_queue, essentially an Unbounded Buffer.
    // Processes will perform looping read and write on the pipe.
    // After parent creates all its children, it will finally write rootpath into pipe, beginning the routine.
    int tqfd;
    mkfifo("task_queue", 0666);
    tqfd = open("task_queue", O_WRONLY|O_TRUNC);      // open now to avoid truncation by child procs
    pdata.tqfd = tqfd;
    
    // Create shared file for busyProcs (since N=1:8, we simply use a 1 char file to keep track of busyProcs)
    int bpfd = open("busyProcs", O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);  
    write(bpfd, argv[1], 1);    // insert N to bpfd
    pdata.bpfd = bpfd;

    // Disable stdout buffering (supplement with flocks inside routine)
    setvbuf(stdout, NULL, _IONBF, 0);

    // Launch Child Processes
    for (int i = 0; i < N; i++) {
        if (fork() == 0) {
            pdata.ID = i;
            routine(&pdata);
        }
    }

    // Convert rootpath first to absolute path if neeeded
    if (rootpath[0] != '/') {
        char origcwd[MAXPATH];
        getcwd(origcwd, MAXPATH);       // NOTE: getcwd() copies a null-terminated string to buff; store orig
        chdir(rootpath);                // switch to rel rootpath
        getcwd(rootpath, MAXPATH);      // get abspath equivalent of rel rootpath
        chdir(origcwd);                 // restore orig
    }

    // Parent now enqueues rootpath to pipe, to be read by waiting child processes
    // Parent will repeatedly try to write until it successfully does
    while (write(tqfd, rootpath, MAXPATH) == -1) {}
    close(tqfd);

    // Wait for Child Processes
    for (int i = 0; i < N; i++) {
        wait(NULL);
    }
    return 0;
}