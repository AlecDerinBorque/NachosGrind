// threadtest.cc 
//	Simple test case for the threads assignment.
//
//	Create two threads, and have them context switch
//	back and forth between themselves by calling Thread::Yield, 
//	to illustratethe inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"

#if defined(CHANGED) && defined(THREADS)

#if defined(HW1_SEMAPHORES) || defined(HW1_LOCKS) || defined(HW1_CONDITIONS) \
    || defined(HW1_ELEVATOR)
#include "synch.h"
#endif

#ifdef HW1_ELEVATOR
//----------------------------------------------------------------------
// Exercise 4: Elevator
//
//	One elevator thread serves floors 1..numFloors.  Each person is a
//	thread that calls ArrivingGoingFromTo(atFloor, toFloor).
//
//	Design (all shared state is protected by elevLock):
//	  - The elevator sweeps in one direction while there is work
//	    ahead of it (people waiting or riders getting off), then
//	    reverses (LOOK scheduling), so requests are batched.
//	  - At each floor it first lets riders off, then lets people
//	    going in its current direction on, up to ELEVATOR_CAPACITY.
//	  - People wait on a per-floor condition (boardCond / exitCond);
//	    the elevator waits on elevCond for people to finish getting
//	    on/off, or for a new request when it is idle.
//	  - Moving one floor takes ELEVATOR_TICKS_PER_FLOOR ticks; the
//	    lock is released while moving so people can keep arriving.
//----------------------------------------------------------------------

#define ELEVATOR_CAPACITY	 5
#define ELEVATOR_TICKS_PER_FLOOR 50
#define ELEV_UP			 0
#define ELEV_DOWN		 1

static Lock *elevLock;		// protects everything below
static Condition *elevCond;	// elevator waits here
static Condition **boardCond;	// boardCond[f]: people waiting at floor f
static Condition **exitCond;	// exitCond[f]: riders going to floor f
static int elevNumFloors;
static int elevFloor;		// floor the elevator is at
static int elevDir;		// ELEV_UP or ELEV_DOWN
static int elevRiders;		// people currently inside
static bool elevBoarding;	// doors open for people to get on
static bool elevExiting;	// doors open for riders to get off
static int *waiting[2];		// waiting[dir][f]: people at f going dir
static int *exiting;		// exiting[f]: riders whose destination is f
static int nextPersonId;

// Is there anyone waiting at, or any rider going to, a floor strictly
// beyond floor f in direction dir?
static bool
WorkAhead(int f, int dir)
{
    int step = (dir == ELEV_UP) ? 1 : -1;
    for (int g = f + step; g >= 1 && g <= elevNumFloors; g += step)
        if (waiting[ELEV_UP][g] > 0 || waiting[ELEV_DOWN][g] > 0
            || exiting[g] > 0)
            return TRUE;
    return FALSE;
}

static bool
AnyRequests()
{
    for (int f = 1; f <= elevNumFloors; f++)
        if (waiting[ELEV_UP][f] > 0 || waiting[ELEV_DOWN][f] > 0
            || exiting[f] > 0)
            return TRUE;
    return FALSE;
}

// Pick the direction to travel from the current floor.  Riders always
// boarded in the current direction, so while anyone is inside we keep
// going.  When empty, keep going if there is work ahead or someone here
// wants to go our way; otherwise turn around.
static void
ChooseDirection()
{
    int f = elevFloor;
    int other = 1 - elevDir;

    if (elevRiders > 0)
        return;
    if (WorkAhead(f, elevDir) || waiting[elevDir][f] > 0)
        return;
    if (WorkAhead(f, other) || waiting[other][f] > 0)
        elevDir = other;
}

static void
ElevatorThread(int unused)
{
    elevLock->Acquire();
    for (;;) {
        int f = elevFloor;

        // 1. Let riders out.
        elevExiting = TRUE;
        exitCond[f]->Broadcast(elevLock);
        while (exiting[f] > 0)
            elevCond->Wait(elevLock);
        elevExiting = FALSE;

        // 2. Decide direction, then let people going that way in.
        ChooseDirection();
        elevBoarding = TRUE;
        boardCond[f]->Broadcast(elevLock);
        while (waiting[elevDir][f] > 0 && elevRiders < ELEVATOR_CAPACITY)
            elevCond->Wait(elevLock);
        elevBoarding = FALSE;

        // 3. Nothing to do anywhere: sleep until someone arrives.
        if (!AnyRequests()) {
            elevCond->Wait(elevLock);
            continue;		// re-serve this floor, don't move
        }

        // 4. Work only at this floor in the other direction (we were
        //    full or just turned): serve it again before moving.
        ChooseDirection();
        if (!WorkAhead(f, elevDir))
            continue;

        // 5. Move one floor.  Release the lock so people can arrive.
        elevLock->Release();
        for (int t = 0; t < ELEVATOR_TICKS_PER_FLOOR; t++)
            currentThread->Yield();
        elevLock->Acquire();

        elevFloor += (elevDir == ELEV_UP) ? 1 : -1;
        printf("Elevator arrives on floor %d.\n", elevFloor);
    }
}

//----------------------------------------------------------------------
// Elevator
//	Set up shared state for a building with numFloors floors and
//	start the elevator thread at floor 1.
//----------------------------------------------------------------------

void
Elevator(int numFloors)
{
    ASSERT(numFloors >= 1);

    elevNumFloors = numFloors;
    elevFloor = 1;
    elevDir = ELEV_UP;
    elevRiders = 0;
    elevBoarding = FALSE;
    elevExiting = FALSE;
    nextPersonId = 1;

    elevLock = new Lock("elevator lock");
    elevCond = new Condition("elevator");
    boardCond = new Condition *[numFloors + 1];
    exitCond = new Condition *[numFloors + 1];
    waiting[ELEV_UP] = new int[numFloors + 1];
    waiting[ELEV_DOWN] = new int[numFloors + 1];
    exiting = new int[numFloors + 1];
    for (int f = 0; f <= numFloors; f++) {
        boardCond[f] = new Condition("board");
        exitCond[f] = new Condition("exit");
        waiting[ELEV_UP][f] = waiting[ELEV_DOWN][f] = exiting[f] = 0;
    }

    Thread *t = new Thread("elevator");
    t->Fork(ElevatorThread, 0);
}

//----------------------------------------------------------------------
// ArrivingGoingFromTo
//	Called by a person thread.  Requests the elevator at atFloor,
//	rides it to toFloor, and returns once the person has gotten out.
//----------------------------------------------------------------------

void
ArrivingGoingFromTo(int atFloor, int toFloor)
{
    ASSERT(atFloor >= 1 && atFloor <= elevNumFloors);
    ASSERT(toFloor >= 1 && toFloor <= elevNumFloors);

    elevLock->Acquire();
    int id = nextPersonId++;
    printf("Person %d wants to go to floor %d from floor %d.\n",
           id, toFloor, atFloor);

    if (atFloor == toFloor) {	// already there
        elevLock->Release();
        return;
    }

    int dir = (toFloor > atFloor) ? ELEV_UP : ELEV_DOWN;
    waiting[dir][atFloor]++;
    elevCond->Signal(elevLock);	// wake the elevator if idle

    // Wait for the elevator to be here, heading our way, with room.
    while (!(elevBoarding && elevFloor == atFloor && elevDir == dir
             && elevRiders < ELEVATOR_CAPACITY))
        boardCond[atFloor]->Wait(elevLock);

    waiting[dir][atFloor]--;
    elevRiders++;
    exiting[toFloor]++;
    printf("Person %d got into the elevator.\n", id);
    elevCond->Signal(elevLock);

    // Ride until the doors open at our floor.
    while (!(elevExiting && elevFloor == toFloor))
        exitCond[toFloor]->Wait(elevLock);

    exiting[toFloor]--;
    elevRiders--;
    printf("Person %d got out of the elevator.\n", id);
    elevCond->Signal(elevLock);

    elevLock->Release();
}

//----------------------------------------------------------------------
// Test driver: n people arrive at random times going between random
// floors.
//----------------------------------------------------------------------

#define ELEVATOR_TEST_FLOORS 5

static void
PersonThread(int unused)
{
    int at = Random() % ELEVATOR_TEST_FLOORS + 1;
    int to = Random() % (ELEVATOR_TEST_FLOORS - 1) + 1;
    if (to >= at)
        to++;			// pick a floor other than "at"

    int delay = Random() % 300;	// stagger arrival times
    for (int i = 0; i < delay; i++)
        currentThread->Yield();

    ArrivingGoingFromTo(at, to);
}

static void
ElevatorTest(int numPeople)
{
    Elevator(ELEVATOR_TEST_FLOORS);
    for (int i = 0; i < numPeople; i++) {
        Thread *t = new Thread("person");
        t->Fork(PersonThread, i);
    }
}
#endif // HW1_ELEVATOR

int SharedVariable;

#if defined(HW1_SEMAPHORES)
static Semaphore *mutex;	// guards SharedVariable and numDone
static Semaphore *barrier;	// released once every thread leaves the loop
#elif defined(HW1_LOCKS)
static Lock *mutex;		// guards SharedVariable and numDone
static Semaphore *barrier;	// released once every thread leaves the loop
#elif defined(HW1_CONDITIONS)
static Lock *mutex;		// guards SharedVariable and numDone
static Lock *barrierLock;
static Condition *barrier;
#endif
#if defined(HW1_SEMAPHORES) || defined(HW1_LOCKS) || defined(HW1_CONDITIONS)
static int numThreads;		// total threads running SimpleThread
static int numDone;		// threads that have finished the loop
#endif

//----------------------------------------------------------------------
// SimpleThread
// 	Loop 5 times, incrementing SharedVariable and yielding the CPU
//	to another ready thread each iteration.
//
//	With HW1_SEMAPHORES defined, each read-modify-write of
//	SharedVariable is done under a mutex, and all threads wait at a
//	barrier before reading the final value.
//
//	"which" is simply a number identifying the thread, for debugging
//	purposes.
//----------------------------------------------------------------------

void
SimpleThread(int which)
{
    int num, val;

    for (num = 0; num < 5; num++) {
#ifdef HW1_SEMAPHORES
        mutex->P();
#elif defined(HW1_LOCKS) || defined(HW1_CONDITIONS)
    mutex->Acquire();
#endif
        val = SharedVariable;
        printf("*** thread %d sees value %d\n", which, val);
        currentThread->Yield();
        SharedVariable = val+1;
#ifdef HW1_SEMAPHORES
        mutex->V();
#elif defined(HW1_LOCKS) || defined(HW1_CONDITIONS)
    mutex->Release();
#endif
        currentThread->Yield();
    }

#ifdef HW1_SEMAPHORES
    mutex->P();
    numDone++;
    if (numDone == numThreads)
        barrier->V();
    mutex->V();
    barrier->P();		// turnstile: each thread passes it on
    barrier->V();
#elif defined(HW1_LOCKS)
    mutex->Acquire();
    numDone++;
    if (numDone == numThreads)
	barrier->V();
    mutex->Release();
    barrier->P();		// turnstile: each thread passes it on
    barrier->V();
#elif defined(HW1_CONDITIONS)
    barrierLock->Acquire();
    numDone++;
    if (numDone == numThreads)
	barrier->Broadcast(barrierLock);
    else
	while (numDone < numThreads)
	    barrier->Wait(barrierLock);
    barrierLock->Release();
#endif

    val = SharedVariable;
    printf("Thread %d sees final value %d\n", which, val);
}

//----------------------------------------------------------------------
// ThreadTest
// 	Fork n new threads to call SimpleThread, and then call
//	SimpleThread ourselves.
//----------------------------------------------------------------------

void
ThreadTest(int n)
{
    DEBUG('t', "Entering ThreadTest");

#ifdef HW1_ELEVATOR
    ElevatorTest(n);		// -q n = number of people
    return;
#endif

#ifdef HW1_SEMAPHORES
    mutex = new Semaphore("SharedVariable mutex", 1);
    barrier = new Semaphore("SimpleThread barrier", 0);
#elif defined(HW1_LOCKS)
    mutex = new Lock("SharedVariable mutex");
    barrier = new Semaphore("SimpleThread barrier", 0);
#elif defined(HW1_CONDITIONS)
    mutex = new Lock("SharedVariable mutex");
    barrierLock = new Lock("SimpleThread barrier lock");
    barrier = new Condition("SimpleThread barrier");
#endif
#if defined(HW1_SEMAPHORES) || defined(HW1_LOCKS) || defined(HW1_CONDITIONS)
    numThreads = n + 1;
    numDone = 0;
#endif

    for (int i = 1; i <= n; i++) {
        Thread *t = new Thread("forked thread");
        t->Fork(SimpleThread, i);
    }
    SimpleThread(0);
}

#else

// testnum is set in main.cc
int testnum = 1;

//----------------------------------------------------------------------
// SimpleThread
// 	Loop 5 times, yielding the CPU to another ready thread 
//	each iteration.
//
//	"which" is simply a number identifying the thread, for debugging
//	purposes.
//----------------------------------------------------------------------

void
SimpleThread(int which)
{
    int num;
    
    for (num = 0; num < 5; num++) {
	printf("*** thread %d looped %d times\n", which, num);
        currentThread->Yield();
    }
}

//----------------------------------------------------------------------
// ThreadTest1
// 	Set up a ping-pong between two threads, by forking a thread 
//	to call SimpleThread, and then calling SimpleThread ourselves.
//----------------------------------------------------------------------

void
ThreadTest1()
{
    DEBUG('t', "Entering ThreadTest1");

    Thread *t = new Thread("forked thread");

    t->Fork(SimpleThread, 1);
    SimpleThread(0);
}

//----------------------------------------------------------------------
// ThreadTest
// 	Invoke a test routine.
//----------------------------------------------------------------------

void
ThreadTest()
{
    switch (testnum) {
    case 1:
	ThreadTest1();
	break;
    default:
	printf("No test specified.\n");
	break;
    }
}

#endif
