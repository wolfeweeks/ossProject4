/**
 * @file oss.c
 * @author Wolfe Weeks
 * @date 2023-04-04
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/msg.h>
#include <sys/ipc.h>

#include "shared_memory.h"

#define PERMS 0644

struct PCB {
  int occupied; // either true or false
  pid_t pid; // process id of this child
  int simPid;
  int startSeconds; // time when it was forked
  int startNano; // time when it was forked
  int cpuSeconds;
  int cpuNano;
  int termSeconds;
  int termNano;
};

struct MessageBuffer {
  long mtype;
  // int durationSec;
  int durationNano;
};

struct QueueNode {
  int pid;
  struct QueueNode* next;
};

struct Queue {
  struct QueueNode* front;
  struct QueueNode* rear;
  int size;
};

void initQueue(struct Queue* q) {
  q->front = NULL;
  q->rear = NULL;
  q->size = 0;
}

int isQueueEmpty(struct Queue* q) {
  return q->front == NULL;
}

void enqueue(struct Queue* q, int pid) {
  struct QueueNode* newNode = (struct QueueNode*)malloc(sizeof(struct QueueNode));
  newNode->pid = pid;
  newNode->next = NULL;

  if (isQueueEmpty(q)) {
    q->front = newNode;
    q->rear = newNode;
  } else {
    q->rear->next = newNode;
    q->rear = newNode;
  }

  q->size++;
}

int dequeue(struct Queue* q) {
  if (isQueueEmpty(q)) {
    return -1; // or some other error code
  }

  struct QueueNode* frontNode = q->front;
  int pid = frontNode->pid;

  if (q->front == q->rear) {
    q->front = NULL;
    q->rear = NULL;
  } else {
    q->front = frontNode->next;
  }

  free(frontNode);
  q->size--;

  return pid;
}

int proc;
struct PCB processTable[18];
int* block;
int msqid;
FILE* file;

// This function is a signal handler that handles three different signals: SIGPROF, SIGTERM, and SIGINT.
static void myhandler(int s) {
  // If a SIGPROF or SIGTERM signal is received, terminate all running processes and exit the program.
  if (s == SIGPROF || s == SIGTERM) {
    printf("Timeout or termination signal received, terminating...\n");

    // Loop through the process table and send the signal to each process that is currently running.
    int i;
    for (i = 0; i < 18; i++) {
      if (processTable[i].occupied == true) {
        kill(processTable[i].pid, s);
      }
    }

    // Destroy the memory block and exit with an error code.
    destroy_memory_block("README.txt");
    detach_memory_block(block);

    // Remove the message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1) {
      perror("msgctl");
      exit(1);
    }

    exit(-1);
  }
  // If a SIGINT signal is received, terminate all running processes and exit the program.
  else if (s == SIGINT) {
    printf("Ctrl-c signal received, cleaning up and terminating...\n");

    // Loop through the process table and send the signal to each process that is currently running.
    int i;
    for (i = 0; i < 18; i++) {
      if (processTable[i].occupied) {
        kill(processTable[i].pid, s);
      }
    }

    // Destroy the memory block and exit with an error code.
    destroy_memory_block("README.txt");
    detach_memory_block(block);

    // Remove the message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1) {
      perror("msgctl");
      exit(1);
    }

    exit(-1);
  }
}

static int setupinterrupt(void) { /* set up myhandler for SIGPROF */
  struct sigaction act;
  act.sa_handler = myhandler;
  act.sa_flags = 0;
  return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
}

static int setupitimer(void) { /* set ITIMER_PROF for 2-second intervals */
  struct itimerval value;
  value.it_interval.tv_sec = 3;
  value.it_interval.tv_usec = 0;
  value.it_value = value.it_interval;
  return (setitimer(ITIMER_PROF, &value, NULL));
}

// This function increments the simulated system clock by the passed in nanoseconds.
void incrementClock(int* clock, int* block, int nano) {
  // Add the passed in nanoseconds to the second element of the clock array.
  clock[1] += nano;

  // If the second element of the clock array has exceeded or equalled 1 billion nanoseconds,
  // increment the first element of the clock array by 1 and subtract 1 billion nanoseconds from the second element.
  if (clock[1] >= 1000000000) {
    clock[0] += 1;
    clock[1] -= 1000000000;
  }

  // Copy the contents of the clock array to the memory block.
  memcpy(block, clock, sizeof(int) * 2);
}

int* getNextScheduleTime(int* clock, int* lastTimeScheduled, int* maxTimeBetweenProcs) {
  int* nextTime = (int*)malloc(2 * sizeof(int));  // Allocate memory for the two-element long int array
  int elapsedSeconds = clock[0] - lastTimeScheduled[0];  // Calculate the elapsed seconds since the last process was scheduled
  int elapsedNanoseconds = clock[1] - lastTimeScheduled[1];  // Calculate the elapsed nanoseconds since the last process was scheduled
  int maxSeconds = maxTimeBetweenProcs[0];  // Get the maximum number of seconds between processes
  int maxNanoseconds = maxTimeBetweenProcs[1];  // Get the maximum number of nanoseconds between processes
  int maxElapsedNanoseconds = maxSeconds * 1000000000 + maxNanoseconds;  // Convert the maximum time between processes to nanoseconds
  int elapsedTotalNanoseconds = elapsedSeconds * 1000000000 + elapsedNanoseconds;  // Convert the elapsed time to nanoseconds
  int remainingNanoseconds = maxElapsedNanoseconds - elapsedTotalNanoseconds;  // Calculate the remaining time in nanoseconds
  int randomNanoseconds = rand() % remainingNanoseconds;  // Generate a random number of nanoseconds between 0 and the remaining time
  int totalNanoseconds = elapsedTotalNanoseconds + randomNanoseconds;  // Calculate the total number of nanoseconds until the next process is scheduled
  nextTime[0] = clock[0] + totalNanoseconds / 1000000000;  // Calculate the number of seconds until the next process is scheduled
  nextTime[1] = clock[1] + totalNanoseconds % 1000000000;  // Calculate the number of remaining nanoseconds until the next process is scheduled
  if (nextTime[1] >= 1000000000) {  // Check if the remaining nanoseconds is greater than or equal to 1 second
    nextTime[0] += 1;  // Increment the number of seconds if necessary
    nextTime[1] -= 1000000000;  // Subtract 1 second from the remaining nanoseconds
  }
  return nextTime;
}

int processTableIsFull() {
  int i;
  for (i = 0; i < 18; i++) {
    if (processTable[i].occupied == false) return false;
  }
  return true;
}

bool isFileOverLimit() {
  // Initialize the line counter
  int lineCount = 0;

  // Loop through the file until the end is reached or the limit is exceeded
  char c;
  while ((c = fgetc(file)) != EOF) {
    if (c == '\n') {
      lineCount++;
    }
    if (lineCount >= 10000) {
      return true;
    }
  }

  // Return false if the limit was not exceeded
  return false;
}

int main(int argc, char* argv[]) {
  if (setupinterrupt() == -1) {
    printf("Failed to set up handler for SIGPROF\n");
    exit(-1);
  }
  if (setupitimer() == -1) {
    printf("Failed to set up 60s timer\n");
    exit(-1);
  }

  // Initialize variables to hold command line options
  int proc = 1, simul = 1, limit = 5;
  char* outputFile = "output.txt";

  // set occupied values in process control block array to false
  int i;
  for (i = 0; i < proc; i++) {
    processTable[i].occupied = false;
  }

  // Use getopt() function to parse command line options
  int opt;
  while ((opt = getopt(argc, argv, "n:s:t:f:h")) != -1) {
    switch (opt) {
    case 'n':
      proc = atoi(optarg);
      break;
    case 's':
      simul = atoi(optarg);
      break;
    case 't':
      limit = atoi(optarg);
      break;
    case 'h':
      printf("Usage: oss [-h] [-n proc] [-s simul] [-t timelimit] [-f outputFile]\n");
      printf("\t-h (optional) shows a help message\n");
      printf("\t-n (optional) is the total number of child processes to create\n");
      printf("\t-s (optional) is the maximum number of concurrent child processes\n");
      printf("\t-t (optional) is the maximum number of seconds each child process should run for\n");
      printf("\t-f (optional) is the filename to output the PCBs to");
      return 0;
    case 'f':
      outputFile = (char*)malloc(strlen(optarg) + 1);
      strcpy(outputFile, optarg);
      break;
    default:
      printf("Invalid option\n");
      return 1;
    }
  }

  file = fopen(outputFile, "w+");

  // initialize the clock that will be put in shared memory
  int clock[] = { 0, 0 };

  // get shared memory block for clock
  block = attach_memory_block("README.txt", sizeof(int) * 2);

  // If block is NULL, it means that shared memory block for clock couldn't be obtained.
  if (block == NULL) {
    printf("ERROR: couldn't get shared memory block\n");
    exit(1);
  }

  // Copy the contents of the clock array to the memory block.
  memcpy(block, clock, sizeof(int) * 2);

  // int remaining = proc;
  // int running = 0;

  int prevClock[] = { 0, 0 };

  key_t key;

  if ((key = ftok("README.txt", 1)) == -1) {
    perror("parent ftok");
    exit(1);
  }

  if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1) { /* connect to the queue */
    perror("parent msgget");
    exit(1);
  }

  int lastTimeScheduled[] = { 0, 0 };
  int maxTimeBetweenProcs[] = { 0, 1000000 };
  int nextScheduleTime[] = { 0, 0 };

  while (1) {
    // if (processTableIsFull()) continue;

    // If it is time to schedule another process
    if (clock[0] > nextScheduleTime[0] || (clock[0] == nextScheduleTime[0] && clock[1] >= nextScheduleTime[1])) {
      if (!isFileOverLimit()) {
        fprintf(file, "OSS: Generating process with PID %d and putting it in queue 0 at time %d:%d\n", 0, clock[0], clock[1]);
      }

      pid_t pid = fork();

      // If fork failed.
      if (pid < 0) {
        printf("Fork failed!\n");
        exit(1);
      }
      // If this is the child process.
      else if (pid == 0) {
        // Update the process table for the child process.
        processTable[0].pid = pid;
        processTable[0].simPid = 0;
        processTable[0].occupied = true;
        processTable[0].startSeconds = clock[0];
        processTable[0].startNano = clock[1];

        struct MessageBuffer buf;

        buf.mtype = getpid();
        buf.durationNano = 500000;

        if (msgsnd(msqid, &buf, sizeof(struct MessageBuffer) - sizeof(long), 0) == -1) {
          printf("msgsnd to %d failed\n", getpid());
          exit(1);
        }

        incrementClock(clock, block, 1000);
        if (!isFileOverLimit()) {
          fprintf(file, "OSS: total time this dispatch was 1000 nanoseconds");
        }

        // Execute the worker program with the random seconds and nanoseconds as arguments.
        execl("./worker", "./worker", NULL);
      }
      // If this is the parent process.
      else {
        struct MessageBuffer recBuf;

        if (msgrcv(msqid, &recBuf, sizeof(struct MessageBuffer), getpid(), 0) == -1) {
          perror("failed to receive message from child\n");
          exit(1);
        }

        printf("message from child:%d\n", recBuf.durationNano);

        if (recBuf.durationNano == 500000) {
          if (!isFileOverLimit()) {
            fprintf(file, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", 0, recBuf.durationNano);
          }
          incrementClock(clock, block, 500000);
        } else if (recBuf.durationNano < 0) {
          if (!isFileOverLimit()) {
            fprintf(file, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", 0, -1 * recBuf.durationNano);
            fprintf(file, "OSS: Not using its entire time quantum\n");
            fprintf(file, "OSS: Putting process with PID %d into blocked queue\n", 0);
          }
          incrementClock(clock, block, -1 * recBuf.durationNano);
        } else {
          if (!isFileOverLimit()) {
            fprintf(file, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", 0, recBuf.durationNano);
            fprintf(file, "OSS: Process with PID %d terminated early\n", 0);
          }
          incrementClock(clock, block, recBuf.durationNano);

          processTable[0].pid = NULL;
          processTable[0].simPid = NULL;
          processTable[0].occupied = false;
          processTable[0].startSeconds = NULL;
          processTable[0].startNano = NULL;
        }

        lastTimeScheduled[0] = clock[0];
        lastTimeScheduled[1] = clock[1];
        int* next = getNextScheduleTime(clock, lastTimeScheduled, maxTimeBetweenProcs);
        nextScheduleTime[0] = next[0];
        nextScheduleTime[1] = next[1];
        printf("next: %d:%d\n", next[0], next[1]);
      }
    } else {
      // printf("difference: %d:%d\n", nextScheduleTime[0] - lastTimeScheduled[0], nextScheduleTime[1] - lastTimeScheduled[1]);
      if (!isFileOverLimit()) {
        fprintf(file, "OSS: Not time to schedule another process\n");
      }
    }
  }

  if (!destroy_memory_block("README.txt")) {
    printf("ERROR: Couldn't destroy shared memory block\n");
  }

  detach_memory_block(block);

  // Remove the message queue
  if (msgctl(msqid, IPC_RMID, NULL) == -1) {
    perror("msgctl");
    exit(1);
  }

  return 0;
}
