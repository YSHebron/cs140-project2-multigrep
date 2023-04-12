#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#define MAXPATH 250 // use 260 to be safe
#define DEBUG 0     // Set to 1 for debugging messages, 0 for none (set to 0 before submission!)

pthread_mutex_t lock1, lock2;
pthread_cond_t fill;    // status variable count linked to this
int count = 0;
int busyThreads = 0;

struct thread_data {
    int tid;        // Ranges from 0 to N-1
    char *search_string;
};

// Task Queue Node
struct task {
    char abspath[MAXPATH];     // abspath corr to curr DIR + child rel path when task was enqueued
    struct task *next;
};

// Global Task Queue (maintain dynamic approach, because if static, memory will be an issue for enqueue)
struct queue{
    struct task *head;
    struct task *tail;
    pthread_mutex_t headLock;
    pthread_mutex_t tailLock;
} task_queue;

// Add dummy node to enable separation between head and tail operations
// Note that head will always be pointing to the dummy node, as
// more apparent with Queue_Dequeue
void Queue_Init(struct queue *q) {
    struct task *dummy = malloc(sizeof(struct task));
    dummy->next = NULL;
    q->head = q->tail = dummy;
    pthread_mutex_init(&q->headLock, NULL);
    pthread_mutex_init(&q->tailLock, NULL);
}

// Equivalent to unbounded buffer put()
// Pass absolute path here, func will be responsible for constructing task
void Queue_Enqueue(struct queue *q, char *abspath) {
    struct task *new = malloc(sizeof(struct task));
    // insert abs path to new node
    strncpy(new->abspath, abspath, MAXPATH);
    new->next = NULL;   // ensure tail->next after this routine is always NULL

    // link to tail of queue
    pthread_mutex_lock(&q->tailLock);
    q->tail->next = new;
    q->tail = new;
    pthread_mutex_unlock(&q->tailLock);
}

// Equivalent to (un)bounded buffer get()
// Pass taskpath here, will be filled up by func with popped head
int Queue_Dequeue(struct queue *q, char *taskpath) {
    pthread_mutex_lock(&q->headLock);
    struct task *tmp = q->head;
    struct task *newHead = tmp->next;

    if (newHead == NULL) {
        pthread_mutex_unlock(&q->headLock);
        return -1;  // queue was empty
    }

    strncpy(taskpath, newHead->abspath, MAXPATH);
    q->head = newHead;  // newHead becomes new dummy, rem conts effectively invalidated
    pthread_mutex_unlock(&q->headLock);

    free(tmp);  // safe to be unlocked, bcos used tmp mem address won't be malloc'ed again until it is freed
                // and there are no more pointers to tmp
    return 0;   // successful dequeue
}

// No need for Queue_Is_Full because we are using dynamic memory.
// Queue_Is_Empty may still be useful.
int Queue_Is_Empty(struct queue *q) {
    // Queue is empty when head and tail both points to dummy node
    if (q->head == q->tail) {
        return 1;
    } else {
        return 0;
    }
}

// Utility function: prints contents of Task Queue
// Needs to be called with both of the same head and tail locks. 
void Queue_Log(struct queue *q) {
    pthread_mutex_lock(&q->headLock);
    pthread_mutex_lock(&q->tailLock);
    printf("=== QUEUE LOG BEGIN ===\n");
    struct task *runner = q->head->next;
    while (runner != NULL) {
        printf("%s\n", runner->abspath);
        runner = runner->next;
    }
    printf("=== QUEUE LOG END =====\n");
    pthread_mutex_unlock(&q->tailLock);
    pthread_mutex_unlock(&q->headLock);
}

// Utility function: Checks if path is a regular file or a directory.
// Returns 1 if ISDIR, returns 0 if ISREG
int isDir(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

// Thread here can both act as a consumer and a producer
void routine(struct thread_data *data) {
    if (DEBUG) printf("=== Thread %d Starting ===\n", data->tid);

    // Note: pthread_cond_wait releases lock before sleeping, then acquires lock shortly before returning.
    // Keep working until "signal 2" is given
    while (1) {
        
        pthread_mutex_lock(&lock1);
        if (DEBUG) printf("[%d] busyThreads: %d | count: %d\n", data->tid, busyThreads, count);
        while (count == 0) {
            busyThreads--;
            if (busyThreads == 0) {       // wait for "signal 2"
                if (DEBUG) printf("=== Thread %d is exiting ===\n", data->tid);
                pthread_cond_signal(&fill);
                pthread_mutex_unlock(&lock1);    // figure out how to only use one unlock
                return;
            }
            pthread_cond_wait(&fill, &lock1);        // wait for signal 1
            busyThreads++;
        }

        // Task can be successfully dequeued (preempt count--)
        count--;
        pthread_mutex_unlock(&lock1);

        char taskpath[MAXPATH];
        Queue_Dequeue(&task_queue, taskpath);   // act as a consumer
        // No need to wake up thread sleeping on fill, because thread atm is a consumer (dequeues).
        // Do wake-up on fill once thread is a consumer (enqueues).

        printf("[%d] DIR %s\n", data->tid, taskpath);
        
        // IDEA: Current Working Directory is also shared between threads
        // Fix race condition here when it comes to changing directories
        DIR *dirp = opendir(taskpath);
        // If realpath() is not available
        char pathbuff[MAXPATH];   // use with constructing abspath corr to next lower dir or a regular file in curr dir
        char origcwd[MAXPATH];
        realpath(taskpath, origcwd);

        // Prepare grepbuff for when grep must be done on a file
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
            //assert(strlen(pathbuff) <= 250);

            // Do work on task.
            // Use isDir with abspath pathbuff, not relpath d->d_name to avoid chdir()-related race conditions!
            if (isDir(pathbuff)) {
                // We now have true concurrency of Enqueue and Dequeue operations
                Queue_Enqueue(&task_queue, pathbuff);   // act as a producer
                printf("[%d] ENQUEUE %s\n", data->tid, pathbuff);
                pthread_mutex_lock(&lock1);
                count++;
                pthread_cond_signal(&fill);
                pthread_mutex_unlock(&lock1);

            } else {
                // Search using absolute path!
                // Don't use relative path d->d_name as it's vulnerable to change in cwd.
                // NOTE: grep "search_string" "file_name"
                snprintf(grepbuff, BUFFSIZE, "grep \"%s\" \"%s\" > /dev/null", data->search_string, pathbuff);
                if (DEBUG) printf("[%d] grepbuff after snprintf: \"%s\"\n", data->tid, grepbuff);
                int check = system(grepbuff)/256;   // execute grep (issue with system() ret so divide by 256)
                if (check == 0) {
                    printf("[%d] PRESENT %s\n", data->tid, pathbuff);
                } else if (check == 1) {
                    printf("[%d] ABSENT %s\n", data->tid, pathbuff);
                } else {
                    printf("[%d] ERROR %s\n", data->tid, pathbuff); // should NOT appear
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
    pthread_mutex_init(&lock1, NULL);
    pthread_mutex_init(&lock2, NULL);
    pthread_cond_init(&fill, NULL);

    pthread_t thread[8];   
    struct thread_data data[8];

    // Init task queue and store first task (rootpath) into it
    Queue_Init(&task_queue);
    // IDEA: Only work with abs path, convert supplied rel path to abs path
    // If first char of rootpath is '/', it is an abs path, else it is rel path (must convert)
    count++;
    if (rootpath[0] == '/') {
        Queue_Enqueue(&task_queue, rootpath);
    } else {
        char origcwd[MAXPATH];
        getcwd(origcwd, MAXPATH);   // NOTE: getcwd() copies a null-terminated string to buff; store orig
        chdir(rootpath);             // switch to rel rootpath
        getcwd(rootpath, MAXPATH);  // get abspath equivalent of rel rootpath
        chdir(origcwd);              // restore orig
        Queue_Enqueue(&task_queue, rootpath);
    }
    // Queue_Log(&task_queue);
    
    // Launch Threads
    busyThreads = N;    // determines when thread exits
    for (int i = 0; i < N; i++) {
        data[i].tid = i;
        data[i].search_string = search_string;
        pthread_create(&thread[i], NULL, (void *) routine, &data[i]);
    }

    // Wait for Threads
    for (int i = 0; i < N; i++) {
        pthread_join(thread[i], NULL);
    }

    // Memory housekeeping
    // Free up dummy node remaining inside task queue
    free(task_queue.head);
    pthread_mutex_destroy(&task_queue.headLock);
    pthread_mutex_destroy(&task_queue.tailLock);

    pthread_mutex_destroy(&lock1); pthread_mutex_destroy(&lock2);
    pthread_cond_destroy(&fill);

    return 0;
}