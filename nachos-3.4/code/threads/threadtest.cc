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

#if defined(HW1_SEMAPHORES) || defined(HW1_LOCKS) || defined(HW1_CONDITIONS)
#include "synch.h"
#endif

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
