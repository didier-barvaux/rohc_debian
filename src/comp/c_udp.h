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
 * @file c_udp.h
 * @brief ROHC compression context for the UDP profile.
 * @author Didier Barvaux <didier.barvaux@toulouse.viveris.com>
 * @author The hackers from ROHC for Linux
 */

#ifndef C_UDP_H
#define C_UDP_H

#include "rohc_comp_internals.h"

#include <stdint.h>
#include <stdbool.h>


/*
 * Function prototypes.
 */

bool c_udp_check_profile(const struct rohc_comp *const comp,
                         const struct ip_packet *const outer_ip,
                         const struct ip_packet *const inner_ip,
                         const uint8_t protocol,
                         rohc_ctxt_key_t *const ctxt_key);

bool c_udp_check_context(const struct c_context *context,
                         const struct ip_packet *ip);


int udp_code_uo_remainder(const struct c_context *context,
                          const unsigned char *next_header,
                          unsigned char *const dest,
                          int counter);

int udp_code_static_udp_part(const struct c_context *context,
                             const unsigned char *next_header,
                             unsigned char *const dest,
                             int counter);

#endif

