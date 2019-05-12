#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "uthread.h"
#include "uthread_mutex_cond.h"

#define NUM_ITERATIONS 20000

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__)
#else
#define VERBOSE_PRINT(S, ...) ((void) 0) // do nothing
#endif

struct Agent {
  uthread_mutex_t mutex;
  uthread_cond_t  match;
  uthread_cond_t  paper;
  uthread_cond_t  tobacco;
  uthread_cond_t  smoke;
};

struct Agent* createAgent() {
  struct Agent* agent = malloc (sizeof (struct Agent));
  agent->mutex   = uthread_mutex_create();
  agent->paper   = uthread_cond_create (agent->mutex);
  agent->match   = uthread_cond_create (agent->mutex);
  agent->tobacco = uthread_cond_create (agent->mutex);
  agent->smoke   = uthread_cond_create (agent->mutex);
  return agent;
}

/**
 * You might find these declarations helpful.
 *   Note that Resource enum had values 1, 2 and 4 so you can combine resources;
 *   e.g., having a MATCH and PAPER is the value MATCH | PAPER == 1 | 2 == 3
 */
enum Resource            {    MATCH = 1, PAPER = 2,   TOBACCO = 4};
char* resource_name [] = {"", "match",   "paper", "", "tobacco"};

int signal_count [5];  // # of times resource signalled
int smoke_count  [5];  // # of times smoker with resource smoked

int middleThreadsReady = 0;
int smokerThreadsReady = 0;
enum Resource sigReceived[2];
int sigIndex = 0;
int SignalsUpdated = 0; // signals updated if value = 2; else not updated
uthread_mutex_t handlerlock;
uthread_cond_t middleTReady;
uthread_cond_t sigUpdate;
uthread_mutex_t smokerlock;
uthread_cond_t smokerM;
uthread_cond_t smokerP;
uthread_cond_t smokerT;
uthread_cond_t smokersReady;

struct SignalHandler {
  enum Resource resource;
  struct Agent* agent;
};

struct SignalHandler* CreateSGHandler (enum Resource r, struct Agent* a) {
  struct SignalHandler* SGHandler = malloc(sizeof(struct SignalHandler));
  SGHandler->resource = r;
  SGHandler->agent = a;
  return SGHandler;
}

void* handle(void* s) {
  struct SignalHandler* sgh  = s;
  enum Resource r = sgh->resource;
  uthread_mutex_t mx = sgh->agent->mutex;
  while (1) {
    uthread_mutex_lock(mx);
    if (r == MATCH) {
      uthread_mutex_lock(handlerlock);
      middleThreadsReady++;
      uthread_cond_signal(middleTReady);
      uthread_mutex_unlock(handlerlock);
      uthread_cond_wait(sgh->agent->match);
      uthread_mutex_unlock(mx);
      uthread_mutex_lock(handlerlock);
      sigReceived[sigIndex] = MATCH;
      sigIndex++;
      SignalsUpdated++;
      uthread_cond_signal(sigUpdate);
      uthread_mutex_unlock(handlerlock);
    }
    else if (r == PAPER) {
      uthread_mutex_lock(handlerlock);
      middleThreadsReady++;
      uthread_cond_signal(middleTReady);
      uthread_mutex_unlock(handlerlock);
      uthread_cond_wait(sgh->agent->paper);
      uthread_mutex_unlock(mx);
      uthread_mutex_lock(handlerlock);
      sigReceived[sigIndex] = PAPER;
      sigIndex++;
      SignalsUpdated++;
      uthread_cond_signal(sigUpdate);
      uthread_mutex_unlock(handlerlock);
    }
    else if (r == TOBACCO) {
      uthread_mutex_lock(handlerlock);
      middleThreadsReady++;
      uthread_cond_signal(middleTReady);
      uthread_mutex_unlock(handlerlock);
      uthread_cond_wait(sgh->agent->tobacco);
      uthread_mutex_unlock(mx);
      uthread_mutex_lock(handlerlock);
      sigReceived[sigIndex] = TOBACCO;
      sigIndex++;
      SignalsUpdated++;
      uthread_cond_signal(sigUpdate);
      uthread_mutex_unlock(handlerlock);
    }
  }
  return NULL;
}

void* findSmoker(void* x) {
  struct Agent* a = x;
  
  while (1) {
    uthread_mutex_lock(handlerlock);
    while(middleThreadsReady < 2) {
      uthread_cond_wait(middleTReady);
    }
    while(SignalsUpdated < 2) {
      uthread_cond_wait(sigUpdate);
    }

    SignalsUpdated = 0;
    sigIndex = 0;
    middleThreadsReady = 0;
    uthread_mutex_unlock(handlerlock);
    uthread_mutex_lock(smokerlock);
    while (smokerThreadsReady < 1) {
      uthread_cond_wait(smokersReady);
    }
    uthread_mutex_unlock(smokerlock);
    if ((sigReceived[0] == MATCH && sigReceived[1] == PAPER) ||
      (sigReceived[0] == PAPER && sigReceived[1] == MATCH)) {
      uthread_mutex_lock(smokerlock);
      uthread_cond_signal(smokerT);
      uthread_cond_wait(smokersReady);
      uthread_mutex_unlock(smokerlock);
    }
    else if ((sigReceived[0] == MATCH && sigReceived[1] == TOBACCO) ||
      (sigReceived[0] == TOBACCO && sigReceived[1] == MATCH)) {
      uthread_mutex_lock(smokerlock);
      uthread_cond_signal(smokerP);
      uthread_cond_wait(smokersReady);
      uthread_mutex_unlock(smokerlock);
    }

    else {
      uthread_mutex_lock(smokerlock);
      uthread_cond_signal(smokerM);
      uthread_cond_wait(smokersReady);
      uthread_mutex_unlock(smokerlock);
    }
  }
  return NULL;
}

struct smoker {
  enum Resource r;
  struct Agent* agent;
};

struct smoker* createSmoker(enum Resource rs, struct Agent* a) {
  struct smoker* smoker = malloc(sizeof(struct smoker));
  smoker->r = rs;
  smoker->agent = a;
  return smoker;
}

void* trySmoke (void* x) {
  struct smoker* s = x;
  enum Resource rs = s->r;
  uthread_mutex_t m = s->agent->mutex;
  uthread_cond_t smoke = s->agent->smoke;

  while (1) {
  uthread_mutex_lock(smokerlock);
  uthread_cond_signal(smokersReady);
  if (rs == MATCH) {
    smokerThreadsReady++;
    uthread_cond_wait(smokerM);
    smoke_count[MATCH]++;
    smokerThreadsReady--;
    uthread_mutex_unlock(smokerlock);
    uthread_mutex_lock(m);
    uthread_cond_signal(smoke);
    uthread_mutex_unlock(m);
  }
  else if (rs == PAPER) {
    smokerThreadsReady++;
    uthread_cond_wait(smokerP);
    smoke_count[PAPER]++;
    smokerThreadsReady--;
    uthread_mutex_unlock(smokerlock);
    uthread_mutex_lock(m);
    uthread_cond_signal(smoke);
    uthread_mutex_unlock(m);
  }
  else if (rs == TOBACCO) {
    smokerThreadsReady++;
    uthread_cond_wait(smokerT);
    smoke_count[TOBACCO]++;
    smokerThreadsReady--;
    uthread_mutex_unlock(smokerlock);
    uthread_mutex_lock(m);
    uthread_cond_signal(smoke);
    uthread_mutex_unlock(m);
  }
}
  return NULL;
}

/**
 * This is the agent procedure.  It is complete and you shouldn't change it in
 * any material way.  You can re-write it if you like, but be sure that all it does
 * is choose 2 random reasources, signal their condition variables, and then wait
 * wait for a smoker to smoke.
 */
void* agent (void* av) {
  struct Agent* a = av;
  static const int choices[]         = {MATCH|PAPER, MATCH|TOBACCO, PAPER|TOBACCO};
  static const int matching_smoker[] = {TOBACCO,     PAPER,         MATCH};

  srandom(time(NULL));
  
  uthread_mutex_lock (a->mutex);

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int r = random() % 3;
    signal_count [matching_smoker [r]] ++;
    int c = choices [r];
    if (c & MATCH) {
      VERBOSE_PRINT ("match available\n");
      uthread_cond_signal (a->match);
    }
    if (c & PAPER) {
      VERBOSE_PRINT ("paper available\n");
      uthread_cond_signal (a->paper);
    }
    if (c & TOBACCO) {
      VERBOSE_PRINT ("tobacco available\n");
      uthread_cond_signal (a->tobacco);
    }
    VERBOSE_PRINT ("agent is waiting for smoker to smoke\n");
    uthread_cond_wait (a->smoke);
  }
  
  uthread_mutex_unlock (a->mutex);
  return NULL;
}

int main (int argc, char** argv) {
  uthread_init (8);
  //create thread arrays
  uthread_t handlers[3];
  uthread_t smokers[3];

  struct Agent*  a = createAgent();
  //initiate handlerlock and its cond vars
  handlerlock = uthread_mutex_create();
  middleTReady = uthread_cond_create(handlerlock);
  sigUpdate = uthread_cond_create(handlerlock);
  //initiate smokerlock and its cond vars
  smokerlock = uthread_mutex_create();
  smokerM = uthread_cond_create(smokerlock);
  smokerP = uthread_cond_create(smokerlock);
  smokerT = uthread_cond_create(smokerlock);
  smokersReady = uthread_cond_create(smokerlock);
  //create smoker structs and array
  struct smoker* SM = createSmoker(MATCH, a);
  struct smoker* SP = createSmoker(PAPER, a);
  struct smoker* ST = createSmoker(TOBACCO, a);
  struct smoker* smkr[3] = {SM, SP, ST};
  //create signal handler structs and array
  struct SignalHandler* SGHM = CreateSGHandler(MATCH, a);
  struct SignalHandler* SGHP = CreateSGHandler(PAPER, a);
  struct SignalHandler* SGHT = CreateSGHandler(TOBACCO, a);
  struct SignalHandler* hd[3] = {SGHM, SGHP, SGHT};
  //create handler and smoker threads
  for (int i=0; i < 3; i++) {
    handlers[i] = uthread_create(handle, hd[i]);
    smokers[i] = uthread_create(trySmoke, smkr[i]);
  }
  //
  uthread_mutex_lock(smokerlock);
  while (smokerThreadsReady < 3) {
    uthread_cond_wait(smokersReady);
  }
  uthread_mutex_unlock(smokerlock);
  uthread_t fs = uthread_create(findSmoker, a);
  //
  uthread_mutex_lock(handlerlock);
  while (middleThreadsReady < 3) {
    uthread_cond_wait(middleTReady);
  }
  uthread_mutex_unlock(handlerlock);
  //create agent
  uthread_join (uthread_create (agent, a), 0);
  //
  for (int i = 0; i < 3; i++) {
    uthread_detach(handlers[i]);
    uthread_detach(smokers[i]);
  }
  uthread_detach(fs);
  free(a);
  uthread_cond_destroy(middleTReady);
  uthread_cond_destroy(sigUpdate);
  uthread_cond_destroy(smokerM);
  uthread_cond_destroy(smokerP);
  uthread_cond_destroy(smokerT);
  uthread_cond_destroy(smokersReady);
  free(SM);
  free(SP);
  free(ST);
  free(SGHM);
  free(SGHP);
  free(SGHT);
  uthread_mutex_destroy(handlerlock);
  uthread_mutex_destroy(smokerlock);


  assert (signal_count [MATCH]   == smoke_count [MATCH]);
  assert (signal_count [PAPER]   == smoke_count [PAPER]);
  assert (signal_count [TOBACCO] == smoke_count [TOBACCO]);
  assert (smoke_count [MATCH] + smoke_count [PAPER] + smoke_count [TOBACCO] == NUM_ITERATIONS);
  printf ("Signal counts: %d matches, %d paper, %d tobacco\n",
          signal_count [MATCH], signal_count [PAPER], signal_count [TOBACCO]);
  printf ("Smoke counts: %d matches, %d paper, %d tobacco\n",
          smoke_count [MATCH], smoke_count [PAPER], smoke_count [TOBACCO]);
}
