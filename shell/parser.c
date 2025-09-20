#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "diagnostics.h"


Command parseCommand(char* commandString) {
	char** tokens = malloc(sizeof(char*) * 8);
	if (!tokens) dFatal(D_ERR_MEM, "Could not allocate memory for command tokens!");

	char* command = strtok(commandString, " \t\n");
	if (!command) {
		Command ret = {.argc = 0, .argv = NULL, .command = NULL};
		return ret;
	}

	int i = 0;
	for (; i < 5; i++) {
		char* tok = strtok(NULL, " \t\n");
		tokens[i] = tok;

		if (!tok) break;
	}


	Command ret = {
		.command = command,
		.argc = i,
		.argv = tokens
	};

	return ret;
}