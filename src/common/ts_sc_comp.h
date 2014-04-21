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
 * @file ts_sc_comp.h
 * @brief Scaled RTP Timestamp encoding
 * @author David Moreau from TAS
 * @author Didier Barvaux <didier.barvaux@toulouse.viveris.com>
 * @author Didier Barvaux <didier@barvaux.org>
 *
 * See section 4.5.3 of RFC 3095 for details about Scaled RTP Timestamp
 * encoding.
 */

#ifndef TS_SC_COMP_H
#define TS_SC_COMP_H

#include "wlsb.h"
#include "rohc_traces.h"

#ifdef __KERNEL__
#	include <linux/types.h>
#else
#	include <stdbool.h>
#endif

#include "dllexport.h"


/**
 * @brief State of scaled RTP Timestamp encoding
 *
 * See section 4.5.3 of RFC 3095 for details about Scaled RTP Timestamp
 * encoding.
 */
typedef enum
{
	/// Initialization state (TS_STRIDE value not yet computed)
	INIT_TS = 1,
	/// Initialization state (TS_STRIDE value computed and sent)
	INIT_STRIDE = 2,
	/// Compression state (TS_SCALED value computed and sent)
	SEND_SCALED = 3,
} ts_sc_state;


/**
 * @brief Scaled RTP Timestamp encoding object
 *
 * See section 4.5.3 of RFC 3095 for details about Scaled RTP Timestamp
 * encoding.
 */
struct ts_sc_comp
{
	/// The TS_STRIDE value
	uint32_t ts_stride;

	/// The TS_SCALED value
	uint32_t ts_scaled;
	/// A window used to encode the TS_SCALED value
	struct c_wlsb *scaled_window;

	/// The TS_OFFSET value
	uint32_t ts_offset;

	/// The timestamp (TS)
	uint32_t ts;
	/// The previous timestamp
	uint32_t old_ts;

	/// The sequence number (SN)
	uint16_t sn;
	/// The previous sequence number
	uint16_t old_sn;

	/// Whether timestamp is deducible from SN or not
	bool is_deducible;

	/// The state of the scaled RTP Timestamp encoding object
	ts_sc_state state;
	/** Whether old SN/TS values are initialized or not */
	bool are_old_val_init;
	/// The number of packets sent in state INIT_STRIDE
	size_t nr_init_stride_packets;

	/// The difference between old and current TS
	uint32_t ts_delta;

	/// The callback function used to get log messages
	rohc_trace_callback_t trace_callback;
};



/*
 * Function prototypes
 */

int ROHC_EXPORT c_create_sc(struct ts_sc_comp *const ts_sc,
                            const size_t wlsb_window_width,
                            rohc_trace_callback_t callback);
void ROHC_EXPORT c_destroy_sc(struct ts_sc_comp *const ts_sc);

void ROHC_EXPORT c_add_ts(struct ts_sc_comp *const ts_sc,
                          const uint32_t ts,
                          const uint16_t sn);

bool ROHC_EXPORT nb_bits_scaled(const struct ts_sc_comp ts_sc,
                                size_t *const bits_nr);

void ROHC_EXPORT add_scaled(const struct ts_sc_comp *const ts_sc,
                            uint16_t sn);

uint32_t ROHC_EXPORT get_ts_stride(const struct ts_sc_comp ts_sc);
uint32_t ROHC_EXPORT get_ts_scaled(const struct ts_sc_comp ts_sc);

bool ROHC_EXPORT rohc_ts_sc_is_deducible(const struct ts_sc_comp ts_sc);

#endif

