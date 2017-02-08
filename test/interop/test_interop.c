/*
 * Copyright 2014 Viveris Technologies
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
 * @file   test_interop.c
 * @brief  Test interoperability with other implementations
 * @author Didier Barvaux <didier.barvaux@toulouse.viveris.com>
 * @author Didier Barvaux <didier@barvaux.org>
 *
 * Test the ROHC decompression performed by the ROHC library with a flow of
 * ROHC packets that were generated by another ROHC implementation.
 */

#include "test.h"
#include "config.h" /* for HAVE_*_H */

/* system includes */
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#if HAVE_WINSOCK2_H == 1
#  include <winsock2.h> /* for ntohs() on Windows */
#endif
#if HAVE_ARPA_INET_H == 1
#  include <arpa/inet.h> /* for ntohs() on Linux */
#endif
#include <errno.h>
#include <assert.h>
#include <stdarg.h>

/* includes for network headers */
#include <protocols/ipv4.h>
#include <protocols/ipv6.h>
#include <protocols/udp.h>

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
#include <rohc_comp.h>
#include <rohc_decomp.h>


/* prototypes of private functions */
static void usage(void);
static int test_decomp_all(const rohc_cid_type_t cid_type,
                           const size_t wlsb_width,
                           const size_t max_contexts,
                           const char *const src_filename,
                           const char *const cmp_filename);
static int test_decomp_one(struct rohc_decomp *const decomp,
                           const size_t num_packet,
                           const struct pcap_pkthdr header,
                           const unsigned char *const packet,
                           const size_t link_len_src,
                           const unsigned char *const cmp_packet,
                           const size_t cmp_size,
                           const size_t link_len_cmp);

static struct rohc_decomp * create_decompressor(const rohc_cid_type_t cid_type,
                                                const size_t max_contexts)
	__attribute__((warn_unused_result));

static void print_rohc_traces(void *const priv_ctxt,
                              const rohc_trace_level_t level,
                              const rohc_trace_entity_t entity,
                              const int profile,
                              const char *const format,
                              ...)
	__attribute__((format(printf, 5, 6), nonnull(5)));

static bool show_rohc_decomp_stats(const struct rohc_decomp *const decomp)
	__attribute__((nonnull(1), warn_unused_result));
static void show_rohc_decomp_profile(const struct rohc_decomp *const decomp,
                                     const rohc_profile_t profile)
	__attribute__((nonnull(1)));

static bool compare_packets(const uint8_t *const pkt1,
                            const size_t pkt1_size,
                            const uint8_t *const pkt2,
                            const size_t pkt2_size)
	__attribute__((nonnull(1, 3), warn_unused_result));


/** Whether the application runs in verbose mode or not */
static bool is_verbose;


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
	char *cid_type_name = NULL;
	char *src_filename = NULL;
	char *cmp_filename = NULL;
	int max_contexts = ROHC_SMALL_CID_MAX + 1;
	int wlsb_width = 4;
	int status = 1;
	rohc_cid_type_t cid_type;
	int args_used;

	/* set to quiet mode by default */
	is_verbose = false;

	/* parse program arguments, print the help message in case of failure */
	if(argc <= 1)
	{
		usage();
		goto error;
	}

	for(argc--, argv++; argc > 0; argc -= args_used, argv += args_used)
	{
		args_used = 1;

		if(!strcmp(*argv, "-V") || !strcmp(*argv, "--version"))
		{
			/* print version */
			printf("test_interop version %s\n", rohc_version());
			goto error;
		}
		else if(!strcmp(*argv, "-h") || !strcmp(*argv, "--help"))
		{
			/* print help */
			usage();
			goto error;
		}
		else if(!strcmp(*argv, "-v") || !strcmp(*argv, "--verbose"))
		{
			/* enable verbose mode */
			is_verbose = true;
		}
		else if(!strcmp(*argv, "-c"))
		{
			/* get the name of the file where the uncompressed packets used for
			 * comparison are stored */
			if(argc <= 1)
			{
				fprintf(stderr, "option -c takes one argument\n\n");
				usage();
				goto error;
			}
			cmp_filename = argv[1];
			args_used++;
		}
		else if(!strcmp(*argv, "--max-contexts"))
		{
			/* get the maximum number of contexts the test should use */
			if(argc <= 1)
			{
				fprintf(stderr, "option --max-contexts takes one argument\n\n");
				usage();
				goto error;
			}
			max_contexts = atoi(argv[1]);
			args_used++;
		}
		else if(!strcmp(*argv, "--wlsb-width"))
		{
			/* get the width of the WLSB window the test should use */
			if(argc <= 1)
			{
				fprintf(stderr, "option --wlsb-width takes one argument\n\n");
				usage();
				goto error;
			}
			wlsb_width = atoi(argv[1]);
			args_used++;
		}
		else if(cid_type_name == NULL)
		{
			/* get the type of CID to use within the ROHC library */
			cid_type_name = argv[0];
		}
		else if(src_filename == NULL)
		{
			/* get the name of the file that contains the packets to decompress */
			src_filename = argv[0];
		}
		else
		{
			/* do not accept more than one filename without option name */
			usage();
			goto error;
		}
	}

	/* check CID type */
	if(cid_type_name == NULL)
	{
		fprintf(stderr, "CID_TYPE is a mandatory parameter\n\n");
		usage();
		goto error;
	}
	else if(!strcmp(cid_type_name, "smallcid"))
	{
		cid_type = ROHC_SMALL_CID;

		/* the maximum number of ROHC contexts should be valid */
		if(max_contexts < 1 || max_contexts > (ROHC_SMALL_CID_MAX + 1))
		{
			fprintf(stderr, "the maximum number of ROHC contexts should be "
			        "between 1 and %u\n\n", ROHC_SMALL_CID_MAX + 1);
			usage();
			goto error;
		}
	}
	else if(!strcmp(cid_type_name, "largecid"))
	{
		cid_type = ROHC_LARGE_CID;

		/* the maximum number of ROHC contexts should be valid */
		if(max_contexts < 1 || max_contexts > (ROHC_LARGE_CID_MAX + 1))
		{
			fprintf(stderr, "the maximum number of ROHC contexts should be "
			        "between 1 and %u\n\n", ROHC_LARGE_CID_MAX + 1);
			usage();
			goto error;
		}
	}
	else
	{
		fprintf(stderr, "invalid CID type '%s', only 'smallcid' and 'largecid' "
		        "expected\n", cid_type_name);
		goto error;
	}

	/* check WLSB width */
	if(wlsb_width <= 0 || (wlsb_width & (wlsb_width - 1)) != 0)
	{
		fprintf(stderr, "invalid WLSB width %d: should be a positive power of "
		        "two\n", wlsb_width);
		goto error;
	}

	/* the source filename is mandatory */
	if(src_filename == NULL)
	{
		fprintf(stderr, "FLOW is a mandatory parameter\n\n");
		usage();
		goto error;
	}

	/* test ROHC decompression with the packets from the file */
	status = test_decomp_all(cid_type, wlsb_width, max_contexts,
	                         src_filename, cmp_filename);

error:
	return status;
}


/**
 * @brief Print usage of the performance test application
 */
static void usage(void)
{
	fprintf(stderr,
	        "ROHC interoperability tool: test the ROHC library with a flow "
	        "of ROHC packets generated by another implementation\n"
	        "\n"
	        "usage: test_interop [OPTIONS] CID_TYPE FLOW\n"
	        "\n"
	        "with:\n"
	        "  CID_TYPE                The type of CID to use among 'smallcid'\n"
	        "                          and 'largecid'\n"
	        "  FLOW                    The flow of Ethernet frames to compress\n"
	        "                          (in PCAP format)\n"
	        "\n"
	        "options:\n"
	        "  -V, --version           Print version information and exit\n"
	        "  -h, --help              Print this usage and exit\n"
	        "  -c FILE                 Compare the generated ROHC packets with the\n"
	        "                          ROHC packets stored in FILE (PCAP format)\n"
	        "  --max-contexts NUM      The maximum number of ROHC contexts to\n"
	        "                          simultaneously use during the test\n"
	        "  --wlsb-width NUM        The width of the WLSB window to use\n"
	        "  -v, --verbose           Run the test in verbose mode\n");
}


/**
 * @brief Print statistics about the given decompressor
 *
 * @param decomp    The decompressor to print statistics for
 * @return          true if statistics were printed, false if a problem occurred
 */
static bool show_rohc_decomp_stats(const struct rohc_decomp *const decomp)
{
	rohc_decomp_general_info_t general_info;
	unsigned long percent;
	size_t max_cid;
	size_t mrru;
	rohc_cid_type_t cid_type;

	assert(decomp != NULL);

	/* general information */
	general_info.version_major = 0;
	general_info.version_minor = 1;
	if(!rohc_decomp_get_general_info(decomp, &general_info))
	{
		fprintf(stderr, "failed to get general information for decompressor\n");
		goto error;
	}

	printf("=== decompressor\n");
	printf("===\tcreator: %s\n", PACKAGE_NAME " (" PACKAGE_URL ")");
	printf("===\tversion: %s\n", rohc_version());

	/* configuration */
	printf("===\tconfiguration:\n");
	if(!rohc_decomp_get_cid_type(decomp, &cid_type))
	{
		fprintf(stderr, "failed to get CID type for decompressor\n");
		goto error;
	}
	printf("===\t\tcid_type: %s\n", cid_type == ROHC_LARGE_CID ? "large" : "small");
	if(!rohc_decomp_get_max_cid(decomp, &max_cid))
	{
		fprintf(stderr, "failed to get MAX_CID for decompressor\n");
		goto error;
	}
	printf("===\t\tmax_cid:  %zu\n", max_cid);
	/* retrieve current decompressor MRRU */
	if(!rohc_decomp_get_mrru(decomp, &mrru))
	{
		fprintf(stderr, "failed to get MRRU for decompressor\n");
		goto error;
	}
	printf("===\t\tmrru:     %zu\n", mrru);

	/* profiles */
	printf("===\tprofiles:\n");
	show_rohc_decomp_profile(decomp, ROHC_PROFILE_UNCOMPRESSED);
	show_rohc_decomp_profile(decomp, ROHC_PROFILE_RTP);
	show_rohc_decomp_profile(decomp, ROHC_PROFILE_UDP);
	show_rohc_decomp_profile(decomp, ROHC_PROFILE_ESP);
	show_rohc_decomp_profile(decomp, ROHC_PROFILE_IP);
	show_rohc_decomp_profile(decomp, ROHC_PROFILE_TCP);
	show_rohc_decomp_profile(decomp, ROHC_PROFILE_UDPLITE);

	/* statistics */
	printf("===\tstatistics:\n");
	printf("===\t\tflows:               %zu\n", general_info.contexts_nr);
	printf("===\t\tpackets:             %lu\n", general_info.packets_nr);
	if(general_info.comp_bytes_nr != 0)
	{
		percent = (100 * general_info.uncomp_bytes_nr) /
		          general_info.comp_bytes_nr;
	}
	else
	{
		percent = 0;
	}
	printf("===\t\tdecompression_ratio: %lu%%\n", percent);
	printf("\n");

	return true;

error:
	return false;
}


/**
 * @brief Print details about a decompression profile
 *
 * @param decomp   The decompressor to print statistics for
 * @param profile  The decompression profile to print details for
 */
static void show_rohc_decomp_profile(const struct rohc_decomp *const decomp,
                                     const rohc_profile_t profile)
{
	printf("===\t\t%s profile: %s (%d)\n",
	       rohc_decomp_profile_enabled(decomp, profile) ? "enabled " : "disabled",
	       rohc_get_profile_descr(profile), profile);
}


/**
 * @brief Decompress one ROHC packet and compare the result with a reference
 *
 * @param decomp           The decompressor to use to decompress the ROHC packet
 * @param num_packet       A number affected to the IP packet to compress/decompress
 * @param header           The PCAP header for the packet
 * @param packet           The packet to compress/decompress (link layer included)
 * @param link_len_src     The length of the link layer header before IP data
 * @param cmp_packet       The ROHC packet for comparison purpose
 * @param cmp_size         The size of the ROHC packet used for comparison
 *                         purpose
 * @param link_len_cmp     The length of the link layer header before ROHC data
 * @return                 0 if the process is successful
 *                         -1 if ROHC packet is malformed
 *                         -2 if decompression fails
 *                         -3 if the decompressed packet doesn't match the
 *                            reference
 */
static int test_decomp_one(struct rohc_decomp *const decomp,
                           const size_t num_packet,
                           const struct pcap_pkthdr header,
                           const unsigned char *const packet,
                           const size_t link_len_src,
                           const unsigned char *const cmp_packet,
                           const size_t cmp_size,
                           const size_t link_len_cmp)
{
	const struct rohc_ts arrival_time = { .sec = 0, .nsec = 0 };
	struct rohc_buf rohc_packet =
		rohc_buf_init_full((uint8_t *) packet, header.caplen, arrival_time);
	uint8_t uncomp_buffer[MAX_ROHC_SIZE];
	struct rohc_buf uncomp_packet =
		rohc_buf_init_empty(uncomp_buffer, MAX_ROHC_SIZE);
	rohc_status_t status;

	printf("=== decompressor packet #%zu:\n", num_packet);

	/* check Ethernet frame length */
	if(header.len <= link_len_src || header.len != header.caplen)
	{
		fprintf(stderr, "bad PCAP packet (len = %u, caplen = %u)\n",
		        header.len, header.caplen);
		goto error_fmt;
	}
	if(cmp_packet != NULL && cmp_size <= link_len_cmp)
	{
		fprintf(stderr, "bad comparison packet: too small for link header\n");
		goto error_fmt;
	}

	/* skip the link layer header */
	rohc_buf_pull(&rohc_packet, link_len_src);

	/* decompress the ROHC packet */
	printf("=== ROHC decompression: start\n");
	status = rohc_decompress3(decomp, rohc_packet, &uncomp_packet, NULL, NULL);
	if(status != ROHC_STATUS_OK)
	{
		size_t i;

		printf("=== ROHC decompression: failure\n");
		printf("=== original %zu-byte compressed packet:\n", rohc_packet.len);
		for(i = 0; i < rohc_packet.len; i++)
		{
			if(i > 0 && (i % 16) == 0)
			{
				printf("\n");
			}
			else if(i > 0 && (i % 8) == 0)
			{
				printf("  ");
			}
			printf("%02x ", rohc_buf_byte_at(rohc_packet, i));
		}
		printf("\n\n");
		goto error_decomp;
	}
	printf("=== ROHC decompression: success\n");

	/* compare the decompressed packet with the original one */
	printf("=== uncompressed packet comparison: start\n");
	if(cmp_packet && !compare_packets(rohc_buf_data(uncomp_packet),
	                                  uncomp_packet.len,
	                                  cmp_packet + link_len_cmp,
	                                  cmp_size - link_len_cmp))
	{
		printf("=== uncompressed packet comparison: failure\n");
		printf("\n");
		goto error_cmp;
	}
	printf("=== uncompressed packet comparison: success\n");

	printf("\n");
	return 0;

error_fmt:
	return -1;
error_decomp:
	return -2;
error_cmp:
	return -3;
}


/**
 * @brief Test the ROHC library decompression with a flow of ROHC packets
 *        generated by another ROHC implementation
 *
 * @param cid_type             The type of CIDs the compressor shall use
 * @param wlsb_width           The width of the WLSB window to use
 * @param max_contexts         The maximum number of ROHC contexts to use
 * @param src_filename         The name of the PCAP file that contains the
 *                             ROHC packets to decompress
 * @param cmp_filename         The name of the PCAP file that contains the
 *                             uncompressed packets used for comparison
 * @return                     0 in case of success,
 *                             1 in case of failure,
 *                             77 if test is skipped
 */
static int test_decomp_all(const rohc_cid_type_t cid_type,
                           const size_t wlsb_width,
                           const size_t max_contexts,
                           const char *const src_filename,
                           const char *const cmp_filename)
{
	char errbuf[PCAP_ERRBUF_SIZE];

	pcap_t *handle;
	struct pcap_pkthdr header;
	int link_layer_type_src;
	size_t link_len_src;
	unsigned char *packet;

	pcap_t *cmp_handle;
	struct pcap_pkthdr cmp_header;
	int link_layer_type_cmp;
	size_t link_len_cmp = 0;
	unsigned char *cmp_packet;

	size_t counter;

	struct rohc_decomp *decomp;

	size_t nb_bad = 0;
	size_t nb_ok = 0;
	size_t err_decomp = 0;
	size_t nb_ref = 0;

	int status = 1;
	int ret;

	printf("=== initialization:\n");

	/* open the source dump file */
	handle = pcap_open_offline(src_filename, errbuf);
	if(handle == NULL)
	{
		printf("failed to open the source pcap file: %s\n", errbuf);
		goto error;
	}

	/* link layer in the source dump must be Ethernet */
	link_layer_type_src = pcap_datalink(handle);
	if(link_layer_type_src != DLT_EN10MB &&
	   link_layer_type_src != DLT_LINUX_SLL &&
	   link_layer_type_src != DLT_RAW &&
	   link_layer_type_src != DLT_NULL)
	{
		printf("link layer type %d not supported in source dump (supported = "
		       "%d, %d, %d, %d)\n", link_layer_type_src, DLT_EN10MB,
		       DLT_LINUX_SLL, DLT_RAW, DLT_NULL);
		status = 77; /* skip test */
		goto close_input;
	}

	if(link_layer_type_src == DLT_EN10MB)
	{
		link_len_src = ETHER_HDR_LEN;
	}
	else if(link_layer_type_src == DLT_LINUX_SLL)
	{
		link_len_src = LINUX_COOKED_HDR_LEN;
	}
	else if(link_layer_type_src == DLT_NULL)
	{
		link_len_src = BSD_LOOPBACK_HDR_LEN;
	}
	else /* DLT_RAW */
	{
		link_len_src = 0;
	}

	/* open the uncompressed comparison dump file if asked */
	if(cmp_filename != NULL)
	{
		cmp_handle = pcap_open_offline(cmp_filename, errbuf);
		if(cmp_handle == NULL)
		{
			printf("failed to open the comparison pcap file: %s\n", errbuf);
			goto close_input;
		}

		/* link layer in the rohc_comparison dump must be Ethernet */
		link_layer_type_cmp = pcap_datalink(cmp_handle);
		if(link_layer_type_cmp != DLT_EN10MB &&
		   link_layer_type_cmp != DLT_LINUX_SLL &&
		   link_layer_type_cmp != DLT_RAW &&
		   link_layer_type_cmp != DLT_NULL)
		{
			printf("link layer type %d not supported in comparison dump "
			       "(supported = %d, %d, %d, %d)\n", link_layer_type_cmp,
			       DLT_EN10MB, DLT_LINUX_SLL, DLT_RAW, DLT_NULL);
			status = 77; /* skip test */
			goto close_comparison;
		}

		if(link_layer_type_cmp == DLT_EN10MB)
		{
			link_len_cmp = ETHER_HDR_LEN;
		}
		else if(link_layer_type_cmp == DLT_LINUX_SLL)
		{
			link_len_cmp = LINUX_COOKED_HDR_LEN;
		}
		else if(link_layer_type_cmp == DLT_NULL)
		{
			link_len_cmp = BSD_LOOPBACK_HDR_LEN;
		}
		else /* DLT_RAW */
		{
			link_len_cmp = 0;
		}
	}
	else
	{
		cmp_handle = NULL;
	}

	/* create the decompressor */
	decomp = create_decompressor(cid_type, max_contexts);
	if(decomp == NULL)
	{
		printf("failed to create the decompressor 1\n");
		goto close_comparison;
	}

	printf("\n");

	/* for each ROHC packet in the dump */
	counter = 0;
	while((packet = (unsigned char *) pcap_next(handle, &header)) != NULL)
	{
		counter++;

		/* get next uncompressed packet from the comparison dump file if asked */
		if(cmp_handle != NULL)
		{
			cmp_packet = (unsigned char *) pcap_next(cmp_handle, &cmp_header);
		}
		else
		{
			cmp_packet = NULL;
			cmp_header.caplen = 0;
		}

		/* decompress one packet of the flow and compare it with the given
		 * reference */
		ret = test_decomp_one(decomp, counter,
		                      header, packet, link_len_src,
		                      cmp_packet, cmp_header.caplen, link_len_cmp);
		if(ret == 0)
		{
			nb_ok++;
		}
		else if(ret == -2)
		{
			err_decomp++;
		}
		else if(ret != -3)
		{
			nb_ref++;
		}
		else
		{
			nb_bad++;
		}
	}

	/* show the compression/decompression results */
	printf("=== summary:\n");
	printf("===\tpackets_processed:    %zu\n", counter);
	printf("===\tmalformed:            %zu\n", nb_bad);
	printf("===\tdecompression_failed: %zu\n", err_decomp);
	printf("===\tmatches:              %zu\n", nb_ok);
	printf("\n");

	/* show some info/stats about the decompressor */
	if(!show_rohc_decomp_stats(decomp))
	{
		fprintf(stderr, "failed to dump ROHC decompressor stats\n");
		goto free_decomp;
	}
	printf("\n");

	/* destroy the compressors and decompressors */
	printf("=== shutdown:\n");
	if(err_decomp == 0 && nb_bad == 0 && nb_ref == 0 && nb_ok == counter)
	{
		/* test is successful */
		status = 0;
	}

free_decomp:
	rohc_decomp_free(decomp);
close_comparison:
	if(cmp_handle != NULL)
	{
		pcap_close(cmp_handle);
	}
close_input:
	pcap_close(handle);
error:
	return status;
}


/**
 * @brief Create and configure a ROHC decompressor
 *
 * @param cid_type      The type of CIDs the compressor shall use
 * @param max_contexts  The maximum number of ROHC contexts to use
 * @return              The new ROHC decompressor
 */
static struct rohc_decomp * create_decompressor(const rohc_cid_type_t cid_type,
                                                const size_t max_contexts)
{
	struct rohc_decomp *decomp;

	/* create the decompressor */
	decomp = rohc_decomp_new2(cid_type, max_contexts - 1, ROHC_U_MODE);
	if(decomp == NULL)
	{
		printf("failed to create decompressor\n");
		goto error;
	}

	/* set the callback for traces */
	if(!rohc_decomp_set_traces_cb2(decomp, print_rohc_traces, NULL))
	{
		printf("failed to set trace callback\n");
		goto destroy_decomp;
	}

	/* enable decompression profiles */
	if(!rohc_decomp_enable_profiles(decomp, ROHC_PROFILE_UNCOMPRESSED,
	                                ROHC_PROFILE_UDP, ROHC_PROFILE_IP,
	                                ROHC_PROFILE_UDPLITE, ROHC_PROFILE_RTP,
	                                ROHC_PROFILE_ESP, ROHC_PROFILE_TCP, -1))
	{
		printf("failed to enable the profiles\n");
		goto destroy_decomp;
	}

	return decomp;

destroy_decomp:
	rohc_decomp_free(decomp);
error:
	return NULL;
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


/**
 * @brief Compare two network packets and print differences if any
 *
 * @param pkt1       The first packet
 * @param pkt1_size  The size of the first packet (in bytes)
 * @param pkt2       The second packet
 * @param pkt2_size  The size of the second packet (in bytes)
 * @return           Whether the packets are equal or not
 */
static bool compare_packets(const uint8_t *const pkt1,
                            const size_t pkt1_size,
                            const uint8_t *const pkt2,
                            const size_t pkt2_size)
{
	size_t min_size;
	size_t bytes_on_line;
	char str1[4][7], str2[4][7];
	char sep1, sep2;

	/* do not compare more than the shortest of the 2 packets */
	min_size = min(pkt1_size, pkt2_size);

	/* do not compare more than 180 bytes to avoid huge output */
	min_size = min(180, min_size);

	/* if packets are equal, do not print the packets */
	if(pkt1_size == pkt2_size && memcmp(pkt1, pkt2, pkt1_size) == 0)
	{
		return true;
	}

	/* packets are different, print the differences */
	printf("------------------------------ Compare ------------------------------\n");
	printf("--------- reference ----------         ----------- new --------------\n");

	if(pkt1_size != pkt2_size)
	{
		printf("packets have different sizes (%zu != %zu), compare only the "
		       "%zu first bytes\n", pkt1_size, pkt2_size, min_size);
	}

	bytes_on_line = 0;
	for(size_t i = 0; i < min_size; i++)
	{
		if(pkt1[i] != pkt2[i])
		{
			sep1 = '#';
			sep2 = '#';
		}
		else
		{
			sep1 = '[';
			sep2 = ']';
		}

		sprintf(str1[bytes_on_line], "%c0x%.2x%c", sep1, pkt1[i], sep2);
		sprintf(str2[bytes_on_line], "%c0x%.2x%c", sep1, pkt2[i], sep2);

		/* make the output human readable */
		if(bytes_on_line < 3 && (i + 1) < min_size)
		{
			/* not enough bytes to print the full line */
			bytes_on_line++;
			continue;
		}

		/* pretty print the bytes of the reference packet */
		for(size_t byte_on_line = 0; byte_on_line < 4; byte_on_line++)
		{
			if(byte_on_line < (bytes_on_line + 1))
			{
				printf("%s  ", str1[byte_on_line]);
			}
			else /* fill the line with blanks if nothing to print */
			{
				printf("        ");
			}
		}
		printf("       ");

		/* pretty print the bytes of the other packet */
		for(size_t byte_on_line = 0; byte_on_line <= bytes_on_line; byte_on_line++)
		{
			printf("%s  ", str2[byte_on_line]);
		}
		printf("\n");

		bytes_on_line = 0;
	}

	printf("----------------------- packets are different -----------------------\n");

	return false;
}

