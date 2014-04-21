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
 * @file   c_esp.c
 * @brief  ROHC ESP compression profile
 * @author FWX <rohc_team@dialine.fr>
 * @author Didier Barvaux <didier@barvaux.org>
 */

#include "c_esp.h"
#include "c_generic.h"
#include "c_ip.h"
#include "rohc_traces_internal.h"
#include "crc.h"
#include "protocols/esp.h"
#include "rohc_utils.h"

#ifdef __KERNEL__
#	include <linux/types.h>
#else
#	include <stdbool.h>
#endif
#ifndef __KERNEL__
#	include <string.h>
#endif
#include <assert.h>


/*
 * Private structures and types
 */

/**
 * @brief Define the ESP part of the profile decompression context
 *
 * This object must be used with the generic part of the decompression
 * context c_generic_context.
 *
 * @see c_generic_context
 */
struct sc_esp_context
{
	/// The previous ESP header
	struct esphdr old_esp;
};


/*
 * Private function prototypes
 */

static int c_esp_create(struct c_context *const context,
                        const struct ip_packet *ip);

static bool c_esp_check_context(const struct c_context *context,
                                const struct ip_packet *ip);

static int c_esp_encode(struct c_context *const context,
                        const struct ip_packet *ip,
                        const size_t packet_size,
                        unsigned char *const dest,
                        const size_t dest_size,
                        rohc_packet_t *const packet_type,
                        int *const payload_offset);

static uint32_t c_esp_get_next_sn(const struct c_context *context,
                                  const struct ip_packet *outer_ip,
                                  const struct ip_packet *inner_ip);

static int esp_code_static_esp_part(const struct c_context *context,
                                    const unsigned char *next_header,
                                    unsigned char *const dest,
                                    int counter);

static int esp_code_dynamic_esp_part(const struct c_context *context,
                                     const unsigned char *next_header,
                                     unsigned char *const dest,
                                     int counter);


/*
 * Private function definitions
 */

/**
 * @brief Create a new ESP context and initialize it thanks to the given IP/ESP
 *        packet.
 *
 * This function is one of the functions that must exist in one profile for the
 * framework to work.
 *
 * @param context The compression context
 * @param ip      The IP/ESP packet given to initialize the new context
 * @return        1 if successful, 0 otherwise
 */
static int c_esp_create(struct c_context *const context,
                        const struct ip_packet *ip)
{
	struct c_generic_context *g_context;
	struct sc_esp_context *esp_context;
	struct ip_packet ip2;
	const struct ip_packet *last_ip_header;
	const struct esphdr *esp;
	unsigned int ip_proto;

	assert(context != NULL);
	assert(context->profile != NULL);
	assert(ip != NULL);

	/* create and initialize the generic part of the profile context */
	if(!c_generic_create(context, ROHC_LSB_SHIFT_ESP_SN, ip))
	{
		rohc_warning(context->compressor, ROHC_TRACE_COMP, context->profile->id,
		             "generic context creation failed\n");
		goto quit;
	}
	g_context = (struct c_generic_context *) context->specific;

	/* check if packet is IP/ESP or IP/IP/ESP */
	ip_proto = ip_get_protocol(ip);
	if(ip_proto == ROHC_IPPROTO_IPIP || ip_proto == ROHC_IPPROTO_IPV6)
	{
		/* get the last IP header */
		if(!ip_get_inner_packet(ip, &ip2))
		{
			rohc_warning(context->compressor, ROHC_TRACE_COMP, context->profile->id,
			             "cannot create the inner IP header\n");
			goto clean;
		}

		/* two IP headers, the last IP header is the second one */
		last_ip_header = &ip2;

		/* get the transport protocol */
		ip_proto = ip_get_protocol(last_ip_header);
	}
	else
	{
		/* only one single IP header, the last IP header is the first one */
		last_ip_header = ip;
	}

	if(ip_proto != ROHC_IPPROTO_ESP)
	{
		rohc_warning(context->compressor, ROHC_TRACE_COMP, context->profile->id,
		             "next header is not ESP (%d), cannot use this profile\n",
		             ip_proto);
		goto clean;
	}

	esp = (struct esphdr *) ip_get_next_layer(last_ip_header);

	/* initialize SN with the SN found in the ESP header */
	g_context->sn = rohc_ntoh32(esp->sn);
	rohc_comp_debug(context, "initialize context(SN) = hdr(SN) of first "
	                "packet = %u\n", g_context->sn);

	/* create the ESP part of the profile context */
	esp_context = malloc(sizeof(struct sc_esp_context));
	if(esp_context == NULL)
	{
		rohc_error(context->compressor, ROHC_TRACE_COMP, context->profile->id,
		           "no memory for the ESP part of the profile context\n");
		goto clean;
	}
	g_context->specific = esp_context;

	/* initialize the ESP part of the profile context */
	memcpy(&(esp_context->old_esp), esp, sizeof(struct esphdr));

	/* init the ESP-specific variables and functions */
	g_context->next_header_proto = ROHC_IPPROTO_ESP;
	g_context->next_header_len = sizeof(struct esphdr);
	g_context->encode_uncomp_fields = NULL;
	g_context->decide_state = decide_state;
	g_context->decide_FO_packet = c_ip_decide_FO_packet;
	g_context->decide_SO_packet = c_ip_decide_SO_packet;
	g_context->decide_extension = decide_extension;
	g_context->init_at_IR = NULL;
	g_context->get_next_sn = c_esp_get_next_sn;
	g_context->code_static_part = esp_code_static_esp_part;
	g_context->code_dynamic_part = esp_code_dynamic_esp_part;
	g_context->code_ir_remainder = NULL;
	g_context->code_UO_packet_head = NULL;
	g_context->code_uo_remainder = NULL;
	g_context->compute_crc_static = esp_compute_crc_static;
	g_context->compute_crc_dynamic = esp_compute_crc_dynamic;

	return 1;

clean:
	c_generic_destroy(context);
quit:
	return 0;
}


/**
 * @brief Check if the given packet corresponds to the ESP profile
 *
 * Conditions are:
 *  \li the transport protocol is ESP
 *  \li the version of the outer IP header is 4 or 6
 *  \li the outer IP header is not an IP fragment
 *  \li if there are at least 2 IP headers, the version of the inner IP header
 *      is 4 or 6
 *  \li if there are at least 2 IP headers, the inner IP header is not an IP
 *      fragment
 *
 * @see c_generic_check_profile
 *
 * This function is one of the functions that must exist in one profile for the
 * framework to work.
 *
 * @param comp      The ROHC compressor
 * @param outer_ip  The outer IP header of the IP packet to check
 * @param inner_ip  \li The inner IP header of the IP packet to check if the IP
 *                      packet contains at least 2 IP headers,
 *                  \li NULL if the IP packet to check contains only one IP header
 * @param protocol  The transport protocol carried by the IP packet:
 *                    \li the protocol carried by the outer IP header if there
 *                        is only one IP header,
 *                    \li the protocol carried by the inner IP header if there
 *                        are at least two IP headers.
 * @param ctxt_key  The key to help finding the context associated with packet
 * @return          Whether the IP packet corresponds to the profile:
 *                    \li true if the IP packet corresponds to the profile,
 *                    \li false if the IP packet does not correspond to
 *                        the profile
 */
bool c_esp_check_profile(const struct rohc_comp *const comp,
                         const struct ip_packet *const outer_ip,
                         const struct ip_packet *const inner_ip,
                         const uint8_t protocol,
                         rohc_ctxt_key_t *const ctxt_key)
{
	const struct ip_packet *last_ip_header;
	const struct esphdr *esp_header;
	unsigned int ip_payload_size;
	bool ip_check;

	assert(comp != NULL);
	assert(outer_ip != NULL);
	assert(ctxt_key != NULL);

	/* check that the transport protocol is ESP */
	if(protocol != ROHC_IPPROTO_ESP)
	{
		goto bad_profile;
	}

	/* check that the the versions of outer and inner IP headers are 4 or 6
	   and that outer and inner IP headers are not IP fragments */
	ip_check = c_generic_check_profile(comp, outer_ip, inner_ip, protocol,
	                                   ctxt_key);
	if(!ip_check)
	{
		goto bad_profile;
	}

	/* determine the last IP header */
	if(inner_ip != NULL)
	{
		/* two IP headers, the last IP header is the inner IP header */
		last_ip_header = inner_ip;
	}
	else
	{
		/* only one IP header, last IP header is the outer IP header */
		last_ip_header = outer_ip;
	}

	/* IP payload shall be large enough for ESP header */
	ip_payload_size = ip_get_plen(last_ip_header);
	if(ip_payload_size < sizeof(struct esphdr))
	{
		goto bad_profile;
	}

	/* retrieve the ESP header */
	esp_header = (const struct esphdr *) ip_get_next_layer(last_ip_header);
	*ctxt_key ^= esp_header->spi;

	return true;

bad_profile:
	return false;
}


/**
 * @brief Check if the IP/ESP packet belongs to the context
 *
 * Conditions are:
 *  - the number of IP headers must be the same as in context
 *  - IP version of the two IP headers must be the same as in context
 *  - IP packets must not be fragmented
 *  - the source and destination addresses of the two IP headers must match the
 *    ones in the context
 *  - the transport protocol must be ESP
 *  - the security parameters index of the ESP header must match the one in
 *    the context
 *  - IPv6 only: the Flow Label of the two IP headers must match the ones the
 *    context
 *
 * This function is one of the functions that must exist in one profile for the
 * framework to work.
 *
 * @param context The compression context
 * @param ip      The IP/ESP packet to check
 * @return        true if the IP/ESP packet belongs to the context,
 *                false if it does not belong to the context
 */
bool c_esp_check_context(const struct c_context *context,
                         const struct ip_packet *ip)
{
	struct c_generic_context *g_context;
	struct sc_esp_context *esp_context;
	struct ip_header_info *ip_flags;
	struct ip_header_info *ip2_flags;
	struct ip_packet ip2;
	const struct ip_packet *last_ip_header;
	const struct esphdr *esp;
	ip_version version;
	unsigned int ip_proto;
	bool is_ip_same;
	bool is_esp_same;

	assert(context != NULL);
	assert(ip != NULL);

	assert(context->specific != NULL);
	g_context = (struct c_generic_context *) context->specific;
	assert(g_context->specific != NULL);
	esp_context = (struct sc_esp_context *) g_context->specific;
	ip_flags = &g_context->ip_flags;
	ip2_flags = &g_context->ip2_flags;

	/* check the IP version of the first header */
	version = ip_get_version(ip);
	if(version != ip_flags->version)
	{
		goto bad_context;
	}

	/* compare the addresses of the first header */
	if(version == IPV4)
	{
		is_ip_same = ip_flags->info.v4.old_ip.saddr == ipv4_get_saddr(ip) &&
		             ip_flags->info.v4.old_ip.daddr == ipv4_get_daddr(ip);
	}
	else /* IPV6 */
	{
		is_ip_same =
			IPV6_ADDR_CMP(&ip_flags->info.v6.old_ip.ip6_src, ipv6_get_saddr(ip)) &&
			IPV6_ADDR_CMP(&ip_flags->info.v6.old_ip.ip6_dst, ipv6_get_daddr(ip));
	}

	if(!is_ip_same)
	{
		goto bad_context;
	}

	/* compare the Flow Label of the first header if IPv6 */
	if(version == IPV6 && ipv6_get_flow_label(ip) !=
	   IPV6_GET_FLOW_LABEL(ip_flags->info.v6.old_ip))
	{
		goto bad_context;
	}

	/* check the second IP header */
	ip_proto = ip_get_protocol(ip);
	if(ip_proto == ROHC_IPPROTO_IPIP || ip_proto == ROHC_IPPROTO_IPV6)
	{
		bool is_ip2_same;

		/* check if the context used to have a second IP header */
		if(!g_context->is_ip2_initialized)
		{
			goto bad_context;
		}

		/* get the second IP header */
		if(!ip_get_inner_packet(ip, &ip2))
		{
			rohc_warning(context->compressor, ROHC_TRACE_COMP, context->profile->id,
			             "cannot create the inner IP header\n");
			goto bad_context;
		}

		/* check the IP version of the second header */
		version = ip_get_version(&ip2);
		if(version != ip2_flags->version)
		{
			goto bad_context;
		}

		/* compare the addresses of the second header */
		if(version == IPV4)
		{
			is_ip2_same = ip2_flags->info.v4.old_ip.saddr == ipv4_get_saddr(&ip2) &&
			              ip2_flags->info.v4.old_ip.daddr == ipv4_get_daddr(&ip2);
		}
		else /* IPV6 */
		{
			is_ip2_same = IPV6_ADDR_CMP(&ip2_flags->info.v6.old_ip.ip6_src,
			                            ipv6_get_saddr(&ip2)) &&
			              IPV6_ADDR_CMP(&ip2_flags->info.v6.old_ip.ip6_dst,
			                            ipv6_get_daddr(&ip2));
		}

		if(!is_ip2_same)
		{
			goto bad_context;
		}

		/* compare the Flow Label of the second header if IPv6 */
		if(version == IPV6 && ipv6_get_flow_label(&ip2) !=
		   IPV6_GET_FLOW_LABEL(ip2_flags->info.v6.old_ip))
		{
			goto bad_context;
		}

		/* get the last IP header */
		last_ip_header = &ip2;

		/* get the transport protocol */
		ip_proto = ip_get_protocol(&ip2);
	}
	else /* no second IP header */
	{
		/* check if the context used not to have a second header */
		if(g_context->is_ip2_initialized)
		{
			goto bad_context;
		}

		/* only one single IP header, the last IP header is the first one */
		last_ip_header = ip;
	}

	/* check the transport protocol */
	if(ip_proto != ROHC_IPPROTO_ESP)
	{
		goto bad_context;
	}

	/* check Security parameters index (SPI) */
	esp = (struct esphdr *) ip_get_next_layer(last_ip_header);
	is_esp_same = esp_context->old_esp.spi == esp->spi;

	return is_esp_same;

bad_context:
	return false;
}


/**
 * @brief Encode an IP/ESP packet according to a pattern decided by several
 *        different factors.
 *
 * @param context        The compression context
 * @param ip             The IP packet to encode
 * @param packet_size    The length of the IP packet to encode
 * @param dest           The rohc-packet-under-build buffer
 * @param dest_size      The length of the rohc-packet-under-build buffer
 * @param packet_type    OUT: The type of ROHC packet that is created
 * @param payload_offset The offset for the payload in the IP packet
 * @return               The length of the created ROHC packet
 *                       or -1 in case of failure
 */
static int c_esp_encode(struct c_context *const context,
                        const struct ip_packet *ip,
                        const size_t packet_size,
                        unsigned char *const dest,
                        const size_t dest_size,
                        rohc_packet_t *const packet_type,
                        int *const payload_offset)
{
	struct c_generic_context *g_context;
	struct sc_esp_context *esp_context;
	struct ip_packet ip2;
	const struct ip_packet *last_ip_header;
	const struct esphdr *esp;
	unsigned int ip_proto;
	int size;

	assert(context != NULL);
	assert(ip != NULL);
	assert(dest != NULL);
	assert(packet_type != NULL);

	assert(context->specific != NULL);
	g_context = (struct c_generic_context *) context->specific;
	assert(g_context->specific != NULL);
	esp_context = (struct sc_esp_context *) g_context->specific;

	ip_proto = ip_get_protocol(ip);
	if(ip_proto == ROHC_IPPROTO_IPIP || ip_proto == ROHC_IPPROTO_IPV6)
	{
		/* get the last IP header */
		if(!ip_get_inner_packet(ip, &ip2))
		{
			rohc_warning(context->compressor, ROHC_TRACE_COMP, context->profile->id,
			             "cannot create the inner IP header\n");
			return -1;
		}
		last_ip_header = &ip2;

		/* get the transport protocol */
		ip_proto = ip_get_protocol(last_ip_header);
	}
	else
	{
		/* only one single IP header, the last IP header is the first one */
		last_ip_header = ip;
	}

	if(ip_proto != ROHC_IPPROTO_ESP)
	{
		rohc_warning(context->compressor, ROHC_TRACE_COMP, context->profile->id,
		             "packet is not an ESP packet\n");
		return -1;
	}
	esp = (struct esphdr *) ip_get_next_layer(last_ip_header);

	/* encode the IP packet */
	size = c_generic_encode(context, ip, packet_size, dest, dest_size,
	                        packet_type, payload_offset);
	if(size < 0)
	{
		goto quit;
	}

	/* update the context with the new ESP header */
	if(g_context->tmp.packet_type == PACKET_IR ||
	   g_context->tmp.packet_type == PACKET_IR_DYN)
	{
		memcpy(&(esp_context->old_esp), esp, sizeof(struct esphdr));
	}

quit:
	return size;
}


/**
 * @brief Determine the SN value for the next packet
 *
 * Profile SN is the ESP SN.
 *
 * @param context   The compression context
 * @param outer_ip  The outer IP header
 * @param inner_ip  The inner IP header if it exists, NULL otherwise
 * @return          The SN
 */
static uint32_t c_esp_get_next_sn(const struct c_context *context,
                                  const struct ip_packet *outer_ip,
                                  const struct ip_packet *inner_ip)
{
	struct c_generic_context *g_context;
	struct esphdr *esp;

	g_context = (struct c_generic_context *) context->specific;

	/* get ESP header */
	if(g_context->tmp.nr_of_ip_hdr > 1)
	{
		esp = (struct esphdr *) ip_get_next_layer(inner_ip);
	}
	else
	{
		esp = (struct esphdr *) ip_get_next_layer(outer_ip);
	}

	return rohc_ntoh32(esp->sn);
}


/**
 * @brief Build the static part of the ESP header
 *
 * \verbatim

 Static part of ESP header (5.7.7.7):

    +---+---+---+---+---+---+---+---+
 1  /              SPI              /   4 octets
    +---+---+---+---+---+---+---+---+

 SPI = Security Parameters Index

\endverbatim
 *
 * @param context     The compression context
 * @param next_header The ESP header
 * @param dest        The rohc-packet-under-build buffer
 * @param counter     The current position in the rohc-packet-under-build buffer
 * @return            The new position in the rohc-packet-under-build buffer
 */
static int esp_code_static_esp_part(const struct c_context *context,
                                    const unsigned char *next_header,
                                    unsigned char *const dest,
                                    int counter)
{
	const struct esphdr *esp = (struct esphdr *) next_header;

	/* part 1 */
	rohc_comp_debug(context, "ESP SPI = 0x%08x\n", rohc_ntoh32(esp->spi));
	memcpy(&dest[counter], &esp->spi, sizeof(uint32_t));
	counter += sizeof(uint32_t);

	return counter;
}


/**
 * @brief Build the dynamic part of the ESP header
 *
 * \verbatim

 Dynamic part of ESP header (5.7.7.7):

    +---+---+---+---+---+---+---+---+
 1  /       Sequence Number         /   4 octets
    +---+---+---+---+---+---+---+---+

\endverbatim
 *
 * @param context     The compression context
 * @param next_header The ESP header
 * @param dest        The rohc-packet-under-build buffer
 * @param counter     The current position in the rohc-packet-under-build buffer
 * @return            The new position in the rohc-packet-under-build buffer
 */
static int esp_code_dynamic_esp_part(const struct c_context *context,
                                     const unsigned char *next_header,
                                     unsigned char *const dest,
                                     int counter)
{
	const struct esphdr *esp;

	assert(context != NULL);
	assert(next_header != NULL);
	assert(dest != NULL);

	esp = (struct esphdr *) next_header;

	/* part 1 */
	rohc_comp_debug(context, "ESP SN = 0x%08x\n", rohc_ntoh32(esp->sn));
	memcpy(&dest[counter], &esp->sn, sizeof(uint32_t));
	counter += sizeof(uint32_t);

	return counter;
}


/**
 * @brief Define the compression part of the ESP profile as described
 *        in the RFC 3095.
 */
struct c_profile c_esp_profile =
{
	ROHC_IPPROTO_ESP,    /* IP protocol */
	ROHC_PROFILE_ESP,    /* profile ID (see 8 in RFC 3095) */
	"ESP / Compressor",  /* profile description */
	c_esp_create,        /* profile handlers */
	c_generic_destroy,
	c_esp_check_profile,
	c_esp_check_context,
	c_esp_encode,
	c_generic_reinit_context,
	c_generic_feedback,
	c_generic_use_udp_port,
};

