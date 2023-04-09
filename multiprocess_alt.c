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
#define CUROFFSET printf("%d\n", (int) lseek(fd, 0, SEEK_CUR));

struct proc_data {
    int ID;         // Ranges from 0 to N-1
    int tqfd;       // quick access to task_queue fd 
    int bpfd;       // quick access to busyThreads fd
    char *search_string;
};

// Equivalent to Bounded Buffer put()
// Pass absolute path here, func is will be responsible for constructing task
void Queue_Enqueue(int fd, char *abspath) {
    flock(fd, LOCK_EX);
    lseek(fd, 0, SEEK_END);         // write at end of file
    write(fd, abspath, MAXPATH);
    write(fd, "\n", 1);             // insert newline at end
    flock(fd, LOCK_UN);
}

// Equivalent to Bounded Buffer get()
// Pass char * taskpath here, will be filled up by func with popped head
void Queue_Dequeue(int fd, char *taskpath) {
    flock(fd, LOCK_EX);
    lseek(fd, -MAXPATH, SEEK_END);     // set offset to start of last entry in file, read in LIFO order
    read(fd, taskpath, MAXPATH);
    struct stat filestat;
    fstat(fd, &filestat);
    int filesize = filestat.st_size;
    if (DEBUG) printf("filesize %d\n", filesize);
    ftruncate(fd, filesize-MAXPATH-1);
    flock(fd, LOCK_UN);
}

// Utility function: prints contents of Task Queue
// Needs to be called with both of the same head and tail locks.
void Queue_Log(int fd) {
    printf("=== QUEUE LOG START ===\n");
    flock(fd, LOCK_EX);
    int origoffset = (int) lseek(fd, 0, SEEK_CUR);  // store for later restoration
    lseek(fd, 0, SEEK_SET);     // set offset to start of file
    char buff[MAXPATH];
    while (read(fd, buff, MAXPATH) != 0) {
        lseek(fd, 1, SEEK_CUR);     // add 1 to offset to accomodate the newline at the end of 260 chars
        if (buff[0] == '/') {
            printf("%s\n", buff);   // only print abspaths (avoids printing newlines)
        }
    }
    lseek(fd, origoffset, SEEK_SET);        // restore file offset before Queue_Log
    flock(fd, LOCK_UN);
    printf("=== QUEUE LOG END =====\n");
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
    struct stat filestat;
    char bpbuff;
    while (1) {
        
        flock(tqfd, LOCK_EX);
        fstat(tqfd, &filestat);
        int filesize = filestat.st_size;
        while (filesize == 0) {
            flock(bpfd, LOCK_EX);
            lseek(bpfd, 0, SEEK_SET);
            read(bpfd, &bpbuff, 1);
            write(bpfd, &(--bpbuff), 1);
            if (bpbuff == '0') {
                printf("=== Process %d is exiting ===\n", ID);
                exit(0);
            }
            flock(bpfd, LOCK_UN);
            yield();
            flock(bpfd, LOCK_EX);
            lseek(bpfd, 0, SEEK_SET);
            read(bpfd, &bpbuff, 1);
            write(bpfd, &(++bpbuff), 1);
            flock(bpfd, LOCK_UN);
        }
        flock(tqfd, LOCK_UN);

        // Task can be successfully dequeued
        char taskpath[MAXPATH];
        Queue_Dequeue(tqfd, taskpath);   // act as a consumer
        printf("[%d] DIR %s\n", ID, taskpath);
        
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
                Queue_Enqueue(pathbuff);    // act as a producer
                printf("[%d] ENQUEUE %s\n", ID, pathbuff);
            } else {
                // Search using absolute path!
                // Don't use relative path d->d_name as it's vulnerable to change in cwd.
                // NOTE: grep "search_string" "file_name"
                snprintf(grepbuff, BUFFSIZE, "grep \"%s\" \"%s\" > /dev/null", data->search_string, pathbuff);
                if (DEBUG) printf("[%d] grepbuff after snprintf: \"%s\"\n", ID, grepbuff);
                int check = system(grepbuff)/256;   // execute grep (issue with system() ret so divide by 256)
                if (check == 0) {
                    printf("[%d] PRESENT %s\n", ID, pathbuff);
                } else if (check == 1) {
                    printf("[%d] ABSENT %s\n", ID, pathbuff);
                } else {
                    printf("[%d] ERROR %s\n", ID, pathbuff); // should NOT appear
                }
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
    struct proc_data pdata;
    pdata.search_string = search_string;
    // Create shared file for task_queue, essentially an Unbounded Buffer.
    // Find another way to tell if task_queue.txt is empty.
    int tqfd = open("task_queue.txt", O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);   

    // Store first task (rootpath) into task_queue
    // IDEA: Only work with abs path, convert supplied rel path to abs path
    // If first char of rootpath is '/', it is an abs path, else it is rel path (must convert)
    // NOTE: The child inherits copies of the parent's set of open file descriptors.
    // But afterwards, the set of open fds of the child (not the fds themselves) can be manipulated
    // independently from its parent and siblings. Same idea with the parent.

    if (rootpath[0] == '/') {
        Queue_Enqueue(tqfd, rootpath);
    } else {
        char origcwd[MAXPATH];
        getcwd(origcwd, MAXPATH);   // NOTE: getcwd() copies a null-terminated string to buff; store orig
        chdir(rootpath);             // switch to rel rootpath
        getcwd(rootpath, MAXPATH);  // get abspath equivalent of rel rootpath
        chdir(origcwd);              // restore orig
        Queue_Enqueue(tqfd, rootpath);
    }
    Queue_Log(tqfd);
    // Do not close tqfd yet. Store in pdata.
    pdata.tqfd = tqfd;
    
    // Create named pipe for busyProcs
    int busyProcs;
    pipe(busyProcs);    // READ END fds[busyProcs[0]], WRITE END fds[busyProcs[1]]
    write(busyProcs[1], argv[1], 1);

    // Disable stdout buffering (supplement with flock synch inside routine)
    setvbuf(stdout, NULL, _IONBF, 0);

    // Launch Child Processes
    for (int i = 0; i < N; i++) {
        if (fork() == 0) {
            pdata.ID = i;
            routine(&pdata);
        }
    }

    // Wait for Child Processes
    close(tqfd);
    close(busy)
    for (int i = 0; i < N; i++) {
        wait(NULL);
    }

    // Housekeeping: Close all open file directories.
    close(tqfd);
    close(bpfd);

    return 0;
}