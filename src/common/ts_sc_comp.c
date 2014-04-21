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
 * @file ts_sc_comp.c
 * @brief Scaled RTP Timestamp encoding
 * @author David Moreau from TAS
 * @author Didier Barvaux <didier.barvaux@toulouse.viveris.com>
 * @author Didier Barvaux <didier@barvaux.org>
 */

#include "ts_sc_comp.h"
#include "sdvl.h"
#include "rohc_traces_internal.h"

#include <stdlib.h> /* for abs(3) */
#include <assert.h>


/** Print debug messages for the ts_sc_comp module */
#define ts_debug(entity_struct, format, ...) \
	rohc_debug(entity_struct, ROHC_TRACE_COMP, ROHC_PROFILE_GENERAL, \
	           format, ##__VA_ARGS__)


/**
 * @brief Create the ts_sc_comp object
 *
 * @param ts_sc              The ts_sc_comp object to create
 * @param wlsb_window_width  The width of the W-LSB sliding window to use
 *                           for TS_STRIDE (must be > 0)
 * @param callback           The trace callback
 * @return                   1 if creation is successful, 0 otherwise
 */
int c_create_sc(struct ts_sc_comp *const ts_sc,
                const size_t wlsb_window_width,
                rohc_trace_callback_t callback)
{
	assert(ts_sc != NULL);
	assert(wlsb_window_width > 0);

	ts_sc->ts_stride = 0;
	ts_sc->ts_scaled = 0;
	ts_sc->ts_offset = 0;
	ts_sc->old_ts = 0;
	ts_sc->ts = 0;
	ts_sc->ts_delta = 0;
	ts_sc->old_sn = 0;
	ts_sc->sn = 0;
	ts_sc->is_deducible = 0;
	ts_sc->state = INIT_TS;
	ts_sc->are_old_val_init = false;
	ts_sc->nr_init_stride_packets = 0;
	ts_sc->trace_callback = callback;

	ts_sc->scaled_window = c_create_wlsb(32, wlsb_window_width,
	                                     ROHC_LSB_SHIFT_RTP_TS);
	if(ts_sc->scaled_window == NULL)
	{
		rohc_error(ts_sc, ROHC_TRACE_COMP, ROHC_PROFILE_GENERAL,
		           "cannot create a W-LSB window for TS scaled\n");
		goto error;
	}

	return 1;

error:
	return 0;
}


/**
 * @brief Destroy the ts_sc_comp object
 *
 * @param ts_sc        The ts_sc_comp object to destroy
 */
void c_destroy_sc(struct ts_sc_comp *const ts_sc)
{
	assert(ts_sc != NULL);
	assert(ts_sc->scaled_window != NULL);
	c_destroy_wlsb(ts_sc->scaled_window);
}


/**
 * @brief Store the new TS, calculate new values and update the state
 *
 * @param ts_sc        The ts_sc_comp object
 * @param ts           The timestamp to add
 * @param sn           The sequence number of the RTP packet
 */
void c_add_ts(struct ts_sc_comp *const ts_sc, const uint32_t ts, const uint16_t sn)
{
	assert(ts_sc != NULL);

	ts_debug(ts_sc, "Timestamp = %u\n", ts);

	/* consider that TS bits are not deducible by default */
	ts_sc->is_deducible = false;

	/* we save the old value */
	ts_sc->old_ts = ts_sc->ts;
	ts_sc->old_sn = ts_sc->sn;

	/* we store the new value */
	ts_sc->ts = ts;
	ts_sc->sn = sn;

	/* if we had no old values, TS_STRIDE cannot be computed yet */
	if(!ts_sc->are_old_val_init)
	{
		assert(ts_sc->state == INIT_TS);
		ts_debug(ts_sc, "TS_STRIDE cannot be computed, stay in INIT_TS state\n");
		ts_sc->are_old_val_init = true;
		return;
	}

	/* compute the absolute delta between new and old TS */
	/* abs() on unsigned 32-bit values seems to be a problem sometimes */
	if(ts_sc->ts >= ts_sc->old_ts)
	{
		ts_sc->ts_delta = ts_sc->ts - ts_sc->old_ts;
	}
	else
	{
		ts_sc->ts_delta = ts_sc->old_ts - ts_sc->ts;
	}
	ts_debug(ts_sc, "TS delta = %u\n", ts_sc->ts_delta);

	/* go back to INIT_TS state if TS is constant */
	if(ts_sc->ts_delta == 0)
	{
		ts_debug(ts_sc, "TS is constant, go in INIT_TS state\n");
		ts_sc->state = INIT_TS;
		return;
	}

	/* go back to INIT_TS state if TS_STRIDE cannot be SDVL-encoded */
	if(!sdvl_can_value_be_encoded(ts_sc->ts_delta))
	{
		/* TS_STRIDE is too large for SDVL encoding */
		ts_debug(ts_sc, "TS_STRIDE is too large for SDVL encoding, "
		         "go in INIT_TS state\n");
		ts_sc->state = INIT_TS;
		return;
	}

	/* TS_STRIDE can be computed, so leave INIT_TS state */
	if(ts_sc->state == INIT_TS)
	{
		ts_debug(ts_sc, "TS_STRIDE can be computed, go to INIT_STRIDE state\n");
		ts_sc->state = INIT_STRIDE;
		ts_sc->nr_init_stride_packets = 0;
	}

	if(ts_sc->state == INIT_STRIDE)
	{
		/* TS is changing and TS_STRIDE can be computed but TS_STRIDE was
		 * not transmitted enough times to the decompressor to be used */
		ts_debug(ts_sc, "state INIT_STRIDE\n");

		/* reset INIT_STRIDE counter if TS_STRIDE/TS_OFFSET changed */
		if(ts_sc->ts_delta != ts_sc->ts_stride ||
		   (ts_sc->ts % ts_sc->ts_delta) != ts_sc->ts_offset)
		{
			ts_debug(ts_sc, "TS_STRIDE and/or TS_OFFSET changed\n");
			ts_sc->nr_init_stride_packets = 0;
		}

		/* compute TS_STRIDE, TS_OFFSET and TS_SCALED */
		ts_sc->ts_stride = ts_sc->ts_delta;
		ts_debug(ts_sc, "TS_STRIDE = %u\n", ts_sc->ts_stride);
		assert(ts_sc->ts_stride != 0);
		ts_sc->ts_offset = ts_sc->ts % ts_sc->ts_stride;
		ts_debug(ts_sc, "TS_OFFSET = %u modulo %u = %u\n",
		         ts_sc->ts, ts_sc->ts_stride, ts_sc->ts_offset);
		assert(ts_sc->ts_stride != 0);
		ts_sc->ts_scaled = (ts_sc->ts - ts_sc->ts_offset) / ts_sc->ts_stride;
		ts_debug(ts_sc, "TS_SCALED = (%u - %u) / %u = %u\n", ts_sc->ts,
		         ts_sc->ts_offset, ts_sc->ts_stride, ts_sc->ts_scaled);
	}
	else if(ts_sc->state == SEND_SCALED)
	{
		const uint32_t old_scaled = ts_sc->ts_scaled;
		const uint32_t old_offset = ts_sc->ts_offset;

		/* TS is changing, TS_STRIDE can be computed, and TS_STRIDE was
		 * transmitted enough times to the decompressor to be used */
		ts_debug(ts_sc, "state SEND_SCALED\n");

		/* does TS_STRIDE changed? */
		ts_debug(ts_sc, "TS_STRIDE calculated = %u\n", ts_sc->ts_delta);
		ts_debug(ts_sc, "previous TS_STRIDE = %u\n", ts_sc->ts_stride);
		if(ts_sc->ts_delta != ts_sc->ts_stride)
		{
			assert(ts_sc->ts_stride != 0);
			if((ts_sc->ts_delta % ts_sc->ts_stride) != 0)
			{
				/* TS delta changed and is not a multiple of previous TS_STRIDE:
				 * record the new value as TS_STRIDE and transmit it several
				 * times for robustness purposes */
				ts_debug(ts_sc, "/!\\ TS_STRIDE changed and is not a multiple "
				         "of previous TS_STRIDE, so change TS_STRIDE and "
				         "transmit it several times along all TS bits "
				         "(probably a clock resync at source)\n");
				ts_sc->state = INIT_STRIDE;
				ts_sc->nr_init_stride_packets = 0;
				ts_debug(ts_sc, "state -> INIT_STRIDE\n");
				ts_sc->ts_stride = ts_sc->ts_delta;
			}
			else if((ts_sc->ts_delta / ts_sc->ts_stride) != (ts_sc->sn - ts_sc->old_sn))
			{
				/* TS delta changed but is a multiple of previous TS_STRIDE:
				 * do not change TS_STRIDE, but transmit all TS bits several
				 * times for robustness purposes */
				ts_debug(ts_sc, "/!\\ TS delta changed but is a multiple of "
				         "previous TS_STRIDE, so do not change TS_STRIDE, but "
				         "retransmit it several times along all TS bits "
				         "(probably a RTP TS jump at source)\n");
				ts_sc->state = INIT_STRIDE;
				ts_sc->nr_init_stride_packets = 0;
				ts_debug(ts_sc, "state -> INIT_STRIDE\n");
			}
			else
			{
				/* do not change TS_STRIDE, probably a packet loss */
				ts_debug(ts_sc, "/!\\ TS delta changed, is a multiple of "
				         "previous TS_STRIDE and follows SN changes, so do "
				         "not change TS_STRIDE (probably a packet loss)\n");
			}
		}
		ts_debug(ts_sc, "TS_STRIDE = %u\n", ts_sc->ts_stride);

		/* update TS_OFFSET is needed */
		assert(ts_sc->ts_stride != 0);
		ts_sc->ts_offset = ts_sc->ts % ts_sc->ts_stride;
		ts_debug(ts_sc, "TS_OFFSET = %u modulo %u = %u\n",
		         ts_sc->ts, ts_sc->ts_stride, ts_sc->ts_offset);

		/* compute TS_SCALED */
		assert(ts_sc->ts_stride != 0);
		ts_sc->ts_scaled = (ts_sc->ts - ts_sc->ts_offset) / ts_sc->ts_stride;
		ts_debug(ts_sc, "TS_SCALED = (%u - %u) / %u = %u\n", ts_sc->ts,
		         ts_sc->ts_offset, ts_sc->ts_stride, ts_sc->ts_scaled);

		/* could TS_SCALED be deduced from SN? */
		if(ts_sc->state == SEND_SCALED &&
		   (ts_sc->ts_scaled - old_scaled) == (ts_sc->sn - ts_sc->old_sn))
		{
			ts_debug(ts_sc, "TS can be deducted from SN (old TS_SCALED = %u, "
			         "new TS_SCALED = %u, old SN = %u, new SN = %u)\n",
			         old_scaled, ts_sc->ts_scaled, ts_sc->old_sn, ts_sc->sn);
			ts_sc->is_deducible = 1;
		}
		else
		{
			ts_debug(ts_sc, "TS can not be deducted from SN (old TS_SCALED = %u, "
			         "new TS_SCALED = %u, old SN = %u, new SN = %u)\n",
			         old_scaled, ts_sc->ts_scaled, ts_sc->old_sn, ts_sc->sn);
			ts_sc->is_deducible = 0;
		}

		/* Wraparound (See RFC 4815 Section 4.4.3) */
		if(ts_sc->ts < ts_sc->old_ts)
		{
			ts_debug(ts_sc, "TS wraparound detected\n");
			if(old_offset != ts_sc->ts_offset)
			{
				ts_debug(ts_sc, "TS_OFFSET changed, re-initialize TS_STRIDE\n");
				ts_sc->state = INIT_STRIDE;
				ts_sc->nr_init_stride_packets = 0;
			}
			else
			{
				ts_debug(ts_sc, "TS_OFFSET is unchanged\n");
			}
		}
	}
	else
	{
		/* invalid state, should not happen */
		ts_debug(ts_sc, "invalid state (%d), should not happen\n", ts_sc->state);
		assert(0);
		return;
	}
}


/**
 * @brief Return the number of bits needed to encode TS_SCALED
 *
 * @param ts_sc    The ts_sc_comp object
 * @param bits_nr  OUT: The number of bits needed
 * @return         true in case of success,
 *                 false if the minimal number of bits can not be found
 */
bool nb_bits_scaled(const struct ts_sc_comp ts_sc, size_t *const bits_nr)
{
	return wlsb_get_k_32bits(ts_sc.scaled_window, ts_sc.ts_scaled, bits_nr);
}


/**
 * @brief Add a new TS_SCALED value to the ts_sc_comp object
 *
 * @param ts_sc        The ts_sc_comp object
 * @param sn           The Sequence Number
 */
void add_scaled(const struct ts_sc_comp *const ts_sc, uint16_t sn)
{
	assert(ts_sc != NULL);
	c_add_wlsb(ts_sc->scaled_window, sn, ts_sc->ts_scaled);
}


/**
 * @brief Return the TS_STRIDE value
 *
 * @param ts_sc        The ts_sc_comp object
 * @return             TS_STRIDE value
 */
uint32_t get_ts_stride(const struct ts_sc_comp ts_sc)
{
	return ts_sc.ts_stride;
}


/**
 * @brief Return the TS_SCALED value
 *
 * @param ts_sc        The ts_sc_comp object
 * @return             The TS_SCALED value
 */
uint32_t get_ts_scaled(const struct ts_sc_comp ts_sc)
{
	return ts_sc.ts_scaled;
}


/**
 * @brief Whether TimeStamp (TS) is deducible from the Sequence Number (SN)
 *        or not
 *
 * @param ts_sc  The TS SCALED compression context
 * @return       true if TS is deducible from SN, false otherwise
 */
bool rohc_ts_sc_is_deducible(const struct ts_sc_comp ts_sc)
{
	return ts_sc.is_deducible;
}

