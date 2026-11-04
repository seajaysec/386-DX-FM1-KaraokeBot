/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
const void *es_data(const char *,int *);
void *es_malloc(size_t);
void *es_calloc(size_t,size_t);
void *es_realloc(void *,size_t);
void es_free(void *);
FILE *es_fopen(const char *,const char *);
int es_fclose(FILE *);
char *es_fgets(char *,int,FILE *);
size_t es_fread(void *,size_t,size_t,FILE *);
int es_fseek(FILE *,long,int);
long es_ftell(FILE *);
int es_fgetc(FILE *);
int es_feof(FILE *);
int es_fprintf(FILE *,const char *,...);
int es_printf(const char *,...);
extern unsigned es_pitch_q16;
extern int dx_sing_active,dx_sing_measure,dx_sing_adjust,dx_sing_body;
extern unsigned char *dx_sing_buffer;
void dx_sing_select(void);
int dx_sing_f0(void);
#define malloc es_malloc
#define calloc es_calloc
#define realloc es_realloc
#define free es_free
#define fopen es_fopen
#define fclose es_fclose
#define fgets es_fgets
#define fread es_fread
#define fseek es_fseek
#define ftell es_ftell
#define fgetc es_fgetc
#define feof es_feof
#define fprintf es_fprintf
#define printf es_printf
int es_sprintf(char *,const char *,...);
int es_snprintf(char *,size_t,const char *,...);
int es_sscanf(const char *,const char *,...);
#define sprintf es_sprintf
#define snprintf es_snprintf
#define sscanf es_sscanf

#ifdef DX_HOST
#define ES_POOL
#else
#define ES_POOL __attribute__((section(".pool")))
#endif
char *es_strdup(const char *);
#define strdup es_strdup
double es_sin(double);
double es_cos(double);
#define sin es_sin
#define cos es_cos
double es_exp(double);
#define exp es_exp
long es_strtol(const char *,char **,int);
int es_atoi(const char *);
char *es_strtok(char *,const char *);
#define strtol es_strtol
#define atoi es_atoi
#define strtok es_strtok
void es_qsort(void *,size_t,size_t,int (*)(const void *,const void *));
#define qsort es_qsort
/* the C locale's classes for the few the library asks, without the C library's 640-byte table */
#include <ctype.h>
static inline int es_isdigit(int c) { return (unsigned)c - '0' < 10u; }
static inline int es_isalpha(int c) { return (unsigned)(c | 32) - 'a' < 26u; }
static inline int es_isalnum(int c) { return es_isalpha(c) || es_isdigit(c); }
static inline int es_isspace(int c) { return c == ' ' || (unsigned)c - '\t' < 5u; }
static inline int es_tolower(int c) { return (unsigned)c - 'A' < 26u ? c + 32 : c; }
#undef isdigit
#undef isalpha
#undef isalnum
#undef isspace
#undef tolower
#define isdigit es_isdigit
#define isalpha es_isalpha
#define isalnum es_isalnum
#define isspace es_isspace
#define tolower es_tolower
#include <wctype.h>
static inline int es_iswalpha(wint_t c) { return c < 128u && es_isalpha((int)c); }
static inline int es_iswdigit(wint_t c) { return c - '0' < 10u; }
static inline int es_iswalnum(wint_t c) { return es_iswalpha(c) || es_iswdigit(c); }
static inline int es_iswlower(wint_t c) { return c - 'a' < 26u; }
static inline int es_iswupper(wint_t c) { return c - 'A' < 26u; }
static inline int es_iswspace(wint_t c) { return c < 128u && es_isspace((int)c); }
static inline int es_iswpunct(wint_t c) { return c - 33u < 94u && !es_iswalnum(c); }
#undef iswalpha
#undef iswdigit
#undef iswalnum
#undef iswlower
#undef iswupper
#undef iswspace
#undef iswpunct
#define iswalpha es_iswalpha
#define iswdigit es_iswdigit
#define iswalnum es_iswalnum
#define iswlower es_iswlower
#define iswupper es_iswupper
#define iswspace es_iswspace
#define iswpunct es_iswpunct
#include <string.h>
#include <strings.h>
static inline int es_strcasecmp(const char *a, const char *b)
{
    int x, y;
    do {
        x = es_tolower((unsigned char)*a++);
        y = es_tolower((unsigned char)*b++);
    } while (x && x == y);
    return x - y;
}
#define strcasecmp es_strcasecmp
#ifndef DX_HOST
#undef stderr
#define stderr ((FILE *)0)
#undef stdout
#define stdout ((FILE *)0)
#endif

extern int dx_sing_style;
