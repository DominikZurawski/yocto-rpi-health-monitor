#ifndef SENTINEL_PARSE_H
#define SENTINEL_PARSE_H

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Czyta wiersz do bufora o ustalonej pojemności, bez znaku nowej linii.
 * Zwraca 1 dla wiersza, 0 na końcu pliku lub ujemny kod błędu.
 */
static inline int read_line(FILE *file, char *line, size_t capacity)
{
    size_t length = 0;
    int ch;

    while ((ch = fgetc(file)) != EOF && ch != '\n') {
        if (ch == '\0') {
            return -EINVAL;
        }
        if (length + 1 == capacity) {
            return -EOVERFLOW;
        }
        line[length++] = (char)ch;
    }
    if (ferror(file)) {
        return -EIO;
    }

    line[length] = '\0';
    return length != 0 || ch == '\n';
}

/* Zwraca wskaźnik do pierwszego znaku po białych znakach lub do końca tekstu. */
static inline char *skip_space(char *text)
{
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    return text;
}

/* Usuwa końcowe białe znaki w miejscu i zwraca początek tekstu
 * po pominięciu białych znaków z przodu.
 */
static inline char *trim(char *text)
{
    text = skip_space(text);
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

/* Czyta całkowitą liczbę dziesiętną w podanym zakresie. Zwraca
 * EINVAL przy złym formacie lub ERANGE przy przekroczeniu zakresu.
 */
static inline int parse_integer(char *text, int min, int max, int *value)
{
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (text == end || *skip_space(end) != '\0') {
        return EINVAL;
    }
    if (errno == ERANGE || parsed < min || parsed > max) {
        return ERANGE;
    }
    *value = (int)parsed;
    return 0;
}

#endif
