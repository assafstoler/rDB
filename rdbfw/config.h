#ifndef _CONFIG_H_
#define _CONFIG_H_

// Below are possible defines to control various compile time components.
// they are all commented out, as this demo defines them via cmake 
// (CmakeLists.txt) build definition file.
// This is to enable easily building multiple demos in various configurations.

// Messagig model - choose one of:
//
// Common messaging pool is simpler, and does present some performace costs due
// to locking associated with access to a common resource.
//
// #define COMMON_MESSAGING_POOL
//
// Individual messaging pools require no locks, thus are faster, at the cost
// of added tables (code complexity / reduced readbiity to untrained eye)
//
// #define INDIVIDUAL_MESSAGING_POOL

// Available syncronization modes.
#define MODE_FORK 1
#define MODE_PTHERAD_SOCKET 2
#define MODE_PTHREAD_MUTEX 3
#define MODE_PTHREAD_COND_WAIT 4
//
#define LOOP_COUNT 100000
#define PORT 5003


#endif
