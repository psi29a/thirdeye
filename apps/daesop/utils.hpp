#ifndef UTILS_H
#define UTILS_H

#include "tdefs.hpp"

#define MAX_COPY_BUFFER 10000

char	*makeString(char *aString);
char 	*unpackDate(ULONG aDate, char *aDateString);
int 	strcmpCS(const char *s1, const char *s2);
void 	toUpperCase(char *aS);
char 	getCharacterForDump(char aChar);
int 	stringEndsWith(char *aFullString, char *aEndString);
int 	copyFile(FILE *aSourceFile, char *aNewFileName);

#endif

