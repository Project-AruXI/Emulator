#ifndef _SHELL_PARSER_H_
#define _SHELL_PARSER_H_

typedef struct CommandType {
	char* command;
	char** argv;
	int argc;
} Command;

Command parseCommand(char* commandString);

#endif