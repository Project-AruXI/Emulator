#include <stdlib.h>
#include <string.h>

#include "environment.h"
#include "diagnostics.h"


static EnvVar predefinedEnv[] = {
	{ "PATH", "." },
	{ "shell", "ash" },
	{ NULL, NULL }
};

extern EnvVar* shellEnv;

void loadEnv() {
	shellEnv = malloc(sizeof(EnvVar) * 5);
	if (!shellEnv) dFatal(D_ERR_MEM, "Could not allocate memory for environmental variables!");

	EnvVar* var;
	size_t predefinedLen = sizeof(predefinedEnv) / sizeof(predefinedEnv[0]);
	for (size_t i = 0; i < predefinedLen-1; i++) {
		var = &shellEnv[i];

		var->key = malloc(sizeof(char) * strlen(predefinedEnv[i].key) + 1);
		if (!var->key) dFatal(D_ERR_MEM, "Could not allocate memory for variable key!");
		strcpy(var->key, predefinedEnv[i].key);

		var->value = malloc(sizeof(char) * strlen(predefinedEnv[i].value) + 1);
		if (!var->value) dFatal(D_ERR_MEM, "Could not allocate memory for variable value!");
		strcpy(var->value, predefinedEnv[i].value);
	}

	var = &shellEnv[predefinedLen-1]; // Get last element
	var->key = NULL;
	var->value = NULL;
}

void deleteEnv() {
	int i = 0;
	EnvVar* var = &shellEnv[i];
	while (var->key && var->value) {
		free(var->key);
		free(var->value);
		i++;
		var = &shellEnv[i];
	}
}

EnvVar* getEnv(const char* key) {
	int i = 0;
	EnvVar* var = &shellEnv[i];
	while (var->key && var->value) {
		if (strcmp(var->key, key) == 0) return var;
		i++;
		var = &shellEnv[i];
	}

	return NULL;
}

void setEnv(const char* key, char* value) {
	EnvVar* env = getEnv(key);

	if (!env) {
		// No existing entry, create new one
		// TODO
		// int i = 0;
		// EnvVar* temp = &shellEnv[i];

		// while (temp->key && temp->value) { i++; temp = &shellEnv[i]; }


	} else {
		// Update entry
		char* temp = malloc(sizeof(char) * strlen(value) + 1);
		if (!temp) dFatal(D_ERR_MEM, "Could not allocate memory for new value!");
		env->value = temp;

		strcpy(env->value, value);
	}
}

void appendPath(const char* path) {
	EnvVar* pathEnv = getEnv("PATH");

	size_t currPathLen = strlen(pathEnv->value);
	size_t pathLen = strlen(path);

	char* temp = realloc(pathEnv->value, currPathLen+pathLen+1);
	if (!temp) dFatal(D_ERR_MEM, "Could not reallocate memory for new path!");
	pathEnv->value = temp;

	if (*(pathEnv->value + currPathLen - 1) != ':') {
		strcat(pathEnv->value, ":");
	}
	strcat(pathEnv->value, path);
}

void printEnv() {
	dLog(D_NONE, DSEV_WARN, "printEnv() NOT IMPLEMENTED!");
}

char* pathtok(char* _path, char** save) {
	if (!save) return NULL;
	
	const char delim = ':';
	
	char* path = NULL;
	if (!_path) {
		if (!*save) return NULL;
		path = *save;
	} else path = _path;

	char* pathtok = path;
	char* temp = pathtok;

	if (!*path) return NULL;

	// Restore when it has been modified
	if (*save && *(path-1) == '\0') *(path-1) = delim;

	while (*temp != '\0' && *temp != delim) {
		temp++;
	}

	// At delim
	if (*temp == '\0') {
		// end of path string
		*save = NULL;
		return pathtok;
	}

	*temp = '\0';
	*save = temp+1;

	return pathtok;
}