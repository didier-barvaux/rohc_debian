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
 * @file ip.c
 * @brief IP-agnostic packet
 * @author Didier Barvaux <didier.barvaux@toulouse.viveris.com>
 * @author Didier Barvaux <didier@barvaux.org>
 */

#include "ip.h"
#include "rohc_utils.h"

#ifndef __KERNEL__
#	include <string.h>
#endif
#include <assert.h>


/*
 * Generic IP functions (apply to both IPv4 and IPv6):
 */


/**
 * @brief Create an IP packet from raw data
 *
 * @param ip     OUT: The IP packet to create
 * @param packet The IP packet data
 * @param size   The length of the IP packet data
 * @return       Whether the IP packet was successfully created or not
 */
int ip_create(struct ip_packet *const ip,
              const unsigned char *packet,
              const unsigned int size)
{
	ip_version version;

	/* get the version of the IP packet
	 * (may be IP_UNKNOWN if packet is not IP) */
	if(!get_ip_version(packet, size, &version))
	{
		goto error;
	}

	ip->version = version;

	/* check packet's validity according to IP version */
	if(version == IPV4)
	{
		/* IPv4: packet must be at least 20-byte long (= min header length)
		 *       packet must be large enough for options if any (= 20 bytes)
		 *       packet length must be accurate with the Total Length field */

		if(size < sizeof(struct ipv4_hdr))
		{
			goto malformed;
		}

		/* copy the IPv4 header */
		memcpy(&ip->header.v4, packet, sizeof(struct ipv4_hdr));

		if(ip_get_hdrlen(ip) < sizeof(struct ipv4_hdr) ||
		   ip_get_hdrlen(ip) > size)
		{
			goto malformed;
		}

		if(ip_get_totlen(ip) != size)
		{
			goto malformed;
		}

		/* point to the whole IPv4 packet */
		ip->data = packet;
		ip->size = size;
	}
	else if(version == IPV6)
	{
		/* IPv6: packet must be at least 40-byte long (= header length)
		 *       packet length == header length + Payload Length field */

		if(size < sizeof(struct ipv6_hdr))
		{
			goto malformed;
		}

		/* copy the IPv6 header */
		memcpy(&ip->header.v6, packet, sizeof(struct ipv6_hdr));

		if(ip_get_totlen(ip) != size)
		{
			goto malformed;
		}

		/* point to the whole IPv6 packet */
		ip->data = packet;
		ip->size = size;
	}
	else /* IP_UNKNOWN */
	{
		goto unknown;
	}

	return 1;

malformed:
	/* manage the malformed IP packet */
	if(ip->version == IPV4)
	{
		ip->version = IPV4_MALFORMED;
	}
	else if(ip->version == IPV6)
	{
		ip->version = IPV6_MALFORMED;
	}
	else
	{
		goto error;
	}
	ip->data = packet;
	ip->size = size;
	return 1;

unknown:
	/* manage the IP packet that the library cannot handle as IPv4 nor IPv6
	 * as unknown data */
	ip->version = IP_UNKNOWN;
	ip->data = packet;
	ip->size = size;
	return 1;

error:
	return 0;
}


/**
 * @brief Get the IP raw data (header + payload)
 *
 * The function handles \ref ip_packet whose \ref ip_packet::version is
 * \ref IP_UNKNOWN.
 *
 * @param ip The IP packet to analyze
 * @return   The IP raw data (header + payload)
 */
const unsigned char * ip_get_raw_data(const struct ip_packet *const ip)
{
	return ip->data;
}


/**
 * @brief Get the inner IP packet (IP in IP)
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param outer The outer IP packet to analyze
 * @param inner The inner IP packet to create
 * @return      Whether the inner IP header is successfully created or not
 */
int ip_get_inner_packet(const struct ip_packet *const outer,
                        struct ip_packet *const inner)
{
	unsigned char *next_header;

	/* get the next header data in the IP packet (skip IP extensions) */
	next_header = ip_get_next_layer(outer);

	/* create an IP packet with the next header data */
	return ip_create(inner, next_header, ip_get_plen(outer));
}


/**
 * @brief Get the IP next header
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip   The IP packet to analyze
 * @param type OUT: The type of the next header
 * @return     The next header if successful, NULL otherwise
 */
unsigned char * ip_get_next_header(const struct ip_packet *const ip,
                                   uint8_t *const type)
{
	unsigned char *next_header;

	/* find the start of the next header data */
	if(ip->version == IPV4)
	{
		*type = ip->header.v4.protocol;
		next_header = ((unsigned char *) ip->data) + sizeof(struct ipv4_hdr);
	}
	else if(ip->version == IPV6)
	{
		*type = ip->header.v6.ip6_nxt;
		next_header = ((unsigned char *) ip->data) + sizeof(struct ipv6_hdr);
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		next_header = NULL;
		assert(0);
	}

	return next_header;
}


/**
 * @brief Get the next header (but skip IP extensions)
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip   The IP packet to analyze
 * @return     The next header that is not an IP extension
 */
unsigned char * ip_get_next_layer(const struct ip_packet *const ip)
{
	unsigned char *next_header;
	uint8_t next_header_type;
	uint8_t length;

	/* get the next header data in the IP packet */
	next_header = ip_get_next_header(ip, &next_header_type);

	/* skip IPv6 extensions */
	if(ip->version == IPV6)
	{
		/* TODO: stop when IP total length is reached */
		while(next_header_type == IPV6_EXT_HOP_BY_HOP ||
		      next_header_type == IPV6_EXT_DESTINATION ||
		      next_header_type == IPV6_EXT_ROUTING ||
		      next_header_type == IPV6_EXT_AUTH)
		{
			/* next header is an IPv4 extension header, skip it and
			   get the next header */
			next_header_type = next_header[0];
			length = next_header[1];
			next_header = next_header + (length + 1) * 8;
		}
	}

	return next_header;
}


/**
 * @brief Get the next extension header of IPv6 packets from
 *        an IPv6 header
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip   The IP packet to analyze
 * @param type OUT: The type of the next header
 *             If there is no next header the value must be ignored
 * @return     The next extension header,
 *             NULL if there is no extension
 */
unsigned char * ip_get_next_ext_from_ip(const struct ip_packet *const ip,
                                        uint8_t *const type)
{
	unsigned char *next_header;

	/* function does not handle non-IPv4/IPv6 packets */
	assert(ip->version != IP_UNKNOWN);

	if(ip->version != IPV6)
	{
		return NULL;
	}

	/* get the next header data in the IP packet */
	next_header = ip_get_next_header(ip, type);
	switch(*type)
	{
		case IPV6_EXT_HOP_BY_HOP:
		case IPV6_EXT_DESTINATION:
		case IPV6_EXT_ROUTING:
		case IPV6_EXT_AUTH:
			/* known extension headers */
			break;
		default:
			goto end;
	}

	return next_header;

end:
	return NULL;
}


/**
 * @brief Get the next extension header of IPv6 packets from
 *        another extension
 *
 * @param ext  The extension to analyse
 * @param type OUT: The type of the next header
 *             If there is no next header the value must be ignored
 * @return     The next extension header,
 *             NULL if there is no more extension
 */
unsigned char * ip_get_next_ext_from_ext(const unsigned char *const ext,
                                         uint8_t *const type)
{
	unsigned char *next_header;
	uint8_t length;

	*type = ext[0];

	switch(*type)
	{
		case IPV6_EXT_HOP_BY_HOP:
		case IPV6_EXT_DESTINATION:
		case IPV6_EXT_ROUTING:
		case IPV6_EXT_AUTH:
			/* known extension headers */
			length = ext[1];
			next_header = (unsigned char *)(ext + (length + 1) * 8);
			break;
		default:
			goto end;
	}

	return next_header;

end:
	return NULL;
}


/**
 * @brief Get the size of an IPv6 extension
 *
 * @param ext The extension
 * @return    The size of the extension
 */
unsigned short ip_get_extension_size(const unsigned char *const ext)
{
	const uint8_t ext_length = ext[1];

	return (ext_length + 1) * 8;
}


/**
 * @brief Get the size of the extension list
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip The packet to analyse
 * @return   The size of extension list
 */
unsigned short ip_get_total_extension_size(const struct ip_packet *const ip)
{
	unsigned char *ext;
	uint8_t next_hdr_type;
	unsigned short total_ext_size = 0;

	ext = ip_get_next_ext_from_ip(ip, &next_hdr_type);
	while(ext != NULL)
	{
		total_ext_size += ip_get_extension_size(ext);
		ext = ip_get_next_ext_from_ext(ext, &next_hdr_type);
	}

	return total_ext_size;
}


/**
 * @brief Whether the IP packet is an IP fragment or not
 *
 * The IP packet is a fragment if the  MF (More Fragments) bit is set
 * or the Fragment Offset field is non-zero.
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip The IP packet to analyze
 * @return   Whether the IP packet is an IP fragment or not if successful,
 *           0 otherwise
 */
int ip_is_fragment(const struct ip_packet *const ip)
{
	int is_fragment;

	if(ip->version == IPV4)
	{
		is_fragment = ((rohc_ntoh16(ip->header.v4.frag_off) & (~IP_DF)) != 0);
	}
	else if(ip->version == IPV6)
	{
		is_fragment = 0;
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		is_fragment = 0;
		assert(0);
	}

	return is_fragment;
}


/**
 * @brief Get the total length of an IP packet
 *
 * The function handles \ref ip_packet whose \ref ip_packet::version is
 * \ref IP_UNKNOWN.
 *
 * @param ip The IP packet to analyze
 * @return   The total length of the IP packet
 */
unsigned int ip_get_totlen(const struct ip_packet *const ip)
{
	uint16_t len;

	if(ip->version == IPV4)
	{
		len = rohc_ntoh16(ip->header.v4.tot_len);
	}
	else if(ip->version == IPV6)
	{
		len = sizeof(struct ipv6_hdr) + rohc_ntoh16(ip->header.v6.ip6_plen);
	}
	else /* IP_UNKNOWN */
	{
		len = ip->size;
	}

	return len;
}


/**
 * @brief Get the length of an IP header
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip The IP packet to analyze
 * @return   The length of the IP header if successful, 0 otherwise
 */
unsigned int ip_get_hdrlen(const struct ip_packet *const ip)
{
	unsigned int len;

	if(ip->version == IPV4)
	{
		len = ip->header.v4.ihl * 4;
	}
	else if(ip->version == IPV6)
	{
		len = sizeof(struct ipv6_hdr);
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		len = 0;
		assert(0);
	}

	return len;
}


/**
 * @brief Get the length of an IPv4/IPv6 payload
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip The IPv4/IPv6 packet to analyze
 * @return   The length of the IPv4/IPv6 payload if successful, 0 otherwise
 */
unsigned int ip_get_plen(const struct ip_packet *const ip)
{
	unsigned int len;

	if(ip->version == IPV4)
	{
		len = rohc_ntoh16(ip->header.v4.tot_len) - ip->header.v4.ihl * 4;
	}
	else if(ip->version == IPV6)
	{
		len = rohc_ntoh16(ip->header.v6.ip6_plen) -
		      ip_get_total_extension_size(ip);
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		len = 0;
		assert(0);
	}

	return len;
}


/**
 * @brief Get the IP version of an IP packet
 *
 * The function handles \ref ip_packet whose \ref ip_packet::version is
 * \ref IP_UNKNOWN.
 *
 * @param ip The IP packet to analyze
 * @return   The version of the IP packet
 */
ip_version ip_get_version(const struct ip_packet *const ip)
{
	return ip->version;
}


/**
 * @brief Set the IP version of an IP packet
 *
 * @param ip     The IP packet to modify
 * @param value  The version value
 */
void ip_set_version(struct ip_packet *const ip, const ip_version value)
{
	ip->version = value;
}


/**
 * @brief Get the protocol transported by an IP packet
 *
 * The protocol returned is the one transported by the last known IP extension
 * header if any is found.
 *
 * The function handles \ref ip_packet whose \ref ip_packet::version is
 * \ref IP_UNKNOWN. It always returns the special value 0.
 *
 * @param ip  The IP packet to analyze
 * @return    The protocol number that identify the protocol transported
 *            by the given IP packet, 0 if the packet is not IPv4 nor IPv6
 */
unsigned int ip_get_protocol(const struct ip_packet *const ip)
{
	uint8_t protocol;
	unsigned char *next_header;
	uint8_t next_header_type;

	if(ip->version == IPV4)
	{
		protocol = ip->header.v4.protocol;
	}
	else if(ip->version == IPV6)
	{
		next_header_type = ip->header.v6.ip6_nxt;
		switch(next_header_type)
		{
			case IPV6_EXT_HOP_BY_HOP:
			case IPV6_EXT_DESTINATION:
			case IPV6_EXT_ROUTING:
			case IPV6_EXT_AUTH:
				/* known extension headers */
				next_header = ((unsigned char *) ip->data) + sizeof(struct ipv6_hdr);
				protocol = ext_get_protocol(next_header);
				break;
			default:
				protocol = next_header_type;
				break;
		}
	}
	else /* IP_UNKNOWN */
	{
		protocol = 0;
	}

	return protocol;
}


/**
 * @brief Get the protocol transported by the last IPv6 extension
 *
 * @param ext The first extension
 * @return    The protocol number that identify the protocol transported
 *            by the given IP extension
 */
unsigned int ext_get_protocol(const unsigned char *const ext)
{
	uint8_t type;
	uint8_t length;
	uint8_t protocol;
	unsigned char *next_header;

	type = ext[0];
	length = ext[1];
	switch(type)
	{
		case IPV6_EXT_HOP_BY_HOP:
		case IPV6_EXT_DESTINATION:
		case IPV6_EXT_ROUTING:
		case IPV6_EXT_AUTH:
			/* known extension headers */
			next_header = ((unsigned char *) ext) + (length + 1) * 8;
			protocol = ext_get_protocol(next_header);
			break;
		default:
			protocol = type;
			break;
	}

	return protocol;
}


/**
 * @brief Set the protocol transported by an IP packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip     The IP packet to modify
 * @param value  The protocol value
 */
void ip_set_protocol(struct ip_packet *const ip, const uint8_t value)
{
	if(ip->version == IPV4)
	{
		ip->header.v4.protocol = value & 0xff;
	}
	else if(ip->version == IPV6)
	{
		ip->header.v6.ip6_nxt = value & 0xff;
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		assert(0);
	}
}


/**
 * @brief Get the IPv4 Type Of Service (TOS) or IPv6 Traffic Class (TC)
 *        of an IP packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip The IP packet to analyze
 * @return   The TOS or TC value if successful, 0 otherwise
 */
unsigned int ip_get_tos(const struct ip_packet *const ip)
{
	unsigned int tos;

	if(ip->version == IPV4)
	{
		tos = ip->header.v4.tos;
	}
	else if(ip->version == IPV6)
	{
		tos = IPV6_GET_TC(ip->header.v6);
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		tos = 0;
		assert(0);
	}

	return tos;
}


/**
 * @brief Set the IPv4 Type Of Service (TOS) or IPv6 Traffic Class (TC)
 *        of an IP packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip     The IP packet to modify
 * @param value  The TOS/TC value
 */
void ip_set_tos(struct ip_packet *const ip, const uint8_t value)
{
	if(ip->version == IPV4)
	{
		ip->header.v4.tos = value & 0xff;
	}
	else if(ip->version == IPV6)
	{
		IPV6_SET_TC(&ip->header.v6, value);
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		assert(0);
	}
}


/**
 * @brief Get the IPv4 Time To Live (TTL) or IPv6 Hop Limit (HL)
 *        of an IP packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip The IP packet to analyze
 * @return   The TTL or HL value if successful, 0 otherwise
 */
unsigned int ip_get_ttl(const struct ip_packet *const ip)
{
	unsigned int ttl;

	if(ip->version == IPV4)
	{
		ttl = ip->header.v4.ttl;
	}
	else if(ip->version == IPV6)
	{
		ttl = ip->header.v6.ip6_hlim;
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		ttl = 0;
		assert(0);
	}

	return ttl;
}


/**
 * @brief Set the IPv4 Time To Live (TTL) or IPv6 Hop Limit (HL)
 *        of an IP packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip     The IP packet to modify
 * @param value  The TTL/HL value
 */
void ip_set_ttl(struct ip_packet *const ip, const uint8_t value)
{
	if(ip->version == IPV4)
	{
		ip->header.v4.ttl = value & 0xff;
	}
	else if(ip->version == IPV6)
	{
		ip->header.v6.ip6_hlim = value & 0xff;
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		assert(0);
	}
}


/**
 * @brief Set the Source Address of an IP packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip     The IP packet to modify
 * @param value  The IP address value
 */
void ip_set_saddr(struct ip_packet *const ip, const unsigned char *value)
{
	if(ip->version == IPV4)
	{
		memcpy(&ip->header.v4.saddr, value, sizeof(uint32_t));
	}
	else if(ip->version == IPV6)
	{
		memcpy(&ip->header.v6.ip6_src, value, sizeof(struct ipv6_addr));
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		assert(0);
	}
}


/**
 * @brief Set the Destination Address of an IP packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is \ref IP_UNKNOWN.
 *
 * @param ip     The IP packet to modify
 * @param value  The IP address value
 */
void ip_set_daddr(struct ip_packet *const ip, const unsigned char *value)
{
	if(ip->version == IPV4)
	{
		memcpy(&ip->header.v4.daddr, value, sizeof(uint32_t));
	}
	else if(ip->version == IPV6)
	{
		memcpy(&ip->header.v6.ip6_dst, value, sizeof(struct ipv6_addr));
	}
	else
	{
		/* function does not handle non-IPv4/IPv6 packets */
		assert(0);
	}
}


/*
 * IPv4 specific functions:
 */


/**
 * @brief Get the IPv4 header
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV4.
 *
 * @param ip The IP packet to analyze
 * @return   The IP header
 */
const struct ipv4_hdr * ipv4_get_header(const struct ip_packet *const ip)
{
	assert(ip->version == IPV4);
	return &(ip->header.v4);
}


/**
 * @brief Get the IP-ID of an IPv4 packet
 *
 * The IP-ID value is returned as-is (ie. not automatically converted to
 * the host byte order).
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV4.
 *
 * @param ip  The IP packet to analyze
 * @return    The IP-ID
 */
uint16_t ipv4_get_id(const struct ip_packet *const ip)
{
	assert(ip->version == IPV4);
	return ipv4_get_id_nbo(ip, 1);
}


/**
 * @brief Get the IP-ID of an IPv4 packet in Network Byte Order
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV4.
 *
 * @param ip  The IP packet to analyze
 * @param nbo The NBO flag (if RND = 1, use NBO = 1)
 * @return    The IP-ID
 */
uint16_t ipv4_get_id_nbo(const struct ip_packet *const ip,
                         const unsigned int nbo)
{
	uint16_t id;

	assert(ip->version == IPV4);

	id = ip->header.v4.id;
	if(!nbo)
	{
		/* If IP-ID is not transmitted in Network Byte Order,
		 * swap the two bytes */
		id = swab16(id);
	}

	return id;
}


/**
 * @brief Set the IP-ID of an IPv4 packet
 *
 * The IP-ID value is set as-is (ie. not automatically converted to
 * the host byte order).
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV4.
 *
 * @param ip     The IP packet to modify
 * @param value  The IP-ID value
 */
void ipv4_set_id(struct ip_packet *const ip, const int value)
{
	assert(ip->version == IPV4);
	ip->header.v4.id = value & 0xffff;
}


/**
 * @brief Get the Don't Fragment (DF) bit of an IPv4 packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV4.
 *
 * @param ip The IP packet to analyze
 * @return   The DF bit
 */
int ipv4_get_df(const struct ip_packet *const ip)
{
	assert(ip->version == IPV4);
	return IPV4_GET_DF(ip->header.v4);
}


/**
 * @brief Set the Don't Fragment (DF) bit of an IPv4 packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV4.
 *
 * @param ip     The IP packet to modify
 * @param value  The value of the DF bit
 */
void ipv4_set_df(struct ip_packet *const ip, const int value)
{
	assert(ip->version == IPV4);
	IPV4_SET_DF(&ip->header.v4, value);
}


/**
 * @brief Get the source address of an IPv4 packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV4.
 *
 * @param ip The IPv4 packet to analyze
 * @return   The source address of the given IPv4 packet
 */
uint32_t ipv4_get_saddr(const struct ip_packet *const ip)
{
	assert(ip->version == IPV4);
	return ip->header.v4.saddr;
}


/**
 * @brief Get the destination address of an IPv4 packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV4.
 *
 * @param ip The IPv4 packet to analyze
 * @return   The source address of the given IPv4 packet
 */
uint32_t ipv4_get_daddr(const struct ip_packet *const ip)
{
	assert(ip->version == IPV4);
	return ip->header.v4.daddr;
}


/*
 * IPv6 specific functions:
 */


/**
 * @brief Get the IPv6 header
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV6.
 *
 * @param ip The IP packet to analyze
 * @return   The IP header if IPv6
 */
const struct ipv6_hdr * ipv6_get_header(const struct ip_packet *const ip)
{
	assert(ip->version == IPV6);
	return &(ip->header.v6);
}


/**
 * @brief Get the flow label of an IPv6 packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV6.
 *
 * @param ip The IPv6 packet to analyze
 * @return   The flow label of the given IPv6 packet
 */
uint32_t ipv6_get_flow_label(const struct ip_packet *const ip)
{
	assert(ip->version == IPV6);
	return IPV6_GET_FLOW_LABEL(ip->header.v6);
}


/**
 * @brief Set the flow label of an IPv6 packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV6.
 *
 * @param ip     The IPv6 packet to modify
 * @param value  The flow label value
 */
void ipv6_set_flow_label(struct ip_packet *const ip, const uint32_t value)
{
	assert(ip->version == IPV6);
	IPV6_SET_FLOW_LABEL(&ip->header.v6, value);
}


/**
 * @brief Get the source address of an IPv6 packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV6.
 *
 * @param ip The IPv6 packet to analyze
 * @return   The source address of the given IPv6 packet
 */
const struct ipv6_addr * ipv6_get_saddr(const struct ip_packet *const ip)
{
	assert(ip->version == IPV6);
	return &(ip->header.v6.ip6_src);
}


/**
 * @brief Get the destination address of an IPv6 packet
 *
 * The function does not handle \ref ip_packet whose \ref ip_packet::version
 * is not \ref IPV6.
 *
 * @param ip The IPv6 packet to analyze
 * @return   The source address of the given IPv6 packet
 */
const struct ipv6_addr * ipv6_get_daddr(const struct ip_packet *const ip)
{
	assert(ip->version == IPV6);
	return &(ip->header.v6.ip6_dst);
}


/**
 * Private functions used by the IP module:
 * (please do not use directly)
 */

/*
 * @brief Get the version of an IP packet
 *
 * If the function returns an error (packet too short for example), the value
 * of 'version' is unchanged.
 *
 * @param packet  The IP data
 * @param size    The length of the IP data
 * @param version OUT: the version of the IP packet: IPV4, IPV6 or IP_UNKNOWN
 * @return        Whether the given packet was successfully parsed or not
 */
int get_ip_version(const unsigned char *const packet,
                   const unsigned int size,
                   ip_version *const version)
{
	/* check the length of the packet */
	if(size <= 0)
	{
		goto error;
	}

	/* check the version field */
	switch((packet[0] >> 4) & 0x0f)
	{
		case 4:
			*version = IPV4;
			break;
		case 6:
			*version = IPV6;
			break;
		default:
			*version = IP_UNKNOWN;
			break;
	}

	return 1;

error:
	return 0;
}

