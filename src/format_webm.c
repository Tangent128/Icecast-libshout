/* -*- c-basic-offset: 8; -*- */
/* webm.c: WebM data handler
 * $Id$
 *
 *  Copyright (C) 2002-2012 the Icecast team <team@icecast.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include <shout/shout.h>
#include "shout_private.h"

/* -- local datatypes -- */

/* unused state for a filter that passes
 * through data unmodified.
 */
/* TODO: Run data through the internal buffers
 * so we have a spot we can insert processing.
 */
/* TODO: incorporate EBML parsing & extract
 * timestamps from Clusters and SimpleBlocks.
 */
/* TODO: provide for "fake chaining", where
 * concatinated files have extra headers stripped
 * and Cluster / Block timestamps rewritten
 */
typedef struct _webm_t {

    /* buffer state */
    size_t input_position;
    size_t output_position;

    /* buffer storage */
    unsigned char input_buffer[SHOUT_BUFSIZE];
    unsigned char output_buffer[SHOUT_BUFSIZE];

} webm_t;

/* -- static prototypes -- */
static int send_webm(shout_t *self, const unsigned char *data, size_t len);
static void close_webm(shout_t *self);

static int webm_output(shout_t *self, webm_t *webm, const unsigned char *data, size_t len);

static size_t copy_possible(const void *src_base,
                            size_t *src_position,
                            size_t src_len,
                            void *target_base,
                            size_t *target_position,
                            size_t target_len);
static int flush_output(shout_t *self, webm_t *webm);

/* -- interface functions -- */
int shout_open_webm(shout_t *self)
{
    webm_t *webm_filter;

    /* Alloc WebM filter */
    if (!(webm_filter = (webm_t *)calloc(1, sizeof(webm_t)))) {
        return self->error = SHOUTERR_MALLOC;
    }

    /* configure shout state */
    self->format_data = webm_filter;

    self->send = send_webm;
    self->close = close_webm;

    return SHOUTERR_SUCCESS;
}

static int send_webm(shout_t *self, const unsigned char *data, size_t len)
{
    webm_t *webm = (webm_t *) self->format_data;
    /* IMPORTANT TODO: we just send the raw data. We need throttling. */

    self->error = SHOUTERR_SUCCESS;

    self->error = webm_output(self, webm, data, len);

    /* Squeeze out any possible output, unless we're failing */
    if(self->error == SHOUTERR_SUCCESS) {
        self->error = flush_output(self, webm);
    }

    return self->error;
}

static void close_webm(shout_t *self)
{
    webm_t *webm_filter = (webm_t *) self->format_data;
    if(webm_filter) free(webm_filter);
}

/* -- processing functions -- */

/* Queue the given data in the output buffer,
 * flushing as needed. Returns a status code
 * to allow detecting socket errors on a flush.
 */
static int webm_output(shout_t *self, webm_t *webm, const unsigned char *data, size_t len)
{
    size_t output_progress = 0;

    while(output_progress < len && self->error == SHOUTERR_SUCCESS)
    {
        copy_possible(data, &output_progress, len,
                      webm->output_buffer, &webm->output_position, SHOUT_BUFSIZE);

        if(webm->output_position == SHOUT_BUFSIZE) {
            self->error = flush_output(self, webm);
        }
    }

    return self->error;
}

/* -- utility functions -- */

/* Copies as much of the source buffer into the target
 * as will fit, and returns the actual size copied.
 * Updates position pointers to match.
 */
static size_t copy_possible(const void *src_base,
                            size_t *src_position,
                            size_t src_len,
                            void *target_base,
                            size_t *target_position,
                            size_t target_len)
{
    size_t src_space = src_len - *src_position;
    size_t target_space = target_len - *target_position;

    size_t to_copy = src_space;
    if(target_space < to_copy) to_copy = target_space;

    memcpy(target_base + *target_position, src_base + *src_position, to_copy);

    *src_position += to_copy;
    *target_position += to_copy;

    return to_copy;
}

/* Send currently buffered output to the server.
 * Output buffering is needed because parsing
 * and/or rewriting code may pass through small
 * chunks at a time, and we don't want to expend a
 * syscall on each one.
 * However, we do not want to leave sendable data
 * in the buffer before we return to the client and
 * potentially sleep, so this is called before
 * send_webm() returns.
 */
static int flush_output(shout_t *self, webm_t *webm)
{
    if(webm->output_position == 0) {
        return self->error = SHOUTERR_SUCCESS;
    }

    ssize_t ret = shout_send_raw(self, webm->output_buffer, webm->output_position);
    if (ret != (ssize_t) webm->output_position) {
        return self->error = SHOUTERR_SOCKET;
    }

    webm->output_position = 0;
    return self->error = SHOUTERR_SUCCESS;
}
