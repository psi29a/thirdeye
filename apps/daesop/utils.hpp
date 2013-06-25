#ifndef UTILS_H
#define UTILS_H

#define MAX_COPY_BUFFER 10000

char *makeString(const char *aString);
char *unpackDate(unsigned int aDate, char *aDateString);
int strcmpCS(const char *s1, const char *s2);
void toUpperCase(char *aS);
char getCharacterForDump(char aChar);
int stringEndsWith(char *aFullString, const char *aEndString);
int copyFile(FILE *aSourceFile, char *aNewFileName);

#endif

