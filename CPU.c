#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include "systemcall.h"
#include <time.h>

/*
This program does the following.
1) Creates handlers for two signals.
2) Create an idle process which will be executed when there is nothing
   else to do.
3) Create a send_signals process that sends a SIGALRM every so often.

The output looks approximately like:

[...]
Sending signal:  14 to process: 35050
Stopping: 35052
---- entering scheduler
Continuing idle: 35052
---- leaving scheduler
Sending signal:  14 to process: 35050
at the end of send_signals
Stopping: 35052
---- entering scheduler
Continuing idle: 35052
---- leaving scheduler
---- entering process_done
Timer died, cleaning up and killing everything
Terminated: 15

---------------------------------------------------------------------------
Add the following functionality.
1) Change the NUM_SECONDS to 20.

2) Take any number of arguments for executables and place each on the
   processes list with a state of NEW. The executables will not require
   arguments themselves.

3) When a SIGALRM arrives, scheduler() will be called; it currently simply
   restarts the idle process. Instead, do the following.
   a) Update the PCB for the process that was interrupted including the
      number of context switches and interrupts it had and changing its
      state from RUNNING to READY.
   b) If there are any NEW processes on processes list, change its state to
      RUNNING, and fork() and execl() it.
   c) If there are no NEW processes, round robin the processes in the
      processes queue that are READY. If no process is READY in the
      list, execute the idle process.

4) When a SIGCHLD arrives notifying that a child has exited, process_done() is
   called.
   a) Add the printing of the information in the PCB including the number
      of times it was interrupted, the number of times it was context
      switched (this may be fewer than the interrupts if a process
      becomes the only non-idle process in the ready queue), and the total
      system time the process took.
   b) Change the state to TERMINATED.
   c) Restart the idle process to use the rest of the time slice.
*/

#define NUM_SECONDS 2
#define ever ;;

enum STATE { NEW, RUNNING, WAITING, READY, TERMINATED, EMPTY };

struct PCB {
    enum STATE state;
    const char *name;   // name of the executable
    int pid;            // process id from fork();
    int ppid;           // parent process id
    int interrupts;     // number of times interrupted
    int switches;       // may be < interrupts
    int started;        // the time this process started
};

typedef enum { false, true } bool;

#define PROCESSTABLESIZE 10
struct PCB processes[PROCESSTABLESIZE];

struct PCB idle;
struct PCB *running;

int sys_time;
int timer;
double time_taken;
struct sigaction alarm_handler;
struct sigaction child_handler;
int processes_ready;
// pid_t fork_process;
int exit_status;

void bad(int signum) {
    WRITESTRING("bad signal: ");
    WRITEINT(signum, 4);
    WRITESTRING("\n");
}

// cdecl> declare ISV as array 32 of pointer to function(int) returning void
void(*ISV[32])(int) = {
/*       00   01   02   03   04   05   06   07   08   09 */
/*  0 */ bad, bad, bad, bad, bad, bad, bad, bad, bad, bad,
/* 10 */ bad, bad, bad, bad, bad, bad, bad, bad, bad, bad,
/* 20 */ bad, bad, bad, bad, bad, bad, bad, bad, bad, bad,
/* 30 */ bad, bad
};

void ISR (int signum) {
    if (signum != SIGCHLD) {
        kill (running->pid, SIGSTOP);
        WRITESTRING("Stopping: ");
        WRITEINT(running->pid, 6);
        WRITESTRING("\n");
    }

    ISV[signum](signum);
}

void send_signals(int signal, int pid, int interval, int number) {
    for(int i = 1; i <= number; i++) {
        sleep(interval);
        WRITESTRING("Sending signal: ");
        WRITEINT(signal, 4);
        WRITESTRING(" to process: ");
        WRITEINT(pid, 6);
        WRITESTRING("\n");
        systemcall(kill(pid, signal));
    }

    WRITESTRING("<<<Exiting send_signals\n");
}

void create_handler(int signum, struct sigaction action, void(*handler)(int)) {
    action.sa_handler = handler;

    if (signum == SIGCHLD) {
        action.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    } else {
        action.sa_flags =  SA_RESTART;
    }

    systemcall(sigemptyset(&action.sa_mask));
    systemcall(sigaction(signum, &action, NULL));
}

void scheduler (int signum) {
    clock_t duration;
    duration = clock();
    WRITESTRING("---- entering scheduler\n");
    assert(signum == SIGALRM);

    for(int i = 0; i < PROCESSTABLESIZE; i++) {

        if(processes[i].name != NULL && processes[i].state == NEW) {
            processes[i].state = RUNNING;
            pid_t fork_process = fork();
            if (fork_process < 0) {

                perror("fork() error.\n");
                exit(-1);
            } else if (fork_process == 0) {


                WRITESTRING("FORKED!!!!!!!!!!\n")
                processes[i].pid = getpid();
                processes[i].ppid = getppid();
                processes[i].started = sys_time;
                exit_status = execl(processes[i].name, processes[i].name, NULL);
                perror("execl() error.\n");
                exit(exit_status);
            } else {

                processes[i].interrupts++;
                processes[i].switches++;
                processes[i].state = READY;
                //assert(pause() == 0); // sigchld() auto called
            }
        }
    }

//    WRITESTRING("ABOUT TO LOOK FOR READYS,,,,,,,,,,,,,,\n")
//    processes_ready = 1;
//    for (int i = 0; i < PROCESSTABLESIZE; i++)
//    {
//        if (processes[i].name != NULL) {
//            WRITESTRING("INSIDE RD_ROBIN_FOR/IF\n")
//            printf("%u\n", processes[i].state);  //  returns 3/READY
//            if (processes[i].state == READY)
//            {
//                WRITESTRING("LOOKING FOR READY'S??????????????????\n")
////                assert(kill(running->pid, SIGSTOP) == 0);
////                errno = 0;
////                running->state = READY;
////                running->switches++;
////                assert(kill(processes[i].pid, SIGCONT) == 0);
//                processes[i].state = TERMINATED;
////                running = &processes[i];
////                running->switches++;
//
//            }
//        }
//    }
    processes_ready = 0;
    WRITESTRING("processes_ready = 0\n")

    // new processes go here: 3b

    //time the process
    duration = clock() - duration;
    time_taken = ((double)duration)/CLOCKS_PER_SEC;

    WRITESTRING ("Continuing idle: ");
    WRITEINT (idle.pid, 6);
    WRITESTRING ("\n");
    running = &idle;
    idle.state = RUNNING;
    systemcall (kill (idle.pid, SIGCONT));

    WRITESTRING("---- leaving scheduler\n");
}

void process_done (int signum) {
    WRITESTRING("---- entering process_done\n");

    WRITESTRING("Interrupts: ");
    WRITEINT(running->interrupts, 2);
    WRITESTRING("\n");
    WRITESTRING("Switches: ");
    WRITEINT(running->switches, 2);
    WRITESTRING("\n");
    WRITEINT(time_taken, 9);
    WRITESTRING(" seconds to execute process.\n")

    running->state = TERMINATED;
    systemcall(kill (idle.pid, SIGCONT));
    assert (signum == SIGCHLD);
    errno = 0;

    WRITESTRING ("Timer died, cleaning up and killing everything\n");
    WRITESTRING ("---- leaving process_done\n");
    systemcall(kill(0, SIGTERM));
}

void boot()
{
    sys_time = 0;

    ISV[SIGALRM] = scheduler;
    ISV[SIGCHLD] = process_done;
    create_handler(SIGALRM, alarm_handler, ISR);
    create_handler(SIGCHLD, child_handler, ISR);

    assert((timer = fork()) != -1);
    if (timer == 0) {
        send_signals(SIGALRM, getppid(), 1, NUM_SECONDS);
        exit(0);
    }
}

void create_idle() {
    idle.state = READY;
    idle.name = "IDLE";
    idle.ppid = getpid();
    idle.interrupts = 0;
    idle.switches = 0;
    idle.started = sys_time;

    assert((idle.pid = fork()) != -1);
    if (idle.pid == 0) {
        systemcall(pause());
    }
}

int main(int argc, char **argv) {

    boot();
    create_idle();
    running = &idle;

    for(int i = 0; i < argc - 1; i++) {

        WRITESTRING("main for loop\n")
        processes[i].name = argv[i];
        processes[i].state = NEW;
        printf("%s\n", processes[i].name);
    }

    for(ever) {
        pause();
    }


}