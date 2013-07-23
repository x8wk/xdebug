/*
	ZomPHP extension for Xdebug
	Author: Jean Rouge (jer329@cornell.edu)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <unistd.h>

#include "xdebug_zomphp.h"

// only used for debugging - not thread/process-safe!
#define ZOMPHP_DEBUG_MODE 0 // TODO wkpo
#if ZOMPHP_DEBUG_MODE

#define ZOMPHP_DEBUG_LOG_FILE "/tmp/zomphp_debug.log"

void zomphp_debug(const char *format, ...)
{
	FILE *f ;
	va_list args;
	struct timeval tv;
	f = fopen(ZOMPHP_DEBUG_LOG_FILE, "a+");
	gettimeofday(&tv, NULL);
	fprintf(f, "[ %lu-%lu (%d)] ", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec, (int) getpid());
	va_start(args, format);
	vfprintf(f, format, args);
	fprintf(f, "\n");
	va_end(args);
	fclose(f);
}

#define ZOMPHP_DEBUG(format, ...) zomphp_debug(format, ##__VA_ARGS__)

#else

#define ZOMPHP_DEBUG(format, ...)

#endif

#define FLUSH_DELAY 2 // the # of seconds between two automatic flushes // TODO wkpo 300

// {{{ STRING_LIST }}}

string_list* new_string_list()
{
	string_list* result;
	result = (string_list*) malloc(sizeof(string_list));
	if (!result) {
		return NULL;
	}
	result->head = NULL;
	result->tail = NULL;
	return result;
}

void free_and_process_string_list(string_list* sl, process_string func)
{
	string_list_el *current, *next;
	if (sl) {
		next = sl->head;
		while (next) {
			current = next;
			next = current->next;
			if (func) {
				func(current->data);
			}
			free(current->data);
			free(current);
		}
		free(sl);
	}
}

void free_string_list(string_list* sl)
{
	free_and_process_string_list(sl, NULL);
}

void add_string_to_string_list(string_list* sl, const char* s)
{
	string_list_el* new_el;
	char* data;
	if (!sl || !s) {
		return;
	}
	data = strdup(s);
	if (!data) {
		return;
	}
	new_el = (string_list_el*) malloc(sizeof(string_list_el));
	if (!new_el) {
		free(data);
		return;
	}
	new_el->data = data;
	new_el->next = NULL;
	if (!sl->head) {
		sl->head = new_el;
		sl->tail = new_el;
	} else {
		sl->tail->next = new_el;
		sl->tail = new_el;
	}
}

// {{{ END OF STRING_LIST }}}


// {{{ ZOMPHP_EXTENSIBLE_STRING }}}

#define ZOMPHP_EXTENSIBLE_STRING_DELTA_INCR 100

zomphp_extensible_string* zomphp_extensible_strcat(zomphp_extensible_string* ext_string, const int nb_strings, ...)
{
	va_list args;
	char *current_string, *buffer;
	size_t current_pos, current_length;
	int i;

	if (!ext_string) {
		return NULL;
	}

	va_start(args, nb_strings);
	current_pos = 0;
	for (i = 0; i != nb_strings; i++) {
		current_string = va_arg(args, char*);
		current_length = strlen(current_string);
		if (current_pos + current_length >= ext_string->current_length) {
			// we need to make the container bigger!
			ext_string->current_length += current_length > ZOMPHP_EXTENSIBLE_STRING_DELTA_INCR ? current_length : ZOMPHP_EXTENSIBLE_STRING_DELTA_INCR;
			buffer = (char*) malloc(sizeof(char) * ext_string->current_length);
			if (!buffer) {
				// not enough RAM, we're done with that thing
				free_zomphp_extensible_string(ext_string);
				return NULL;
			}
			// copy everything up to that point
			memcpy(buffer, ext_string->data, sizeof(char) * current_pos);
			// and make that the new data
			free(ext_string->data);
			ext_string->data = buffer;
		}
		memcpy(ext_string->data + current_pos, current_string, current_length * sizeof(char));
		current_pos += current_length;
	}
	memcpy(ext_string->data + current_pos, "\0", sizeof(char));
	va_end(args);

	return ext_string;
}

zomphp_extensible_string* new_zomphp_extensible_string()
{
	zomphp_extensible_string* result;
	char* data;

	result = (zomphp_extensible_string*) malloc(sizeof(zomphp_extensible_string));
	data = (char*) malloc(sizeof(char) * ZOMPHP_EXTENSIBLE_STRING_DELTA_INCR);

	if (!result || !data) {
		if (result) {
			free(result);
		}
		return NULL;
	}

	result->data = data;
	result->current_length = ZOMPHP_EXTENSIBLE_STRING_DELTA_INCR;
	return result;
}

void free_zomphp_extensible_string(zomphp_extensible_string* ext_string)
{
	if (ext_string) {
		free(ext_string->data);
		free(ext_string);
	}
}

// {{{ END OF ZOMPHP_EXTENSIBLE_STRING }}}


// {{{ ZOMPHP_DATA }}}

void zomphp_file_hash_el_dtor(void* data)
{
	zomphp_file_hash_el *file;
	if (!data) {
		return;
	}
	file = (zomphp_file_hash_el*) data;
	xdebug_hash_destroy(file->functions);
	free(file->name);
	free(file);
}

void zomphp_function_hash_el_dtor(void* data)
{
	zomphp_function_hash_el* function;
	if (!data) {
		return;
	}
	function = (zomphp_function_hash_el*) data;
	if (function->linenos) {
		free(function->linenos);
	}
	free(function->name);
	free(function);
}

zomphp_data* new_zomphp_data()
{
	zomphp_data* result;
	result = (zomphp_data*) malloc(sizeof(zomphp_data));
	if (!result) {
		return NULL;
	}
	result->files = xdebug_hash_alloc(32768, zomphp_file_hash_el_dtor);
	result->new_data = new_string_list();
	result->buffer = new_zomphp_extensible_string();
	result->last_flush = (struct timeval*) malloc(sizeof(struct timeval));
	if (!result->files || !result->new_data || !result->buffer || !result->last_flush) {
		free_zomphp_data(result);
		return NULL;
	}
	gettimeofday(result->last_flush, NULL);
	result->next_func_name = NULL;
	result->last_file = NULL;
	result->last_func = NULL;
	result->last_line = 0;
	return result;
}

void free_zomphp_data(zomphp_data* zd)
{
	if (zd) {
		if (zd->files) {
			xdebug_hash_destroy(zd->files);
		}
		if (zd->last_flush) {
			free(zd->last_flush);
		}
		if (zd->next_func_name) {
			free(zd->next_func_name);
		}
		free_string_list(zd->new_data);
		free_zomphp_extensible_string(zd->buffer);
		free(zd);
	}
}

void report_item_to_daemon(const char* s)
{
	ZOMPHP_DEBUG("Reporting to daemon! %s", s);
	// TODO wkpo
}

void flush_zomphp(zomphp_data* zd) // TODO wkpo fork ?
{
	if (zd) {
		ZOMPHP_DEBUG("Flushing!");
		free_and_process_string_list(zd->new_data, report_item_to_daemon);
		zd->new_data = new_string_list();
		gettimeofday(zd->last_flush, NULL);
	}
}

void flush_zomphp_automatic(zomphp_data* zd)
{
	if (!zd) {
		return;
	}
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (tv.tv_sec - zd->last_flush->tv_sec > FLUSH_DELAY) {
		ZOMPHP_DEBUG("Flushing! It's been %lu secs since last time (vs %lu allowed)", (unsigned long)(tv.tv_sec - zd->last_flush->tv_sec), FLUSH_DELAY);
		flush_zomphp(zd);
	} else {
		ZOMPHP_DEBUG("No need to flush, it's been only %lu secs (vs %lu allowed)", (unsigned long)(tv.tv_sec - zd->last_flush->tv_sec), FLUSH_DELAY);
	}
}

#define MAX_LINE_NB INT_MAX
#define MAX_LINE_NB_LENGTH ((int) (ceil(log10(MAX_LINE_NB)) + 2))
#define ITEM_DELIMITER "\n"
#define INTRA_DELIMITER ":"

void zomphp_register_function_call(zomphp_data* zd, char* filename, char* funcname, int lineno)
{
	char same_so_far, is_new;
	char lineno_buffer[MAX_LINE_NB_LENGTH];
	int i, *new_linenos;
	zomphp_file_hash_el* file;
	zomphp_function_hash_el* func;

	ZOMPHP_DEBUG("Processing new func call: %s:%s:%d", filename, funcname, lineno);

	if (!zd || !filename || !funcname || !lineno) {
		return;
	}

	if (zd->last_file && !strcmp(zd->last_file->name, filename)) {
		same_so_far = 1;
		file = zd->last_file;
	} else {
		same_so_far = 0;
		if (!xdebug_hash_find(zd->files, filename, strlen(filename), (void*) &file)) {
			file = malloc(sizeof(zomphp_file_hash_el));
			file->name = strdup(filename);
			file->functions = xdebug_hash_alloc(32, zomphp_function_hash_el_dtor);
			xdebug_hash_add(zd->files, filename, strlen(filename), file);
		}
		zd->last_file = file;
	}

	if (same_so_far && zd->last_func && !strcmp(zd->last_func->name, funcname)) {
		func = zd->last_func;
	} else {
		same_so_far = 0;
		if (!xdebug_hash_find(file->functions, funcname, strlen(funcname), (void*) &func)) {
			func = malloc(sizeof(zomphp_function_hash_el));
			func->name = strdup(funcname);
			func->linenos = NULL;
			func->n = lineno;
			xdebug_hash_add(file->functions, funcname, strlen(funcname), func);
		}
		zd->last_func = func;
	}

	if (same_so_far && zd->last_line && zd->last_line == lineno) {
		// it's all the same, nothing to do
		ZOMPHP_DEBUG("Cache hit for %s:%s:%d", filename, funcname, lineno);
		return;
	}

	// either it's a multiple lineno, or we don't know that one yet
	is_new = 1;
	if (func->n > 0) {
		if (func->n == lineno) {
			is_new = 0;
		} else {
			// we need to switch to a multi lineno
			func->linenos = (int*) malloc(sizeof(int) * 2);
			func->linenos[0] = func->n;
			func->linenos[1] = lineno;
			func->n = -2;
		}
	} else {
		// it's already a multi, let's see if we already have that line no
		for (i = 0; i < -func->n; i++) {
			if (func->linenos[i] == lineno) {
				is_new = 0;
				break;
			}
			if (is_new) {
				// we need to enlarge the array
				new_linenos = (int*) malloc(sizeof(int) * (-func->n + 1));
				memcpy(new_linenos, func->linenos, -sizeof(int) * func->n);
				new_linenos[-func->n] = lineno;
				free(func->linenos);
				func->linenos = new_linenos;
				func->n--;
			}
		}
	}
	zd->last_line = lineno;

	if (is_new) {
		// build the new string to be pushed, and append it to the list
		// we need to convert the line nb to a string
		sprintf(lineno_buffer, "%d", lineno);
		zd->buffer = zomphp_extensible_strcat(zd->buffer, 6, filename, INTRA_DELIMITER, funcname, INTRA_DELIMITER, lineno, ITEM_DELIMITER);
		if (zd->new_data && zd->buffer) {
			add_string_to_string_list(zd->new_data, zd->buffer->data);
		}
		ZOMPHP_DEBUG("That func call is new! %s:%s:%d", filename, funcname, lineno);
	} else {
		ZOMPHP_DEBUG("That func call is nothing new. %s:%s:%d", filename, funcname, lineno);
	}
}

void set_next_func_name(zomphp_data* zd, const char* funcname)
{
	if (zd) {
		if (zd->next_func_name) {
			// shouldn't happen, but let's avoid mem leaks...
			free(zd->next_func_name);
		}
		zd->next_func_name = strdup(funcname);
	}
}

void zomphp_register_line_call(zomphp_data* zd, char* filename, int lneno)
{
	if (zd && zd->next_func_name) {
		zomphp_register_function_call(zd, filename, zd->next_func_name, lneno);
		free(zd->next_func_name);
		zd->next_func_name = NULL;
	}
}

// {{{ END OF ZOMPHP_DATA }}}
