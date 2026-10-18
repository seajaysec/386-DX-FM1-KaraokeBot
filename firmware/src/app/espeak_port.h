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

extern int dx_sing_style;
