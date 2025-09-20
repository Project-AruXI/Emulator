#ifndef _SIGNAL_HANDLER_H_
#define _SIGNAL_HANDLER_H_

#include <signal.h>


typedef void handler_t(int);

handler_t* redefineSignal(int signum, handler_t* handler);

#endif