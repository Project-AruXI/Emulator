#ifndef _SHELL_ENV_H_
#define _SHELL_ENV_H_

typedef struct EnvironmentalVariable {
	char* key;
	char* value;
} EnvVar;

void loadEnv();
void deleteEnv();

EnvVar* getEnv(const char* key);
void setEnv(const char* key, char* value);

/**
 * Appends the provided path to the current path.
 * @param path
 */
void appendPath(const char* path);

void printEnv();

char* pathtok(char* path, char** save);

#endif