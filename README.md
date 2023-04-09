# **CS 140 Project 2: Parallelized `grep` Runner**

## **Description**
For this Operating Systems project, our goal was to **parallelize `grep`**, Linux's primary RegEx search utility.
The concurrency topics of thread synchronization primitives such as locks, condition
variables, and semaphores, critical sections in code, and thread safety and locality
are key here. For the final output, see `multithreaded.c`.
A single-threaded version is available as `single.c`.

A prudent attempt at making a multiprocessing version was also done by on-the-go study of multiprocess-safety and implementation
of interprocess communication constructs such as shared files and shared memory sections (e.g. via `mmap()`).
For the result, see `multiprocess.c`. This is almost working, the only issue was with the chain termination of sibling processes.

Main language used was C. Compilation and testing were done through Oracle VirtualBox
using CS 140's prebuilt Ubuntu appliance. Other tools used include Valgrind for catching memory leaks.

Worker thread behavior in `multithreaded.c` along with their outputs and control signals are shown in the flowchart below.
![worker_behavior.png](extras/worker_behavior_2.png)

### **References**
We immediately show our references here as they contain important realizations.
- OSTEP (Ch. 29): The chapter’s threadsafe implementation of a dynamic queue served as a guide 
for my own implementation which utilizes a heap-based unbounded buffer for the task queue. 
- OSTEP  (Ch.  30):  The  chapter’s discussion  on  the  producer-consumer  bounded  buffer  problem 
served as guide for my use of locks and condition variables for thread synchronization. 
- OSTEP  (Ch.  39):  This  chapter  helped  me  understand  directory  traversal  and  the  handling  of 
absolute and relative paths. 
- https://stackoverflow.com/questions/3736320/executing-shell-script-with-system-returns-256-
what-does-that-mean : For why we should divide the return value of system() by 256. 
- https://unix.stackexchange.com/questions/119648/redirecting-to-dev-null : For how to redirect 
to /dev/null. 
- https://stackoverflow.com/questions/4553012/checking-if-a-file-is-a-directory-or-just-a-file : For 
how  to  check  if  a  “file”  is  a  regular  file  or  a  directory.  This  is  where  I  got  the  idea  of  the  isDir 
function from.

*OSTEP:* Arpaci-Dusseau, A. C. & Arpaci-Dusseau, R. H. (2018). *Operating Systems: Three Easy Steps.* University of Wisconsin-Madison. https://research.cs.wisc.edu/wind/OSTEP/

### **Quick Links**
- Full project specifications are in `extras\CS140_Project2_Specs.pdf`. Please email me for access.
- Project Documentation can be found in `extras\cs140project2.pdf`.
- Video Documentation can be found [here](https://drive.google.com/file/d/1TQsPHLUj58J7ZiazSH22b_iuG5ZbK7XZ/view?usp=share_link).

## **Requirements**
It is suggested for you to only watch the video documentation or read the written documentation.

However, if you really wish to verify the results on your end, the following would be needed.
- Windows 7 or higher. Skip the next bullet if you already have a Linux environment.
- Latest version of [WSL](https://learn.microsoft.com/en-us/windows/wsl/install) (recommended, any Distro would do). Alternative would be to set up a Linux environment in a Virtual Machine, but lightweight testing can be accomplished through WSL.
- **Important:** You must have **at least 4 CPU cores** in your Linux environment to proceed. 
- Upon setting-up your Linux environment, ensure that you have a `gcc` compiler. This project is untested on `clang`.
- Version 3.7 of `grep`.
- Optional: `tree` for visualizing the contents of the input path or directory. To install, enter `sudo apt install tree`.

For running tests, please proceed directly to the next section.

## **Running Tests**
This walkthrough will only focus on `multithreaded.c`. A test directory `testdir\` in which you could run the multithreaded `grep` is already provided for your convenience. The long file and folder names are used as edge cases (noting that the absolute path to files can only have at most 250 characters). Analagous steps can be taken for your own tests on different directories. You may also populate `testdir\` or your own directory with your own files (text files are most recommended).

To make use of the provided test directory:
1. Launch `wsl` on a terminal.
2. `cd` to the root directory of this project.
3. Enter `gcc multithreaded.c -lpthread -o multithreaded`. This compiles the file as an executable named `multithreaded`. Note the POSIX multithreading tag. Other filenames could be used. Important, this program has the following execution format:
```shell
./multithreaded <number_of_threads> <root_directory> <search_string>

# number_of_threads: number of worker threads
# root_directory: dir in which grep exploration will begin (first dir to be enqueued to task queue)
# search_string: regular expression for use with grep
```
4. For this example, let's enter `./multithreaded 4 testdir hello`. That is, there will be 1 main thread that launches 4 worker threads that will concurrently comb all files in testdir and its subdirectories for instances of the string "hello".
5. Verify the results by checking the output in the terminal for any errors. The root absolute path and all its subdirectories must have been enqueued (`ENQUEUE`) and explored (`DIR`), and all files in each directory must have been grep'd, resulting in either `PRESENT` or `ABSENT`.
6. Note that the utility `tree` is particularly useful for checking if your multithreaded grep runner has behaved properly. Try entering `tree testdir` to test this particular case.

Other suggested test cases:
```shell
./multithreaded 4 . hello       # in the project directory
./multithreaded 4 /home hello   # a larger directory

# Number of worker threads could be any positive integer, but the number of them
# that could work simultaneously is bounded above by the number of your CPU cores.
# Only valid paths will be accepted.
# The test regular expression may also be modified to your liking.
# Try * to get all PRESENT.
```

 


---
Yenzy Urson S. Hebron

University of the Philippines Diliman

1st Semester A.Y. 2022-2023

© Course Materials by Sir Wilson Tan, Sir Juan Felipe Coronel, and Ma'am Angela Zabala