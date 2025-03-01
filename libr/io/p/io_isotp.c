/* radare - LGPL - Copyright 2021 - pancake */

#include <r_io.h>
#include <r_lib.h>
#include <r_cons.h>

#if __linux__

#include "../io_memory.h"
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define ISOTPURI "isotp://"

typedef struct {
	RSocket *sc;
	int count;
} RIOSocketData;

static void free_socketdata(RIOSocketData *sd) {
	if (sd) {
		r_socket_free (sd->sc);
		free (sd);
	}
}

static int __write(RIO *io, RIODesc *desc, const ut8 *buf, int count) {
	RIOMalloc *mal = (RIOMalloc*)desc->data;
	if (mal) {
		r_cons_break_push (NULL, NULL);
		RSocket *s = ((RIOSocketData*)(mal->data))->sc;
		return r_socket_write (s, buf, count);
	}
	return -1;
}

static int __read(RIO *io, RIODesc *desc, ut8 *buf, int count) {
	RIOMalloc *mal = (RIOMalloc*)desc->data;
	if (mal) {
		ut64 addr = mal->offset;
		r_cons_break_push (NULL, NULL);
		RIOSocketData *sdat = mal->data;
		RSocket *s = sdat->sc;
		ut8 *mem = malloc (4096);
		if (mem) {
			int c = r_socket_read (s, mem, 4096);
			if (c > 0) {
				int osz = mal->size;
				io_memory_resize (io, desc, mal->size + c);
				memcpy (mal->buf + osz, mem, c);
				io->corebind.cmdf (io->corebind.core, "f nread_%d %d %d",
					sdat->count, c, mal->size);
				io->corebind.cmdf (io->corebind.core, "omr 1 %d", mal->size);
				sdat->count++;
			}
			free (mem);
		}
		r_cons_break_pop ();
		mal->offset = addr;
		return io_memory_read (io, desc, buf, count);
	}
	return -1;
}

static int __close(RIODesc *desc) {
	R_FREE (desc->data);
	return 0;
}

static bool __check(RIO *io, const char *pathname, bool many) {
	return !strncmp (pathname, ISOTPURI, strlen (ISOTPURI));
}

static RIODesc *__open(RIO *io, const char *pathname, int rw, int mode) {
	if (r_sandbox_enable (false)) {
		eprintf ("The " ISOTPURI " uri is not permitted in sandbox mode.\n");
		return NULL;
	}
	if (!__check (io, pathname, 0)) {
		return NULL;
	}
	RIOMalloc *mal = R_NEW0 (RIOMalloc);
	if (!mal) {
		return NULL;
	}
	RIOSocketData *data = R_NEW0 (RIOSocketData);
	if (!mal || !data) {
		free (mal);
		free_socketdata (data);
		return NULL;
	}
	mal->data = data;
	mal->buf = calloc (1, 1);
	if (!mal->buf) {
		free (mal);
		free_socketdata (data);
		return NULL;
	}
	mal->size = 1;
	mal->offset = 0;
	pathname += strlen (ISOTPURI);

	if (*pathname == '?') {
		eprintf ("Usage: r2 isotp://interface/source/destination\n");
	} else {
		char *host = strdup (pathname);
		const char *port = "";
		char *slash = strchr (host, '/');
		if (slash) {
			*slash = 0;
			port = slash + 1;
		}
		data->sc = r_socket_new (false);
		if (!r_socket_connect (data->sc, host, port, R_SOCKET_PROTO_CAN, 0)) {
			eprintf ("Cannot connect\n");
			free (host);
			free_socketdata (data);
			return NULL;
		}
		r_socket_block_time (data->sc, false, 0, 0);
		free (host);
	}
	return r_io_desc_new (io, &r_io_plugin_isotp, pathname, R_PERM_RW | rw, mode, mal);
}

RIOPlugin r_io_plugin_isotp = {
	.name = "isotp",
	.desc = "Connect using the ISOTP protocol (isotp://interface/srcid/dstid)",
	.uris = ISOTPURI,
	.license = "MIT",
	.open = __open,
	.close = __close,
	.read = __read,
	.seek = io_memory_lseek,
	.check = __check,
	.write = __write,
};

#else
RIOPlugin r_io_plugin_isotp = {
	.name = "isotp",
	.desc = "shared memory resources (not for this platform)",
};
#endif

#ifndef R2_PLUGIN_INCORE
R_API RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_IO,
	.data = &r_io_plugin_isotp,
	.version = R2_VERSION
};
#endif
