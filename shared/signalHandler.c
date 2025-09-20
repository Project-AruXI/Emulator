#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

#include "signalHandler.h"


handler_t* redefineSignal(int signum, handler_t* handler) {
	struct sigaction action, prevAction;

	action.sa_handler = handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;

	sigaction(signum, &action, &prevAction);

	return prevAction.sa_handler;
}