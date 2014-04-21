/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file   fuzzer.c
 * @brief  ROHC fuzzer program
 * @author Didier Barvaux <didier@barvaux.org>
 * @author Yura
 *
 * Stress test the ROHC decompressor to discover bugs.
 */

/* system includes */
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

/* ROHC includes */
#include <rohc.h>
#include <rohc_decomp.h>


/** The maximum size of IP and ROHC packets */
#define PACKET_MAX_SIZE 2048


/* prototypes of private functions */
static void usage(void);
static void print_rohc_traces(const rohc_trace_level_t level,
                              const rohc_trace_entity_t entity,
                              const int profile,
                              const char *const format,
                              ...)
	__attribute__((format(printf, 4, 5), nonnull(4)));


/** The maximum number of traces to keep */
#define MAX_LAST_TRACES  5000
/** The maximum length of a trace */
#define MAX_TRACE_LEN  300

/** The ring buffer for the last traces */
static char last_traces[MAX_LAST_TRACES][MAX_LAST_TRACES + 1];
/** The index of the first trace */
static int last_traces_first;
/** The index of the last trace */
static int last_traces_last;


/**
 * @brief Main function for the ROHC fuzzer application
 *
 * @param argc The number of program arguments
 * @param argv The program arguments
 * @return     The unix return code:
 *              \li 0 in case of success,
 *              \li 1 in case of failure
 */
int main(int argc, char *argv[])
{
	unsigned int rand_seed;
	struct rohc_decomp *decomp;
	const unsigned long max_iter = 2000000000;
	unsigned long cur_iter;
	int i;

	/* no traces at the moment */
	for(i = 0; i < MAX_LAST_TRACES; i++)
	{
		last_traces[i][0] = '\0';
	}
	last_traces_first = -1;
	last_traces_last = -1;

	/* parse arguments and check consistency */
	if(argc != 2 && argc != 3)
	{
		fprintf(stderr, "wrong number of arguments\n");
		usage();
		goto error;
	}
	else if(strcmp(argv[1], "play") == 0)
	{
		/* 'play' command: no additional argument allowed */
		if(argc != 2)
		{
			fprintf(stderr, "play command does not take any argument\n");
			usage();
			goto error;
		}

		/* 'play' command: choose a random seed */
		rand_seed = time(NULL);
	}
	else if(strcmp(argv[1], "replay") == 0)
	{
		if(argc != 3)
		{
			fprintf(stderr, "replay command takes one argument\n");
			usage();
			goto error;
		}

		/* 'replay' command: take random seed given as argument */
		rand_seed = atoi(argv[2]);
	}
	else
	{
		fprintf(stderr, "unrecognized command '%s'\n", argv[1]);
		usage();
		goto error;
	}

	printf("start fuzzing session with random seed %u\n", rand_seed);
	printf("you can use the replay command and the above random seed to run\n"
	       "the same fuzzing session again\n\n");
	srand(rand_seed);

	/* create ROHC decompressor */
	decomp = rohc_alloc_decompressor(NULL);
	assert(decomp != NULL);

	/* set the callback for traces on ROHC decompressor */
	assert(rohc_decomp_set_traces_cb(decomp, print_rohc_traces));

	/* decompress many random packets in a row */
	for(cur_iter = 1; cur_iter <= max_iter; cur_iter++)
	{
		unsigned char rohc_packet[PACKET_MAX_SIZE];
		int rohc_len;
		unsigned char ip_packet[PACKET_MAX_SIZE];
		int i;

		/* print progress from time to time */
		if(cur_iter == 1 || (cur_iter % 10000) == 0)
		{
			if(cur_iter > 1)
			{
				printf("\r");
			}
			printf("iteration %lu / %lu", cur_iter, max_iter);
			fflush(stdout);
		}

		/* create one crazy ROHC packet */
		rohc_len = rand() % PACKET_MAX_SIZE;
		for(i = 0; i < rohc_len; i++)
		{
			rohc_packet[i] = rand() % 0xff;
		}

		/* decompress the crazy ROHC packet */
		rohc_decompress(decomp,
		                rohc_packet, rohc_len,
		                ip_packet, PACKET_MAX_SIZE);
		/* do not check for result, only robustess is checked */
	}

	printf("\nTEST OK\n");

	rohc_free_decompressor(decomp);
	return 0;

error:
	return 1;
}


/**
 * @brief Print usage of the fuzzer application
 */
static void usage(void)
{
	printf("ROHC fuzzer tool: test the ROHC library robustness\n"
	       "\n"
	       "usage: rohc_fuzzer COMMAND\n"
	       "\n"
	       "available commands:\n"
	       "  play                Run a test\n"
	       "  replay SEED         Run a specific test (to reproduce bugs)\n");
}


/**
 * @brief Callback to print traces of the ROHC library
 *
 * @param level    The priority level of the trace
 * @param entity   The entity that emitted the trace among:
 *                  \li ROHC_TRACE_COMP
 *                  \li ROHC_TRACE_DECOMP
 * @param profile  The ID of the ROHC compression/decompression profile
 *                 the trace is related to
 * @param format   The format string of the trace
 */
static void print_rohc_traces(const rohc_trace_level_t level,
                              const rohc_trace_entity_t entity,
                              const int profile,
                              const char *format, ...)
{
	va_list args;

	if(last_traces_last == -1)
	{
		last_traces_last = 0;
	}
	else
	{
		last_traces_last = (last_traces_last + 1) % MAX_LAST_TRACES;
	}

	va_start(args, format);
	vsnprintf(last_traces[last_traces_last], MAX_TRACE_LEN + 1, format, args);
	/* TODO: check return code */
	last_traces[last_traces_last][MAX_TRACE_LEN] = '\0';
	va_end(args);

	if(last_traces_first == -1)
	{
		last_traces_first = 0;
	}
	else if(last_traces_first == last_traces_last)
	{
		last_traces_first = (last_traces_first + 1) % MAX_LAST_TRACES;
	}
}

