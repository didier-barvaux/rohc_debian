/*
 * Copyright 2012,2013,2014,2015 Didier Barvaux
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file   test_malformed_rohc_packets.c
 * @brief  Test the decompression of malformed ROHC packets
 * @author Didier Barvaux <didier@barvaux.org>
 */

#include "config.h" /* for HAVE_*_H */
#include "test.h"

/* system includes */
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <stdarg.h>

/* include for the PCAP library */
#if HAVE_PCAP_PCAP_H == 1
#  include <pcap/pcap.h>
#elif HAVE_PCAP_H == 1
#  include <pcap.h>
#else
#  error "pcap.h header not found, did you specified --enable-rohc-tests \
for ./configure ? If yes, check configure output and config.log"
#endif

/* ROHC includes */
#include <rohc.h>
#include <rohc_decomp.h>


/* prototypes of private functions */
static void usage(void);
static int test_decomp(const char *const filename,
                       const size_t failure_start,
                       const rohc_cid_type_t cid_type,
                       const rohc_cid_t cid_max,
                       const bool ignore_malformed)
	__attribute__((warn_unused_result, nonnull(1)));

static void print_rohc_traces(void *const priv_ctxt,
                              const rohc_trace_level_t level,
                              const rohc_trace_entity_t entity,
                              const int profile,
                              const char *const format,
                              ...)
	__attribute__((format(printf, 5, 6), nonnull(5)));


/** Whether to be verbose or not */
static bool is_verbose = false;


/**
 * @brief Main function for the ROHC test program
 *
 * @param argc The number of program arguments
 * @param argv The program arguments
 * @return     The unix return code:
 *              \li 0 in case of success,
 *              \li 1 in case of failure,
 *              \li 77 in case test is skipped
 */
int main(int argc, char *argv[])
{
	char *filename = NULL;
	int status = 1;
	int args_used;
	bool ignore_malformed = false;
	const char *cid_type_str = NULL;
	rohc_cid_type_t cid_type;
	const char *cid_max_str = NULL;
	rohc_cid_t cid_very_max;
	rohc_cid_t cid_max;
	int failure_start = -1;

	/* parse program arguments, print the help message in case of failure */
	if(argc <= 1)
	{
		usage();
		goto error;
	}

	for(argc--, argv++; argc > 0; argc -= args_used, argv += args_used)
	{
		args_used = 1;

		if(!strcmp(*argv, "-h"))
		{
			/* print help */
			usage();
			goto error;
		}
		else if(!strcmp(*argv, "-v"))
		{
			/* be more verbose */
			is_verbose = true;
		}
		else if(!strcmp(*argv, "--ignore-malformed"))
		{
			/* do not exit with error code if malformed packets are found */
			ignore_malformed = true;
		}
		else if(!strcmp(*argv, "--cid-type"))
		{
			/* the CID type */
			if(argc <= 1)
			{
				fprintf(stderr, "the --cid-type option requires a value among "
				        "'small' and 'large'\n");
				usage();
				goto error;
			}
			cid_type_str = argv[1];
			args_used++;
		}
		else if(!strcmp(*argv, "--cid-max"))
		{
			/* the MAX_CID value */
			if(argc <= 1)
			{
				fprintf(stderr, "the --cid-max option requires a numeric value\n");
				usage();
				goto error;
			}
			cid_max_str = argv[1];
			args_used++;
		}
		else if(filename == NULL)
		{
			/* get the name of the file that contains the packets to
			 * decompress */
			filename = argv[0];
		}
		else if(failure_start == -1)
		{
			failure_start = atoi(argv[0]);
			if(failure_start < 0)
			{
				fprintf(stderr, "invalid start for failed packets\n");
				goto error;
			}
		}
		else
		{
			/* do not accept more than one filename without option name */
			usage();
			goto error;
		}
	}

	/* the source filename is mandatory */
	if(filename == NULL)
	{
		usage();
		goto error;
	}

	/* CID type shall be small or large, nothing else */
	if(cid_type_str == NULL || strcmp(cid_type_str, "small") == 0)
	{
		cid_type = ROHC_SMALL_CID;
		cid_very_max = ROHC_SMALL_CID_MAX;
	}
	else if(strcmp(cid_type_str, "large") == 0)
	{
		cid_type = ROHC_LARGE_CID;
		cid_very_max = ROHC_LARGE_CID_MAX;
	}
	else
	{
		fprintf(stderr, "the --cid-type option requires a value among "
		        "'small' and 'large'\n");
		usage();
		goto error;
	}

	/* CID_MAX value */
	if(cid_max_str == NULL)
	{
		cid_max = ROHC_SMALL_CID_MAX;
	}
	else
	{
		const int cid_max_int = atoi(cid_max_str);
		if(cid_max_int < 0 || cid_max_int > cid_very_max)
		{
			fprintf(stderr, "the --cid-max option requires a value in range "
			        "[0;%zu]\n", cid_very_max);
			usage();
			goto error;
		}
		cid_max = cid_max_int;
	}

	/* the failure start is mandatory */
	if(failure_start < 0)
	{
		usage();
		goto error;
	}

	/* test ROHC decompression with the packets from the file */
	status = test_decomp(filename, failure_start, cid_type, cid_max,
	                     ignore_malformed);

error:
	return status;
}


/**
 * @brief Print usage of the performance test application
 */
static void usage(void)
{
	fprintf(stderr,
	        "ROHC decompression tool: test the ROHC library with a flow\n"
	        "                         of malformed ROHC packets\n"
	        "\n"
	        "usage: test_malformed_rohc_packets -h\n"
	        "       test_malformed_rohc_packets [-v] FLOW FAILURE_START\n"
	        "\n"
	        "with:\n"
	        "  FLOW                The flow of Ethernet/ROHC frames to\n"
	        "                      decompress (in PCAP format)\n"
	        "  FAILURE_START       The first packet that is malformed ;\n"
	        "                      If set to 0, no success/failure check\n"
	        "                      is performed. This is useful for fuzzing\n"
	        "                      tests.\n"
	        "\n"
	        "options:\n"
	        "  -v                  Print version information and exit\n"
	        "  -h                  Print this usage and exit\n"
	        "  --ignore-malformed  Ignore malformed packets for test\n"
	        "  --cid-type TYPE     The type of CID among 'small' and 'large'\n"
	        "                      (default: small)\n"
	        "  --cid-max MAX_CID   The MAX_CID value\n"
	        "                      (default: 15)\n");
}


/**
 * @brief Test the ROHC library with a flow of ROHC packets
 *
 * @param filename          The name of the PCAP file that contains the ROHC packets
 * @param failure_start     The first packet that shall fail to be decompressed,
 *                          or 0 if no success/failure check shall be performed
 * @param cid_type          The CID type among ROHC_SMALL_CID and ROHC_LARGE_CID
 * @param cid_max           The CID_MAX value
 * @param ignore_malformed  do not exit with error code if malformed packets
 *                          are found
 * @return                  0 in case of success,
 *                          1 in case of failure,
 *                          77 if test is skipped
 */
static int test_decomp(const char *const filename,
                       const size_t failure_start,
                       const rohc_cid_type_t cid_type,
                       const rohc_cid_t cid_max,
                       const bool ignore_malformed)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *handle;
	int link_layer_type;
	int link_len;
	struct pcap_pkthdr header;
	unsigned char *packet;
	struct rohc_decomp *decomp;
	unsigned int counter;
	int test_status = 1;

	/* open the source dump file */
	handle = pcap_open_offline(filename, errbuf);
	if(handle == NULL)
	{
		fprintf(stderr, "failed to open the source pcap file: %s\n", errbuf);
		test_status = 77; /* skip test */
		goto error;
	}

	/* link layer in the source dump must be Ethernet */
	link_layer_type = pcap_datalink(handle);
	if(link_layer_type != DLT_EN10MB &&
	   link_layer_type != DLT_LINUX_SLL &&
	   link_layer_type != DLT_RAW)
	{
		fprintf(stderr, "link layer type %d not supported in source dump (supported = "
		        "%d, %d, %d)\n", link_layer_type, DLT_EN10MB, DLT_LINUX_SLL,
		        DLT_RAW);
		test_status = 77; /* skip test */
		goto close_input;
	}

	if(link_layer_type == DLT_EN10MB)
	{
		link_len = ETHER_HDR_LEN;
	}
	else if(link_layer_type == DLT_LINUX_SLL)
	{
		link_len = LINUX_COOKED_HDR_LEN;
	}
	else /* DLT_RAW */
	{
		link_len = 0;
	}

	/* create the decompressor */
	decomp = rohc_decomp_new2(cid_type, cid_max, ROHC_O_MODE);
	if(decomp == NULL)
	{
		fprintf(stderr, "cannot create the decompressor\n");
		goto close_input;
	}

	/* set the callback for traces */
	if(!rohc_decomp_set_traces_cb2(decomp, print_rohc_traces, NULL))
	{
		fprintf(stderr, "failed to set trace callback\n");
		goto destroy_decomp;
	}

	/* enable decompression profiles */
	if(!rohc_decomp_enable_profiles(decomp, ROHC_PROFILE_UNCOMPRESSED,
	                                ROHC_PROFILE_UDP, ROHC_PROFILE_IP,
	                                ROHC_PROFILE_UDPLITE, ROHC_PROFILE_RTP,
	                                ROHC_PROFILE_ESP, ROHC_PROFILE_TCP, -1))
	{
		fprintf(stderr, "failed to enable the decompression profiles\n");
		goto destroy_decomp;
	}

	/* for each packet in the dump */
	counter = 0;
	while((packet = (unsigned char *) pcap_next(handle, &header)) != NULL)
	{
		const struct rohc_ts arrival_time = { .sec = 0, .nsec = 0 };
		struct rohc_buf rohc_packet =
			rohc_buf_init_full(packet, header.caplen, arrival_time);
		uint8_t ip_buffer[MAX_ROHC_SIZE];
		struct rohc_buf ip_packet = rohc_buf_init_empty(ip_buffer, MAX_ROHC_SIZE);
		uint8_t rcvd_feedback_buf[6];
		struct rohc_buf rcvd_feedback = rohc_buf_init_empty(rcvd_feedback_buf, 6);
		uint8_t send_feedback_buf[6];
		struct rohc_buf send_feedback = rohc_buf_init_empty(send_feedback_buf, 6);
		rohc_status_t status;

		counter++;

		fprintf(stderr, "decompress malformed packet #%u:\n", counter);

		/* check Ethernet frame length */
		if(header.len < link_len || header.len != header.caplen)
		{
			fprintf(stderr, "bad PCAP packet (len = %u, caplen = %u)\n",
			        header.len, header.caplen);
			if(ignore_malformed)
			{
				continue;
			}
			else
			{
				goto destroy_decomp;
			}
		}

		/* skip the link layer header */
		rohc_buf_pull(&rohc_packet, link_len);

		/* decompress the ROHC packet */
		status = rohc_decompress3(decomp, rohc_packet, &ip_packet, &rcvd_feedback,
		                          &send_feedback);
		fprintf(stderr, "\tdecompression status: %s\n", rohc_strerror(status));
		if(failure_start > 0 && status == ROHC_STATUS_OK)
		{
			if(counter >= failure_start)
			{
				fprintf(stderr, "\tunexpected successful decompression\n");
				goto destroy_decomp;
			}
			else
			{
				fprintf(stderr, "\texpected successful decompression\n");
			}
		}
		else if(failure_start > 0)
		{
			if(counter >= failure_start)
			{
				fprintf(stderr, "\texpected decompression failure\n");
			}
			else
			{
				fprintf(stderr, "\tunexpected decompression failure\n");
				goto destroy_decomp;
			}
		}

		/* be ready to get the next received feedback */
		rohc_buf_reset(&rcvd_feedback);
		/* be ready to get the next feedback to send */
		rohc_buf_reset(&send_feedback);
	}

	test_status = 0;

destroy_decomp:
	rohc_decomp_free(decomp);
close_input:
	pcap_close(handle);
error:
	return test_status;
}


/**
 * @brief Callback to print traces of the ROHC library
 *
 * @param priv_ctxt  An optional private context, may be NULL
 * @param level      The priority level of the trace
 * @param entity     The entity that emitted the trace among:
 *                    \li ROHC_TRACE_COMP
 *                    \li ROHC_TRACE_DECOMP
 * @param profile    The ID of the ROHC compression/decompression profile
 *                   the trace is related to
 * @param format     The format string of the trace
 */
static void print_rohc_traces(void *const priv_ctxt,
                              const rohc_trace_level_t level,
                              const rohc_trace_entity_t entity,
                              const int profile,
                              const char *const format,
                              ...)
{
	if(level >= ROHC_TRACE_WARNING || is_verbose)
	{
		va_list args;
		fprintf(stdout, "[%s] ", trace_level_descrs[level]);
		va_start(args, format);
		vfprintf(stdout, format, args);
		va_end(args);
	}
}

