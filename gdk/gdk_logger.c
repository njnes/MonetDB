/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2020 MonetDB B.V.
 */

#include "monetdb_config.h"
#include "gdk.h"
#include "gdk_private.h"
#include "gdk_logger.h"
#include "gdk_logger_internals.h"
#include <string.h>

static gdk_return logger_add_bat(logger *lg, BAT *b, log_id id);
static gdk_return logger_del_bat(logger *lg, log_bid bid);
/*
 * The logger uses a directory to store its log files. One master log
 * file stores information about the version of the logger and the
 * type mapping it uses. This file is a simple ascii file with the
 * following format:
 *  {6DIGIT-VERSION\n[id,type_name\n]*}
 * The transaction log files have a binary format.
 */

#define LOG_START	0
#define LOG_END		1
#define LOG_UPDATE_BULK	2
#define LOG_UPDATE	3
#define LOG_CREATE	4
#define LOG_DESTROY	5
#define LOG_SEQ		6
#define LOG_CLEAR	7
#define LOG_ROW		8 /* per row relative small log entry */

#ifdef NATIVE_WIN32
#define getfilepos _ftelli64
#else
#ifdef HAVE_FSEEKO
#define getfilepos ftello
#else
#define getfilepos ftell
#endif
#endif

#define BATSIZE 0

#define LOG_DISABLED(lg) ((lg)->debug&128)

static const char *log_commands[] = {
	"LOG_START",
	"LOG_END",
	"LOG_UPDATE_BULK",
	"LOG_UPDATE",
	"LOG_CREATE",
	"LOG_DESTROY",
	"LOG_SEQ",
	"LOG_CLEAR",
	"LOG_ROW",
};

typedef struct logaction {
	int type;		/* type of change */
	lng nr;
	int tt;
	lng id;
	lng offset;
	log_id cid;		/* id of object */
	BAT *b;			/* temporary bat with changes */
	BAT *uid;		/* temporary bat with bun positions to update */
} logaction;

/* during the recover process a number of transactions could be active */
typedef struct trans {
	int tid;		/* transaction id */
	int sz;			/* sz of the changes array */
	int nr;			/* nr of changes */

	logaction *changes;

	struct trans *tr;
} trans;

typedef struct logformat_t {
	char flag;
	int id;
} logformat;

typedef enum {LOG_OK, LOG_EOF, LOG_ERR} log_return;

static gdk_return bm_commit(logger *lg);
static gdk_return tr_grow(trans *tr);

static char
find_type(logger *lg, int tpe)
{
	BATiter cni = bat_iterator(lg->type_nr);
	char *res = (char*)Tloc(lg->type_id, 0);
	BUN p;

	/* type should be there !*/
	if (BAThash(lg->type_nr) == GDK_SUCCEED) {
		HASHloop_int(cni, cni.b->thash, p, &tpe) {
			return res[p];
		}
	}
	return -1;
}

static int
find_type_nr(logger *lg, char tpe)
{
	BATiter cni = bat_iterator(lg->type_id);
	int *res = (int*)Tloc(lg->type_nr, 0);
	BUN p;

	/* type should be there !*/
	if (BAThash(lg->type_id) == GDK_SUCCEED) {
		HASHloop_bte(cni, cni.b->thash, p, &tpe) {
			return res[p];
		}
	}
	return -1;
}

static BUN
log_find(BAT *b, BAT *d, int val)
{
	BATiter cni = bat_iterator(b);
	BUN p;

	assert(b->ttype == TYPE_int);
	assert(d->ttype == TYPE_oid);
	if (BAThash(b) == GDK_SUCCEED) {
		HASHloop_int(cni, cni.b->thash, p, &val) {
			oid pos = p;
			if (BUNfnd(d, &pos) == BUN_NONE)
				return p;
		}
	} else {		/* unlikely: BAThash failed */
		BUN q;
		int *t = (int *) Tloc(b, 0);

		for (p = 0, q = BUNlast(b); p < q; p++) {
			if (t[p] == val) {
				oid pos = p;
				if (BUNfnd(d, &pos) == BUN_NONE)
					return p;
			}
		}
	}
	return BUN_NONE;
}

static void
logbat_destroy(BAT *b)
{
	if (b)
		BBPunfix(b->batCacheid);
}

static BAT *
logbat_new(int tt, BUN size, role_t role)
{
	BAT *nb = COLnew(0, tt, size, role);

	if (nb) {
		if (role == PERSISTENT)
			BATmode(nb, false);
	} else {
		fprintf(stderr, "!ERROR: logbat_new: creating new BAT[void:%s]#" BUNFMT " failed\n", ATOMname(tt), size);
	}
	return nb;
}

static int
log_read_format(logger *l, logformat *data)
{
	assert(!l->inmemory);
	return mnstr_read(l->input_log, &data->flag, 1, 1) == 1 &&
		mnstr_readInt(l->input_log, &data->id) == 1;
}

static gdk_return
log_write_format(logger *lg, logformat *data)
{
	assert(!lg->inmemory);
	if (mnstr_write(lg->output_log, &data->flag, 1, 1) == 1 &&
	    mnstr_writeInt(lg->output_log, data->id))
		return GDK_SUCCEED;
	fprintf(stderr, "!ERROR: log_write_format: write failed\n");
	return GDK_FAIL;
}

static log_return
log_read_clear(logger *lg, trans *tr, log_id id)
{
	if (lg->debug & 1)
		fprintf(stderr, "#logger found log_read_clear %d\n", id);
	if (tr_grow(tr) != GDK_SUCCEED)
		return LOG_ERR;
	tr->changes[tr->nr].type = LOG_CLEAR;
	tr->changes[tr->nr].cid = id;
	tr->nr++;
	return LOG_OK;
}

static gdk_return
la_bat_clear(logger *lg, logaction *la)
{
	log_bid bid = logger_find_bat(lg, la->cid);
	BAT *b;

	if (lg->debug & 1)
		fprintf(stderr, "#la_bat_clear %d\n", la->cid);

	b = BATdescriptor(bid);
	if (b) {
		restrict_t access = (restrict_t) b->batRestricted;
		b->batRestricted = BAT_WRITE;
		BATclear(b, true);
		b->batRestricted = access;
		logbat_destroy(b);
	}
	return GDK_SUCCEED;
}

static log_return
log_read_seq(logger *lg, logformat *l)
{
	int seq = l->id;
	lng val;
	BUN p;

	assert(!lg->inmemory);
	if (!mnstr_readLng(lg->input_log, &val)) {
		fprintf(stderr, "!ERROR: log_read_seq: read failed\n");
		return LOG_EOF;
	}

	if ((p = log_find(lg->seqs_id, lg->dseqs, seq)) != BUN_NONE &&
	    p >= lg->seqs_id->batInserted) {
		if (BUNinplace(lg->seqs_val, p, &val, false) != GDK_SUCCEED)
			return LOG_ERR;
	} else {
		if (p != BUN_NONE) {
			oid pos = p;
			if (BUNappend(lg->dseqs, &pos, false) != GDK_SUCCEED)
				return LOG_ERR;
		}
		if (BUNappend(lg->seqs_id, &seq, false) != GDK_SUCCEED ||
		    BUNappend(lg->seqs_val, &val, false) != GDK_SUCCEED)
			return LOG_ERR;
	}
	return LOG_OK;
}

#if 0
static gdk_return
log_write_id(logger *lg, int id)
{
	assert(!lg->inmemory);
	assert(id >= 0);
	if (mnstr_writeInt(lg->output_log, id))
		return GDK_SUCCEED;
	fprintf(stderr, "!ERROR: log_write_id: write failed\n");
	return GDK_FAIL;
}

static int
log_read_id(logger *lg, log_id *id)
{
	assert(!lg->inmemory);
	if (mnstr_readInt(lg->input_log, id) != 1) {
		fprintf(stderr, "!ERROR: log_read_id: read failed\n");
		return LOG_EOF;
	}
	return LOG_OK;
}
#endif

static log_return
log_read_updates(logger *lg, trans *tr, logformat *l, log_id id, lng offset)
{
	log_return res = LOG_OK;
	lng nr, pnr;
	char type_id = -1;
	int tpe;

	assert(!lg->inmemory);
	if (lg->debug & 1)
		fprintf(stderr, "#logger found log_read_updates %d %s\n", id, l->flag == LOG_UPDATE ? "update" : "update_buld");

	if (!mnstr_readLng(lg->input_log, &nr) ||
	    mnstr_read(lg->input_log, &type_id, 1, 1) != 1) 
		return LOG_ERR;

	pnr = nr;
	tpe = find_type_nr(lg, type_id);
	if (tpe >= 0) {
		BAT *uid = NULL;
		BAT *r = NULL;
		void *(*rt) (ptr, stream *, size_t) = BATatoms[tpe].atomRead;
		void *tv = NULL;

		if (ATOMstorage(tpe) < TYPE_str)
			tv = lg->buf;

		assert(nr <= (lng) BUN_MAX);
		if (l->flag == LOG_UPDATE) {
			uid = COLnew(0, TYPE_oid, (BUN)nr, PERSISTENT);
			if (uid == NULL) {
				return LOG_ERR;
			}
		}
		r = COLnew(0, tpe, (BUN) nr, PERSISTENT);
		if (r == NULL) {
			BBPreclaim(uid);
			return LOG_ERR;
		}

		if (l->flag == LOG_UPDATE_BULK) {
	    		if (!mnstr_readLng(lg->input_log, &offset)) {
				BBPreclaim(r);
				return LOG_ERR;
			}
			for (; res == LOG_OK && nr > 0; nr--) {
				void *t = rt(tv, lg->input_log, 1);

				if (t == NULL) {
					/* see if failure was due to
					 * malloc or something less
					 * serious (in the current
					 * context) */
					if (strstr(GDKerrbuf, "alloc") == NULL)
						res = LOG_EOF;
					else
						res = LOG_ERR;
					break;
				}
				if (r && BUNappend(r, t, true) != GDK_SUCCEED)
					res = LOG_ERR;
				if (t != tv)
					GDKfree(t);
			}
		} else {
			void *(*rh) (ptr, stream *, size_t) = BATatoms[TYPE_oid].atomRead;
			void *hv = ATOMnil(TYPE_oid);

			if (hv == NULL)
				res = LOG_ERR;

			for (; res == LOG_OK && nr > 0; nr--) {
				void *h = rh(hv, lg->input_log, 1);
				void *t = rt(tv, lg->input_log, 1);

				if (h == NULL)
					res = LOG_EOF;
				else if (t == NULL) {
					if (strstr(GDKerrbuf, "malloc") == NULL)
						res = LOG_EOF;
					else
						res = LOG_ERR;
				} else if ((uid && BUNappend(uid, h, true) != GDK_SUCCEED) ||
				   	(r && BUNappend(r, t, true) != GDK_SUCCEED))
					res = LOG_ERR;
				if (t != tv)
					GDKfree(t);
			}
			GDKfree(hv);
		}
		if (tv != lg->buf)
			GDKfree(tv);

		if (res == LOG_OK) {
			if (tr_grow(tr) == GDK_SUCCEED) {
				tr->changes[tr->nr].type = l->flag;
				tr->changes[tr->nr].nr = pnr;
				tr->changes[tr->nr].tt = tpe;
				tr->changes[tr->nr].cid = id;
				tr->changes[tr->nr].offset = offset;
				tr->changes[tr->nr].b = r;
				tr->changes[tr->nr].uid = uid;
				tr->nr++;
			} else {
				res = LOG_ERR;
			}
		}
		if (res == LOG_ERR) {
			if (r) 
				BBPreclaim(r);
			if (uid) 
				BBPreclaim(uid);
		}
	} else {
		/* bat missing ERROR or ignore ? currently error. */
		res = LOG_ERR;
	}
	return res;
}


static gdk_return
la_bat_update_count(logger *lg, log_id id, lng cnt)
{
	BATiter cni = bat_iterator(lg->catalog_id);
	BUN p;

	if (BAThash(lg->catalog_id) == GDK_SUCCEED) {
		HASHloop_int(cni, cni.b->thash, p, &id) {
			lng ocnt = *(lng*) Tloc(lg->catalog_cnt, p);
			if (ocnt < cnt && BUNinplace(lg->catalog_cnt, p, &cnt, false) != GDK_SUCCEED)
				return GDK_FAIL;
		}
	}
	return GDK_SUCCEED;
	
}

static gdk_return
la_bat_updates(logger *lg, logaction *la)
{
	log_bid bid = logger_find_bat(lg, la->cid);

	if (bid == 0)
		return GDK_SUCCEED; /* ignore bats no longer in the catalog */

	BAT *b = BATdescriptor(bid);
	if (b == NULL)
		return GDK_FAIL;
	if (la->type == LOG_UPDATE_BULK) {
		BUN cnt = BATcount(b);

		if (!lg->flushing) {
			/* handle offset 0 ie clear */
			if (/* DISABLES CODE */ (0) && la->offset == 0 && cnt)
				BATclear(b, true);
			/* handle offset */
			if (cnt <= (BUN)la->offset) {
				if (cnt < (BUN)la->offset) { /* insert nils */
					const void *tv = ATOMnilptr(b->ttype);
					lng i, d = la->offset - BATcount(b);
					for(i=0;i<d;i++) {
						if (BUNappend(b, tv, true) != GDK_SUCCEED) {
							logbat_destroy(b);
							return GDK_FAIL;
						}
					}
				}
				if (BATcount(b) == (BUN)la->offset && BATappend(b, la->b, NULL, true) != GDK_SUCCEED) {
					logbat_destroy(b);
					return GDK_FAIL;
				}
			} else {
				BATiter vi = bat_iterator(la->b);
				BUN p, q;
	
				for (p=0, q = (BUN)la->offset; p<(BUN)la->nr; p++, q++) {
					const void *t = BUNtail(vi, p);
	
					if (q < cnt) {
						if (BUNreplace(b, q, t, true) != GDK_SUCCEED) {
							logbat_destroy(b);
							return GDK_FAIL;
						}
					} else {
						if (BUNappend(b, t, true) != GDK_SUCCEED) {
							logbat_destroy(b);
							return GDK_FAIL;
						}
					}
				}
			}
		}
		cnt = (BUN)(la->offset + la->nr);
		if (la_bat_update_count(lg, la->cid, cnt) != GDK_SUCCEED) 
			return GDK_FAIL;
	} else if (!lg->flushing && la->type == LOG_UPDATE) {
		BATiter vi = bat_iterator(la->b);
		BUN p, q;

		BATloop(la->b, p, q) {
			oid h = BUNtoid(la->uid, p);
			const void *t = BUNtail(vi, p);

			if (h < b->hseqbase || h >= b->hseqbase + BATcount(b)) {
				/* if value doesn't exist, insert it;
				 * if b void headed, maintain that by
				 * inserting nils */
				if (b->batCount == 0 && !is_oid_nil(h))
					b->hseqbase = h;
				if (!is_oid_nil(b->hseqbase) && !is_oid_nil(h)) {
					const void *tv = ATOMnilptr(b->ttype);

					while (b->hseqbase + b->batCount < h) {
						if (BUNappend(b, tv, true) != GDK_SUCCEED) {
							logbat_destroy(b);
							return GDK_FAIL;
						}
					}
				}
				if (BUNappend(b, t, true) != GDK_SUCCEED) {
					logbat_destroy(b);
					return GDK_FAIL;
				}
			} else {
				if (BUNreplace(b, h, t, true) != GDK_SUCCEED) {
					logbat_destroy(b);
					return GDK_FAIL;
				}
			}
		}
	}
	logbat_destroy(b);
	return GDK_SUCCEED;
}

static log_return
log_read_destroy(logger *lg, trans *tr, log_id id)
{
	(void) lg;
	assert(!lg->inmemory);
	if (tr_grow(tr) == GDK_SUCCEED) {
		tr->changes[tr->nr].type = LOG_DESTROY;
		tr->changes[tr->nr].cid = id;
		tr->nr++;
	}
	return LOG_OK;
}

static gdk_return
la_bat_destroy(logger *lg, logaction *la)
{
	log_bid bid = logger_find_bat(lg, la->cid);

	if (bid && logger_del_bat(lg, bid) != GDK_SUCCEED)
		return GDK_FAIL;
	return GDK_SUCCEED;
}

static log_return
log_read_create(logger *lg, trans *tr, log_id id)
{
	char tt;
	int tpe;

	assert(!lg->inmemory);
	if (lg->debug & 1)
		fprintf(stderr, "#log_read_create %d\n", id);

	if (mnstr_read(lg->input_log, &tt, 1, 1) != 1)
		return LOG_ERR;

	tpe = find_type_nr(lg, tt);
	/* read create */
	if (tr_grow(tr) == GDK_SUCCEED) {
		tr->changes[tr->nr].type = LOG_CREATE;
		tr->changes[tr->nr].tt = tpe;
		tr->changes[tr->nr].cid = id;
		tr->nr++;
	}

	return LOG_OK;
}

static gdk_return
la_bat_create(logger *lg, logaction *la)
{
	BAT *b;

	/* formerly head column type, should be void */
	if ((b = COLnew(0, la->tt, BATSIZE, PERSISTENT)) == NULL)
		return GDK_FAIL;

	if (la->tt < 0)
		BATtseqbase(b, 0);

	if (BATsetaccess(b, BAT_READ) != GDK_SUCCEED ||
	    logger_add_bat(lg, b, la->cid) != GDK_SUCCEED)
		return GDK_FAIL;
	logbat_destroy(b);
	return GDK_SUCCEED;
}

static gdk_return
logger_write_new_types(logger *lg, FILE *fp)
{
	char id = 0;

	/* write types and insert into bats */
	/* first the fixed sized types */
	for (int i=0;i<GDKatomcnt; i++) {
		if (ATOMvarsized(i))
			continue;
		if (BUNappend(lg->type_id, &id, false) != GDK_SUCCEED ||
		    BUNappend(lg->type_nme, BATatoms[i].name, false) != GDK_SUCCEED ||
		    BUNappend(lg->type_nr, &i, false) != GDK_SUCCEED ||
		    fprintf(fp, "%d,%s\n", id, BATatoms[i].name) < 0) {
			return GDK_FAIL;
		}
		id++;
	}
	/* second the var sized types */
	id=-127; // start after nil 
	for (int i=0;i<GDKatomcnt; i++) {
		if (!ATOMvarsized(i))
			continue;
		if (BUNappend(lg->type_id, &id, false) != GDK_SUCCEED ||
		    BUNappend(lg->type_nme, BATatoms[i].name, false) != GDK_SUCCEED ||
		    BUNappend(lg->type_nr, &i, false) != GDK_SUCCEED ||
		    fprintf(fp, "%d,%s\n", id, BATatoms[i].name) < 0) {
			return GDK_FAIL;
		}
		id++;
	}
	return GDK_SUCCEED;
}

#define TR_SIZE		1024

static trans *
tr_create(trans *tr, int tid)
{
	trans *ntr = GDKmalloc(sizeof(trans));

	if (ntr == NULL)
		return NULL;
	ntr->tid = tid;
	ntr->sz = TR_SIZE;
	ntr->nr = 0;
	ntr->changes = GDKmalloc(sizeof(logaction) * TR_SIZE);
	if (ntr->changes == NULL) {
		GDKfree(ntr);
		return NULL;
	}
	ntr->tr = tr;
	return ntr;
}

static gdk_return
la_apply(logger *lg, logaction *c)
{
	gdk_return ret = GDK_SUCCEED;

	switch (c->type) {
	case LOG_UPDATE_BULK:
	case LOG_UPDATE:
		ret = la_bat_updates(lg, c);
		break;
	case LOG_CREATE:
		if (!lg->flushing)
			ret = la_bat_create(lg, c);
		lg->cnt++;
		break;
	case LOG_DESTROY:
		if (!lg->flushing)
			ret = la_bat_destroy(lg, c);
		break;
	case LOG_CLEAR:
		if (!lg->flushing)
			ret = la_bat_clear(lg, c);
		break;
	default:
		assert(0);
	}
	lg->changes += (ret == GDK_SUCCEED);
	return ret;
}

static void
la_destroy(logaction *c)
{
	if (c->b && (c->type == LOG_UPDATE || c->type == LOG_UPDATE_BULK))
		logbat_destroy(c->b);
	if (c->uid && c->type == LOG_UPDATE)
		logbat_destroy(c->uid);
}

static gdk_return
tr_grow(trans *tr)
{
	if (tr->nr == tr->sz) {
		logaction *changes;
		tr->sz <<= 1;
		changes = GDKrealloc(tr->changes, tr->sz * sizeof(logaction));
		if (changes == NULL)
			return GDK_FAIL;
		tr->changes = changes;
	}
	/* cleanup the next */
	tr->changes[tr->nr].b = NULL;
	return GDK_SUCCEED;
}

static trans *
tr_destroy(trans *tr)
{
	trans *r = tr->tr;

	GDKfree(tr->changes);
	GDKfree(tr);
	return r;
}

static trans *
tr_abort_(logger *lg, trans *tr, int s)
{
	int i;

	if (lg->debug & 1)
		fprintf(stderr, "#tr_abort\n");

	for (i = s; i < tr->nr; i++)
		la_destroy(&tr->changes[i]);
	return tr_destroy(tr);
}

static trans *
tr_abort(logger *lg, trans *tr)
{
	return tr_abort_(lg, tr, 0);
}

static trans *
tr_commit(logger *lg, trans *tr)
{
	int i;

	if (lg->debug & 1)
		fprintf(stderr, "#tr_commit\n");

	for (i = 0; i < tr->nr; i++) {
		if (la_apply(lg, &tr->changes[i]) != GDK_SUCCEED) {
			do {
				tr = tr_abort_(lg, tr, i);
			} while (tr != NULL);
			return (trans *) -1;
		}
		la_destroy(&tr->changes[i]);
	}
	lg->saved_tid = tr->tid;
	return tr_destroy(tr);
}

static gdk_return
logger_read_types_file(logger *lg, FILE *fp)
{
	int id = 0;
	char atom_name[IDLENGTH];

	/* scanf should use IDLENGTH somehow */
	while(fscanf(fp, "%d,%64s\n", &id, atom_name) == 2) {
		int i = ATOMindex(atom_name);

		if (id > 255 || i < 0) {
			GDKerror("unknown type in log file '%s'\n", atom_name);
			return GDK_FAIL;
		}
		char lid = (char)id;
		if (BUNappend(lg->type_id, &lid, false) != GDK_SUCCEED ||
		    BUNappend(lg->type_nme, atom_name, false) != GDK_SUCCEED ||
		    BUNappend(lg->type_nr, &i, false) != GDK_SUCCEED) {
			return GDK_FAIL;
		}
	}
	return GDK_SUCCEED;
}


static gdk_return
logger_create_types_file(logger *lg, char filename[FILENAME_MAX])
{
	FILE *fp;

	if ((fp = fopen(filename, "w")) == NULL) {
		GDKerror("cannot create log file %s\n", filename);
		return GDK_FAIL;
	}
	if (fprintf(fp, "%06d\n\n", lg->version) < 0) {
		fclose(fp);
		remove(filename);
		GDKerror("writing log file %s failed", filename);
		return GDK_FAIL;
	}

	if (logger_write_new_types(lg, fp) == GDK_FAIL) {
		fclose(fp);
		remove(filename);
		GDKerror("writing log file %s failed", filename);
		return GDK_FAIL;
	}
	if (fflush(fp) < 0 || (!(GDKdebug & NOSYNCMASK)
#if defined(_MSC_VER)
		     && _commit(_fileno(fp)) < 0
#elif defined(HAVE_FDATASYNC)
		     && fdatasync(fileno(fp)) < 0
#elif defined(HAVE_FSYNC)
		     && fsync(fileno(fp)) < 0
#endif
	    ) || fclose(fp) < 0) {
		remove(filename);
		GDKerror("closing log file %s failed", filename);
		return GDK_FAIL;
	}
	return GDK_SUCCEED;
}

#ifdef _MSC_VER
#define access(file, mode)	_access(file, mode)
#endif

static gdk_return
logger_open_output(logger *lg)
{
	int len;
	char id[BUFSIZ];
	char *filename;

	if (lg->inmemory || LOG_DISABLED(lg)) {
		lg->end = 0;
		if (lg->id) /* go back too last used id */
			lg->id--;
		return GDK_SUCCEED;
	}
	len = snprintf(id, sizeof(id), LLFMT, lg->id);
	if (len == -1 || len >= BUFSIZ) {
		fprintf(stderr, "!ERROR: logger_open: filename is too large\n");
		return GDK_FAIL;
	}
	if (!(filename = GDKfilepath(BBPselectfarm(PERSISTENT, 0, offheap), lg->dir, LOGFILE, id))) {
		fprintf(stderr, "!ERROR: logger_open: allocation failure\n");
		return GDK_FAIL;
	}

	lg->output_log = open_wstream(filename);
	if (lg->output_log) {
		short byteorder = 1234;
		mnstr_write(lg->output_log, &byteorder, sizeof(byteorder), 1);
	}
	lg->end = 0;

	if (lg->output_log == NULL || mnstr_errnr(lg->output_log)) {
		fprintf(stderr, "!ERROR: logger_open: creating %s failed\n", filename);
		GDKfree(filename);
		return GDK_FAIL;
	}
	GDKfree(filename);
	return GDK_SUCCEED;
}

static inline void
logger_close_input(logger *lg)
{
	if (!lg->inmemory)
		close_stream(lg->input_log);
	lg->input_log = NULL;
}

static inline void
logger_close_output(logger *lg)
{
	if (!lg->inmemory)
		close_stream(lg->output_log);
	lg->output_log = NULL;
}

static gdk_return
logger_open_input(logger *lg, char *filename, bool *filemissing)
{
	lg->input_log = open_rstream(filename);

	/* if the file doesn't exist, there is nothing to be read back */
	if (lg->input_log == NULL || mnstr_errnr(lg->input_log)) {
		logger_close_input(lg);
		*filemissing = true;
		return GDK_SUCCEED;
	}
	short byteorder;
	switch (mnstr_read(lg->input_log, &byteorder, sizeof(byteorder), 1)) {
	case -1:
		logger_close_input(lg);
		return GDK_FAIL;
	case 0:
		/* empty file is ok */
		logger_close_input(lg);
		return GDK_SUCCEED;
	case 1:
		/* if not empty, must start with correct byte order mark */
		assert(byteorder == 1234);
		break;
	}
	return GDK_SUCCEED;
}

static log_return
logger_read_transaction(logger *lg)
{
	logformat l;
	trans *tr = NULL;
	log_return err = LOG_OK;
	int ok = 1;
	int dbg = GDKdebug;

	GDKdebug &= ~(CHECKMASK|PROPMASK);

	while (err == LOG_OK && (ok=log_read_format(lg, &l))) {
		if (lg->debug & 1) {
			fprintf(stderr, "#logger_readlog: ");
			if (l.flag > 0 &&
			    l.flag < (char) (sizeof(log_commands) / sizeof(log_commands[0])))
				fprintf(stderr, "%s", log_commands[(int) l.flag]);
			else
				fprintf(stderr, "%d", l.flag);
			fprintf(stderr, " %d\n", l.id);
		}
		/* the functions we call here can succeed (LOG_OK),
		 * but they can also fail for two different reasons:
		 * they can run out of input (LOG_EOF -- this is not
		 * serious, we just abort the remaining transactions),
		 * or some malloc or BAT update fails (LOG_ERR -- this
		 * is serious, we must abort the complete process);
		 * the latter failure causes the current function to
		 * return GDK_FAIL */
		switch (l.flag) {
		case LOG_START:
			if (l.id > lg->tid)
				lg->tid = l.id;
			if ((tr = tr_create(tr, l.id)) == NULL) {
				err = LOG_ERR;
				break;
			}
			if (lg->debug & 1)
				fprintf(stderr, "#logger tstart %d\n", tr->tid);
			break;
		case LOG_END:
			if (tr == NULL)
				err = LOG_EOF;
			else if (tr->tid != l.id)	/* abort record */
				tr = tr_abort(lg, tr);
			else
				tr = tr_commit(lg, tr);
			break;
		case LOG_SEQ:
			err = log_read_seq(lg, &l);
			break;
		case LOG_UPDATE_BULK:
		case LOG_UPDATE:
			if (tr == NULL)
				err = LOG_EOF;
			else
				err = log_read_updates(lg, tr, &l, l.id, 0);
			break;
		case LOG_CREATE:
			if (tr == NULL)
				err = LOG_EOF;
			else
				err = log_read_create(lg, tr, l.id);
			break;
		case LOG_DESTROY:
			if (tr == NULL)
				err = LOG_EOF;
			else
				err = log_read_destroy(lg, tr, l.id);
			break;
		case LOG_CLEAR:
			if (tr == NULL)
				err = LOG_EOF;
			else
				err = log_read_clear(lg, tr, l.id);
			break;
		default:
			err = LOG_ERR;
		}
		if (tr == (trans *) -1) {
			err = LOG_ERR;
			tr = NULL;
			break;
		}
	}
	while (tr)
		tr = tr_abort(lg, tr);
	GDKdebug = dbg;
	if (!ok)
		return LOG_EOF;
	return err;
}

static gdk_return
logger_readlog(logger *lg, char *filename, bool *filemissing)
{
	log_return err = LOG_OK;
	time_t t0, t1;
	struct stat sb;
	int dbg = GDKdebug;

	assert(!lg->inmemory);
	GDKdebug &= ~(CHECKMASK|PROPMASK);

	if (lg->debug & 1) {
		fprintf(stderr, "#logger_readlog opening %s\n", filename);
	}

	gdk_return res = logger_open_input(lg, filename, filemissing);
	if (!lg->input_log)
		return res;
	int fd;
	if ((fd = getFileNo(lg->input_log)) < 0 || fstat(fd, &sb) < 0) {
		fprintf(stderr, "!ERROR: logger_readlog: fstat on opened file %s failed\n", filename);
		logger_close_input(lg);
		/* If the file could be opened, but fstat fails,
		 * something weird is going on */
		return GDK_FAIL;
	}
	t0 = time(NULL);
	if (lg->debug & 1) {
		printf("# Start reading the write-ahead log '%s'\n", filename);
		fflush(stdout);
	}
	while (err != LOG_EOF && err != LOG_ERR) {
		t1 = time(NULL);
		if (t1 - t0 > 10) {
			lng fpos;
			t0 = t1;
			/* not more than once every 10 seconds */
			fpos = (lng) getfilepos(getFile(lg->input_log));
			if (fpos >= 0) {
				printf("# still reading write-ahead log \"%s\" (%d%% done)\n", filename, (int) ((fpos * 100 + 50) / sb.st_size));
				fflush(stdout);
			}
		}
		err = logger_read_transaction(lg);
	}
	logger_close_input(lg);
	lg->input_log = NULL;

	/* remaining transactions are not committed, ie abort */
	if (lg->debug & 1) {
		printf("# Finished reading the write-ahead log '%s'\n", filename);
		fflush(stdout);
	}
	GDKdebug = dbg;
	/* we cannot distinguish errors from incomplete transactions
	 * (even if we would log aborts in the logs). So we simply
	 * abort and move to the next log file */
	return err == LOG_ERR ? GDK_FAIL : GDK_SUCCEED;
}

/*
 * The log files are incrementally numbered, starting from 2. They are
 * processed in the same sequence.
 */
static gdk_return
logger_readlogs(logger *lg, char *filename)
{
	gdk_return res = GDK_SUCCEED;

	assert(!lg->inmemory);
	if (lg->debug & 1)
		fprintf(stderr, "#logger_readlogs logger id is " LLFMT " last logger id is " LLFMT "\n", lg->id, lg->saved_id);

	char log_filename[FILENAME_MAX];
	if (lg->saved_id >= lg->id) {
		bool filemissing = false;

		lg->id = lg->saved_id+1;
		while (res == GDK_SUCCEED && !filemissing) {
			int len = snprintf(log_filename, sizeof(log_filename), "%s." LLFMT, filename, lg->id);
			if (len == -1 || len >= FILENAME_MAX)
				GDKerror("Logger filename path is too large\n");
			res = logger_readlog(lg, log_filename, &filemissing);
			if (!filemissing) {
				lg->saved_id++;
				lg->id++;
			}
		}
	}
	return res;
}

static gdk_return
logger_commit(logger *lg)
{
	if (lg->debug & 1)
		fprintf(stderr, "#logger_commit\n");

	return bm_commit(lg);
}

static gdk_return
check_version(logger *lg, FILE *fp, char filename[FILENAME_MAX])
{
	int version = 0;

	assert(!lg->inmemory);
	if (fscanf(fp, "%6d", &version) != 1) {
		GDKerror("Could not read the version number from the file '%s/log'.\n", lg->dir);
		return GDK_FAIL;
	}
	if (version != lg->version) {
		fclose(fp);
		if (old_logger_load(lg->debug, filename, lg->dir, lg->version, lg->prefuncp, lg->postfuncp) != GDK_SUCCEED) {
			//loads drop no longer needed catalog, snapshots bats
			//convert catalog_oid -> catalog_id (lng->int)
			GDKerror("Incompatible database version %06d, "
				 "this server supports version %06d.\n%s",
				 version, lg->version,
				 version < lg->version ? "Maybe you need to upgrade to an intermediate release first.\n" : "");
			return GDK_FAIL;
		}
		if (logger_create_types_file(lg, filename) != GDK_SUCCEED)
			return GDK_FAIL;
		return GDK_SUCCEED;
	} else {
		lg->postfuncp = NULL;	 /* don't call */

		if (fgetc(fp) != '\n' ||	 /* skip \n */
	    	    fgetc(fp) != '\n') {	 /* skip \n */
			GDKerror("Badly formatted log file");
			return GDK_FAIL;
		}
		if (logger_read_types_file(lg, fp) != GDK_SUCCEED) {
			fclose(fp);
			return GDK_FAIL;
		}
	}
	return GDK_SUCCEED;
}

static BAT *
bm_tids(BAT *b, BAT *d)
{
	BUN sz = BATcount(b);
	BAT *tids = BATdense(0, 0, sz);

	if (tids == NULL)
		return NULL;

	if (BATcount(d)) {
		BAT *diff = BATdiff(tids, d, NULL, NULL, false, false, BUN_NONE);
		logbat_destroy(tids);
		tids = diff;
	}
	return tids;
}


static gdk_return
logger_switch_bat(BAT *old, BAT *new, const char *fn, const char *name)
{
	int len;
	char bak[BUFSIZ];

	if (BATmode(old, true) != GDK_SUCCEED) {
		GDKerror("Logger_new: cannot convert old %s to transient", name);
		return GDK_FAIL;
	}
	len = snprintf(bak, sizeof(bak), "tmp_%o", (unsigned) old->batCacheid);
	if (len == -1 || len >= BUFSIZ) {
		GDKerror("Logger_new: filename is too large");
		return GDK_FAIL;
	}
	if (BBPrename(old->batCacheid, bak) != 0) {
		return GDK_FAIL;
	}
	strconcat_len(bak, sizeof(bak), fn, "_", name, NULL);
	if (BBPrename(new->batCacheid, bak) != 0) {
		return GDK_FAIL;
	}
	return GDK_SUCCEED;
}

static gdk_return
bm_get_counts(logger *lg)
{
	BUN p, q;
	const log_bid *bids = (const log_bid *) Tloc(lg->catalog_bid, 0);

	BATloop(lg->catalog_bid, p, q) {
		oid pos = p;
		lng cnt = 0;

		if (BUNfnd(lg->dcatalog, &pos) == BUN_NONE) {
			BAT *b = BBPquickdesc(bids[p], 1);
			cnt = BATcount(b);
		}
		if (BUNappend(lg->catalog_cnt, &cnt, false) != GDK_SUCCEED)
			return GDK_FAIL;
	}
	return GDK_SUCCEED;
}

static gdk_return
bm_subcommit(logger *lg)
{
	BUN p, q;
	BAT *catalog_bid = lg->catalog_bid;
	BAT *catalog_id = lg->catalog_id;
	BAT *dcatalog = lg->dcatalog;
	BUN nn = 13 + BATcount(catalog_bid);
	bat *n = GDKmalloc(sizeof(bat) * nn);
	BUN *sizes = GDKmalloc(sizeof(BUN) * nn);
	int i = 0;
	gdk_return res;
	const log_bid *bids;
	const BUN *cnts = NULL;

	if (n == NULL)
		return GDK_FAIL;

	sizes[i] = 0;
	n[i++] = 0;		/* n[0] is not used */
	bids = (const log_bid *) Tloc(catalog_bid, 0);
	if (lg->catalog_cnt)
		cnts = (const BUN *) Tloc(lg->catalog_cnt, 0);
	BATloop(catalog_bid, p, q) {
		bat col = bids[p];
		oid pos = p;

		if (BUNfnd(dcatalog, &pos) != BUN_NONE)
			continue;
		if (lg->debug & 1)
			fprintf(stderr, "#commit new %s (%d)\n", BBPname(col), col);
		assert(col);
		sizes[i] = cnts?cnts[p]:0;
		n[i++] = col;
	}
	/* now commit catalog, so it's also up to date on disk */
	sizes[i] = (BUN)lg->cnt;
	n[i++] = catalog_bid->batCacheid;
	sizes[i] = (BUN)lg->cnt;
	n[i++] = catalog_id->batCacheid;
	sizes[i] = BATcount(dcatalog); /* todo ! */
	n[i++] = dcatalog->batCacheid;

	if (BATcount(dcatalog) > 1024) {
		BAT *bids, *tids, *oids;

		tids = bm_tids(catalog_bid, dcatalog);
		if (tids == NULL) {
			GDKfree(n);
			GDKfree(sizes);
			return GDK_FAIL;
		}
		bids = logbat_new(TYPE_int, BATcount(tids), PERSISTENT);
		oids = logbat_new(TYPE_int, BATcount(tids), PERSISTENT);

		if (bids == NULL || oids == NULL) {
			logbat_destroy(tids);
			logbat_destroy(bids);
			logbat_destroy(oids);
			GDKfree(n);
			GDKfree(sizes);
			return GDK_FAIL;
		}

		if (BATappend(bids, catalog_bid, tids, true) != GDK_SUCCEED ||
		    BATappend(oids, catalog_id, tids, true) != GDK_SUCCEED) {
			logbat_destroy(tids);
			logbat_destroy(bids);
			logbat_destroy(oids);
			GDKfree(n);
			GDKfree(sizes);
			return GDK_FAIL;
		}
		logbat_destroy(tids);
		BATclear(dcatalog, true);

		if (logger_switch_bat(catalog_bid, bids, lg->fn, "catalog_bid") != GDK_SUCCEED ||
		    logger_switch_bat(catalog_id, oids, lg->fn, "catalog_id") != GDK_SUCCEED) {
			logbat_destroy(bids);
			GDKfree(n);
			GDKfree(sizes);
			return GDK_FAIL;
		}
		n[i++] = bids->batCacheid;
		n[i++] = oids->batCacheid;

		logbat_destroy(lg->catalog_bid);
		logbat_destroy(lg->catalog_id);

		lg->catalog_bid = catalog_bid = bids;
		lg->catalog_id = catalog_id = oids;
	}
	if (lg->seqs_id) {
		sizes[i] = BATcount(lg->seqs_id);
		n[i++] = lg->seqs_id->batCacheid;
		sizes[i] = BATcount(lg->seqs_id);
		n[i++] = lg->seqs_val->batCacheid;
		sizes[i] = BATcount(lg->dseqs);
		n[i++] = lg->dseqs->batCacheid;
	}
	if (lg->seqs_id && BATcount(lg->dseqs) > (BATcount(lg->seqs_id)/2)) {
		BAT *tids, *ids, *vals;

		tids = bm_tids(lg->seqs_id, lg->dseqs);
		if (tids == NULL) {
			GDKfree(n);
			GDKfree(sizes);
			return GDK_FAIL;
		}
		ids = logbat_new(TYPE_int, BATcount(tids), PERSISTENT);
		vals = logbat_new(TYPE_lng, BATcount(tids), PERSISTENT);

		if (ids == NULL || vals == NULL) {
			logbat_destroy(tids);
			logbat_destroy(ids);
			logbat_destroy(vals);
			GDKfree(n);
			GDKfree(sizes);
			return GDK_FAIL;
		}

		if (BATappend(ids, lg->seqs_id, tids, true) != GDK_SUCCEED ||
		    BATappend(vals, lg->seqs_val, tids, true) != GDK_SUCCEED) {
			logbat_destroy(tids);
			logbat_destroy(ids);
			logbat_destroy(vals);
			GDKfree(n);
			GDKfree(sizes);
			return GDK_FAIL;
		}
		logbat_destroy(tids);
		BATclear(lg->dseqs, true);

		if (logger_switch_bat(lg->seqs_id, ids, lg->fn, "seqs_id") != GDK_SUCCEED ||
		    logger_switch_bat(lg->seqs_val, vals, lg->fn, "seqs_val") != GDK_SUCCEED) {
			logbat_destroy(ids);
			logbat_destroy(vals);
			GDKfree(n);
			GDKfree(sizes);
			return GDK_FAIL;
		}
		n[i++] = ids->batCacheid;
		n[i++] = vals->batCacheid;
		n[i++] = lg->dseqs->batCacheid;

		logbat_destroy(lg->seqs_id);
		logbat_destroy(lg->seqs_val);

		lg->seqs_id = ids;
		lg->seqs_val = vals;
	}

	assert((BUN) i <= nn);
	BATcommit(catalog_bid, BUN_NONE);
	BATcommit(catalog_id, BUN_NONE);
	BATcommit(dcatalog, BUN_NONE);
	res = TMsubcommit_list(n, cnts?sizes:NULL, i, lg->saved_id, lg->saved_tid);
	GDKfree(n);
	GDKfree(sizes);
	if (res != GDK_SUCCEED)
		fprintf(stderr, "!ERROR: bm_subcommit: commit failed\n");
	return res;
}

static str
logger_filename(logger *lg, char bak[FILENAME_MAX], char filename[FILENAME_MAX])
{
	str filenamestr = NULL;
	int farmid = BBPselectfarm(PERSISTENT, 0, offheap);

	if ((filenamestr = GDKfilepath(farmid, lg->dir, LOGFILE, NULL)) == NULL)
		return NULL;
	int len = snprintf(filename, FILENAME_MAX, "%s", filenamestr);
	if (len == -1 || len >= FILENAME_MAX) {
		GDKfree(filenamestr);
		GDKerror("Logger filename path is too large\n");
		return NULL;
	}
	if (bak) {
		len = snprintf(bak, FILENAME_MAX, "%s.bak", filename);
		GDKfree(filenamestr);
		if (len == -1 || len >= FILENAME_MAX) {
			GDKerror("Logger filename path is too large\n");
			return NULL;
		}
	}
	return filename;
}

static gdk_return
logger_cleanup(logger *lg, lng id)
{
	char log_id[FILENAME_MAX];

	int len = snprintf(log_id, sizeof(log_id), LLFMT, id);
	if (len == -1 || len >= FILENAME_MAX) {
		fprintf(stderr, "#logger_cleanup: log_id filename is too large\n");
		return GDK_FAIL;
	}
	int farmid = BBPselectfarm(PERSISTENT, 0, offheap);
	if (GDKunlink(farmid, lg->dir, LOGFILE, log_id) != GDK_SUCCEED) {
		fprintf(stderr, "#logger_cleanup: failed to remove old WAL %s.%s\n", LOGFILE, log_id);
		GDKclrerr();
	}
	return GDK_SUCCEED;
}

/* Load data from the logger logdir
 * Initialize new directories and catalog files if none are present,
 * unless running in read-only mode
 * Load data and persist it in the BATs */
static gdk_return
logger_load(int debug, const char *fn, char filename[FILENAME_MAX], logger *lg)
{
	FILE *fp = NULL;
	char bak[FILENAME_MAX];
	bat catalog_bid, catalog_id, dcatalog;
	int farmid = BBPselectfarm(PERSISTENT, 0, offheap);
	bool needcommit = false;
	int dbg = GDKdebug;
	int readlogs = 0;

	/* refactor */
	if (!lg->inmemory && !LOG_DISABLED(lg)) {
		if (!logger_filename(lg, bak, filename))
			goto error;
	}

	lg->catalog_bid = NULL;
	lg->catalog_id = NULL;
	lg->catalog_cnt = NULL;
	lg->dcatalog = NULL;

	lg->seqs_id = NULL;
	lg->seqs_val = NULL;
	lg->dseqs = NULL;

	lg->type_id = NULL;
	lg->type_nme = NULL;
	lg->type_nr = NULL;

	if (!lg->inmemory && !LOG_DISABLED(lg)) {
		/* try to open logfile backup, or failing that, the file
		 * itself. we need to know whether this file exists when
		 * checking the database consistency later on */
		if ((fp = fopen(bak, "r")) != NULL) {
			fclose(fp);
			fp = NULL;
			if (GDKunlink(farmid, lg->dir, LOGFILE, NULL) != GDK_SUCCEED ||
			    GDKmove(farmid, lg->dir, LOGFILE, "bak", lg->dir, LOGFILE, NULL) != GDK_SUCCEED)
				goto error;
		}
		fp = fopen(filename, "r");
	}

	strconcat_len(bak, sizeof(bak), fn, "_catalog_bid", NULL);
	catalog_bid = BBPindex(bak);

	/* create transient bats for type mapping, to be read from disk */
	lg->type_id = logbat_new(TYPE_bte, BATSIZE, PERSISTENT);
	lg->type_nme = logbat_new(TYPE_str, BATSIZE, PERSISTENT);
	lg->type_nr = logbat_new(TYPE_int, BATSIZE, PERSISTENT);

	if (lg->type_id == NULL || lg->type_nme == NULL || lg->type_nr == NULL) {
		if (fp)
			fclose(fp);
		GDKerror("cannot create type bats");
		goto error;
	}

	/* this is intentional - if catalog_bid is 0, force it to find
	 * the persistent catalog */
	if (catalog_bid == 0) {
		/* catalog does not exist, so the log file also
		 * shouldn't exist */
		if (fp != NULL) {
			GDKerror("there is no logger catalog, "
				 "but there is a log file. "
				 "Are you sure you are using the correct "
				 "combination of database "
				 "(--dbpath) and log directory "
				 "(--set %s_logdir)?\n", fn);
			goto error;
		}

		lg->catalog_bid = logbat_new(TYPE_int, BATSIZE, PERSISTENT);
		lg->catalog_id = logbat_new(TYPE_int, BATSIZE, PERSISTENT);
		lg->dcatalog = logbat_new(TYPE_oid, BATSIZE, PERSISTENT);

		if (lg->catalog_bid == NULL || lg->catalog_id == NULL || lg->dcatalog == NULL) {
			GDKerror("cannot create catalog bats");
			goto error;
		}
		if (debug & 1)
			fprintf(stderr, "#create %s catalog\n", fn);

		/* give the catalog bats names so we can find them
		 * next time */
		strconcat_len(bak, sizeof(bak), fn, "_catalog_bid", NULL);
		if (BBPrename(lg->catalog_bid->batCacheid, bak) < 0) {
			goto error;
		}

		strconcat_len(bak, sizeof(bak), fn, "_catalog_id", NULL);
		if (BBPrename(lg->catalog_id->batCacheid, bak) < 0) {
			goto error;
		}

		strconcat_len(bak, sizeof(bak), fn, "_dcatalog", NULL);
		if (BBPrename(lg->dcatalog->batCacheid, bak) < 0) {
			goto error;
		}

		if (!lg->inmemory && !LOG_DISABLED(lg)) {
			if (GDKcreatedir(filename) != GDK_SUCCEED) {
				GDKerror("cannot create directory for log file %s\n", filename);
				goto error;
			}
			if (logger_create_types_file(lg, filename) != GDK_SUCCEED)
				goto error;
		}

		BBPretain(lg->catalog_bid->batCacheid);
		BBPretain(lg->catalog_id->batCacheid);
		BBPretain(lg->dcatalog->batCacheid);

		if (bm_subcommit(lg) != GDK_SUCCEED) {
			/* cannot commit catalog, so remove log */
			remove(filename);
			BBPrelease(lg->catalog_bid->batCacheid);
			BBPrelease(lg->catalog_id->batCacheid);
			BBPrelease(lg->dcatalog->batCacheid);
			goto error;
		}
	} else {
		/* find the persistent catalog. As non persistent bats
		 * require a logical reference we also add a logical
		 * reference for the persistent bats */
		BUN p, q;
		BAT *b = BATdescriptor(catalog_bid), *o, *d;

		/* the catalog exists, and so should the log file */
		if (fp == NULL && !LOG_DISABLED(lg)) {
			GDKerror("there is a logger catalog, but no log file. "
				 "Are you sure you are using the correct combination of database "
				 "(--dbpath) and log directory (--set %s_logdir)? "
				 "If you have done a recent update of the server, it may be that your "
				 "logs are in an old location.  You should then either use "
				 "--set %s_logdir=<path to old log directory> or move the old log "
				 "directory to the new location (%s).\n",
				 fn, fn, lg->dir);
			goto error;
		}
		if (check_version(lg, fp, filename) != GDK_SUCCEED) {
			goto error;
		}
		readlogs = 1;
		fclose(fp);
		fp = NULL;

		assert(!lg->inmemory);
		if (b == NULL) {
			GDKerror("inconsistent database, catalog does not exist");
			goto error;
		}

		strconcat_len(bak, sizeof(bak), fn, "_catalog_id", NULL);
		catalog_id = BBPindex(bak);
		o = BATdescriptor(catalog_id);
		if (o == NULL) {
			BBPunfix(b->batCacheid);
			GDKerror("inconsistent database, catalog_id does not exist");
			goto error;
		}

		strconcat_len(bak, sizeof(bak), fn, "_dcatalog", NULL);
		dcatalog = BBPindex(bak);
		d = BATdescriptor(dcatalog);
		if (d == NULL) {
			GDKerror("cannot create dcatalog bat");
			BBPunfix(b->batCacheid);
			BBPunfix(o->batCacheid);
			goto error;
		}

		lg->catalog_bid = b;
		lg->catalog_id = o;
		lg->dcatalog = d;
		BBPretain(lg->catalog_bid->batCacheid);
		BBPretain(lg->catalog_id->batCacheid);
		BBPretain(lg->dcatalog->batCacheid);
		const log_bid *bids = (const log_bid *) Tloc(b, 0);
		BATloop(b, p, q) {
			bat bid = bids[p];
			oid pos = p;

			if (BUNfnd(lg->dcatalog, &pos) == BUN_NONE &&
			    BBPretain(bid) == 0 &&
			    BUNappend(lg->dcatalog, &pos, false) != GDK_SUCCEED)
				goto error;
		}
	}
	lg->catalog_cnt = logbat_new(TYPE_lng, 1, TRANSIENT);
	if (lg->catalog_cnt == NULL) {
		GDKerror("failed to create catalog_cnt bat");
		goto error;
	}
	strconcat_len(bak, sizeof(bak), fn, "_catalog_cnt", NULL);
	if (BBPrename(lg->catalog_cnt->batCacheid, bak) < 0) {
		goto error;
	}
	if (bm_get_counts(lg) == GDK_FAIL)
		goto error;

	strconcat_len(bak, sizeof(bak), fn, "_seqs_id", NULL);
	if (BBPindex(bak)) {
		lg->seqs_id = BATdescriptor(BBPindex(bak));
		strconcat_len(bak, sizeof(bak), fn, "_seqs_val", NULL);
		lg->seqs_val = BATdescriptor(BBPindex(bak));
		strconcat_len(bak, sizeof(bak), fn, "_dseqs", NULL);
		lg->dseqs = BATdescriptor(BBPindex(bak));
	} else {
		lg->seqs_id = logbat_new(TYPE_int, 1, PERSISTENT);
		lg->seqs_val = logbat_new(TYPE_lng, 1, PERSISTENT);
		lg->dseqs = logbat_new(TYPE_oid, 1, PERSISTENT);
		if (lg->seqs_id == NULL ||
		    lg->seqs_val == NULL ||
		    lg->dseqs == NULL) {
			GDKerror("Logger_new: cannot create seqs bats");
			goto error;
		}

		strconcat_len(bak, sizeof(bak), fn, "_seqs_id", NULL);
		if (BBPrename(lg->seqs_id->batCacheid, bak) < 0) {
			goto error;
		}

		strconcat_len(bak, sizeof(bak), fn, "_seqs_val", NULL);
		if (BBPrename(lg->seqs_val->batCacheid, bak) < 0) {
			goto error;
		}

		strconcat_len(bak, sizeof(bak), fn, "_dseqs", NULL);
		if (BBPrename(lg->dseqs->batCacheid, bak) < 0) {
			goto error;
		}
		needcommit = true;
	}
	GDKdebug &= ~CHECKMASK;
	if (needcommit && bm_commit(lg) != GDK_SUCCEED) {
		GDKerror("Logger_new: commit failed");
		goto error;
	}
	GDKdebug = dbg;

	if (readlogs) {
		lng log_id = lg->saved_id+1;
		if (logger_readlogs(lg, filename) != GDK_SUCCEED) {
			goto error;
		}
		if (lg->postfuncp && (*lg->postfuncp)(lg) != GDK_SUCCEED)
			goto error;
		if (logger_commit(lg) != GDK_SUCCEED) {
			goto error;
		}
		for( ; log_id <= lg->saved_id; log_id++)
			(void)logger_cleanup(lg, log_id);  /* ignore error of removing file */
	}
	return GDK_SUCCEED;
  error:
	if (fp)
		fclose(fp);
	logbat_destroy(lg->catalog_bid);
	logbat_destroy(lg->catalog_id);
	logbat_destroy(lg->dcatalog);
	logbat_destroy(lg->seqs_id);
	logbat_destroy(lg->seqs_val);
	logbat_destroy(lg->dseqs);
	logbat_destroy(lg->type_id);
	logbat_destroy(lg->type_nme);
	logbat_destroy(lg->type_nr);
	GDKfree(lg->fn);
	GDKfree(lg->dir);
	GDKfree(lg->local_dir);
	GDKfree(lg->buf);
	GDKfree(lg);
	return GDK_FAIL;
}

/* Initialize a new logger
 * It will load any data in the logdir and persist it in the BATs*/
static logger *
logger_new(int debug, const char *fn, const char *logdir, int version, preversionfix_fptr prefuncp, postversionfix_fptr postfuncp)
{
	int len;
	logger *lg;
	char filename[FILENAME_MAX];

	if (!GDKinmemory() && MT_path_absolute(logdir)) {
		fprintf(stderr, "!ERROR: logger_new: logdir must be relative path\n");
		return NULL;
	}

	lg = GDKmalloc(sizeof(struct logger));
	if (lg == NULL) {
		fprintf(stderr, "!ERROR: logger_new: allocating logger structure failed\n");
		return NULL;
	}

	*lg = (logger) {
		.inmemory = GDKinmemory(),
		.debug = debug,
		.version = version,
		.prefuncp = prefuncp,
		.postfuncp = postfuncp,

		.id = 1,
		.tid = (int)getBBPtransid(), 	/* get saved transaction id from bbp */
		.saved_id = getBBPlogno(), 	/* get saved log numer from bbp */
	};

	/* probably open file and check version first, then call call old logger code */
	len = snprintf(filename, sizeof(filename), "%s%c%s%c", logdir, DIR_SEP, fn, DIR_SEP);
	if (len == -1 || len >= FILENAME_MAX) {
		fprintf(stderr, "!ERROR: logger_new: filename is too large\n");
		GDKfree(lg);
		return NULL;
	}
	lg->fn = GDKstrdup(fn);
	lg->dir = GDKstrdup(filename);
	lg->bufsize = 64*1024;
	lg->buf = GDKmalloc(lg->bufsize);
	if (lg->fn == NULL || lg->dir == NULL || lg->buf == NULL) {
		fprintf(stderr, "!ERROR: logger_new: strdup failed\n");
		GDKfree(lg->fn);
		GDKfree(lg->dir);
		GDKfree(lg->buf);
		GDKfree(lg);
		return NULL;
	}
	if (lg->debug & 1) {
		fprintf(stderr, "#logger_new dir set to %s\n", lg->dir);
	}

	if (logger_load(debug, fn, filename, lg) == GDK_SUCCEED) {
		return lg;
	}
	return NULL;
}

void
logger_destroy(logger *lg)
{
	if (lg->catalog_bid) {
		BUN p, q;
		BAT *b = lg->catalog_bid;

		/* free resources */
		const log_bid *bids = (const log_bid *) Tloc(b, 0);
		BATloop(b, p, q) {
			bat bid = bids[p];
			oid pos = p;

			if (BUNfnd(lg->dcatalog, &pos) == BUN_NONE)
				BBPrelease(bid);
		}

		BBPrelease(lg->catalog_bid->batCacheid);
		BBPrelease(lg->catalog_id->batCacheid);
		BBPrelease(lg->dcatalog->batCacheid);
		logbat_destroy(lg->catalog_bid);
		logbat_destroy(lg->catalog_id);
		logbat_destroy(lg->dcatalog);

		logbat_destroy(lg->catalog_cnt);
	}
	GDKfree(lg->fn);
	GDKfree(lg->dir);
	logger_close_input(lg);
	logger_close_output(lg);
	GDKfree(lg);
}

/* Create a new logger */
logger *
logger_create(int debug, const char *fn, const char *logdir, int version, preversionfix_fptr prefuncp, postversionfix_fptr postfuncp)
{
	logger *lg;
	lg = logger_new(debug, fn, logdir, version, prefuncp, postfuncp);
	if (lg == NULL)
		return NULL;
	if (lg->debug & 1) {
		printf("# Started processing logs %s/%s version %d\n",fn,logdir,version);
		fflush(stdout);
	}
	if (lg->debug & 1) {
		printf("# Finished processing logs %s/%s\n",fn,logdir);
		fflush(stdout);
	}
	if (GDKsetenv("recovery", "finished") != GDK_SUCCEED) {
		logger_destroy(lg);
		return NULL;
	}
	if (logger_open_output(lg) != GDK_SUCCEED) {
		logger_destroy(lg);
		return NULL;
	}
	return lg;
}


gdk_return
logger_flush(logger *lg)
{
	if (lg->inmemory || LOG_DISABLED(lg)) {
		lg->saved_id = lg->id-1;
		lg->saved_tid = lg->tid-1;
		lg->changes = 0;
		return GDK_SUCCEED;
	}
	if (lg->saved_id+1 >= lg->id) { /* logger should first release the file */
		lg->changes -= 1000;
		return GDK_SUCCEED;
	}
	if (!lg->input_log) {
		char *filename;
		char id[BUFSIZ];
		int len = snprintf(id, sizeof(id), LLFMT, lg->saved_id+1);

		if (len == -1 || len >= BUFSIZ) {
			fprintf(stderr, "!ERROR: logger_open: filename is too large\n");
			return GDK_FAIL;
		}
		if (!(filename = GDKfilepath(BBPselectfarm(PERSISTENT, 0, offheap), lg->dir, LOGFILE, id)))
			return GDK_FAIL;
		if (strlen(filename) >= FILENAME_MAX) {
			GDKerror("Logger filename path is too large\n");
			GDKfree(filename);
			return GDK_FAIL;
		}
		//printf("log file %s\n", filename);

		bool filemissing = false;
		if (logger_open_input(lg, filename, &filemissing) == GDK_FAIL) {
			GDKfree(filename);
			return GDK_FAIL;
		}
		GDKfree(filename);
	}
	/* we read the full file because skipping is impossible with current log format */
	lg->flushing = 1;
	log_return res = logger_read_transaction(lg);
	lg->flushing = 0;
	if (res == LOG_EOF)
		logger_close_input(lg);
	if (res != LOG_ERR) {
		lg->saved_id++;
		if (logger_commit(lg) != GDK_SUCCEED) {
			fprintf(stderr, "!ERROR: logger_flush: logger_commit failed\n");
			res = LOG_ERR;
		}

		/* remove old log file */
		if (res != LOG_ERR)
			return logger_cleanup(lg, lg->saved_id);
	}
	return res == LOG_ERR ? GDK_FAIL : GDK_SUCCEED;
}

/* Clean-up write-ahead log files already persisted in the BATs, leaving only the most recent one.
 * Only the bak- files are deleted for the preserved WAL files.
 */
lng
logger_changes(logger *lg)
{
	return lg->changes;
}

int
logger_sequence(logger *lg, int seq, lng *id)
{
	BUN p = log_find(lg->seqs_id, lg->dseqs, seq);

	if (p != BUN_NONE) {
		*id = *(lng *) Tloc(lg->seqs_val, p);

		return 1;
	}
	return 0;
}

/*
 * Changes made to the BAT descriptor should be stored in the log
 * files.  Actually, we need to save the descriptor file, perhaps we
 * should simply introduce a versioning scheme.
 */
gdk_return
log_bat_persists(logger *lg, BAT *b, int id)
{
	char ta = find_type(lg, b->ttype);
	logformat l;

	if (logger_add_bat(lg, b, id) != GDK_SUCCEED)
		return GDK_FAIL;

	l.flag = LOG_CREATE;
	l.id = id;
	lg->changes++;
	if (!lg->inmemory && !LOG_DISABLED(lg)) {
		if (log_write_format(lg, &l) != GDK_SUCCEED ||
		    mnstr_write(lg->output_log, &ta, 1, 1) != 1) 
			return GDK_FAIL;
	}
	if (lg->debug & 1)
		fprintf(stderr, "#persists id (%d) bat (%d)\n", id, b->batCacheid);
	if (lg->inmemory || LOG_DISABLED(lg))
		return GDK_SUCCEED;
	return log_bat(lg, b, id, 0);
}

gdk_return
log_bat_transient(logger *lg, int id)
{
	log_bid bid = logger_find_bat(lg, id);
	logformat l;

	l.flag = LOG_DESTROY;
	l.id = id;
	lg->changes++;

	if (lg->inmemory || LOG_DISABLED(lg))
		return GDK_SUCCEED;

	if (log_write_format(lg, &l) != GDK_SUCCEED) {
		fprintf(stderr, "!ERROR: log_bat_transient: write failed\n");
		return GDK_FAIL;
	}
	if (lg->debug & 1)
		fprintf(stderr, "#Logged destroyed bat (%d)\n", id);

	return logger_del_bat(lg, bid);
}

gdk_return
log_delta(logger *lg, BAT *uid, BAT *uval, log_id id)
{
	char tpe = find_type(lg, uval->ttype);
	gdk_return ok = GDK_SUCCEED;
	logformat l;
	BUN p;
	lng nr;

	if (BATtdense(uid)) 
		return log_bat(lg, uval, id, uid->tseqbase);

	assert(uid->ttype == TYPE_oid || uid->ttype == TYPE_void);

	l.flag = LOG_UPDATE;
	l.id = id;
	nr = (BUNlast(uval));
	assert(nr);
	lg->changes += nr;

	if (LOG_DISABLED(lg) || lg->inmemory) {
		/* logging is switched off */
		return GDK_SUCCEED;
	}

	BATiter vi = bat_iterator(uval);
	gdk_return (*wh) (const void *, stream *, size_t) = BATatoms[TYPE_oid].atomWrite;
	gdk_return (*wt) (const void *, stream *, size_t) = BATatoms[uval->ttype].atomWrite;

	if (log_write_format(lg, &l) != GDK_SUCCEED ||
	    !mnstr_writeLng(lg->output_log, nr) ||
	    mnstr_write(lg->output_log, &tpe, 1, 1) != 1) 
		return GDK_FAIL;
	/* TODO call efficient n-value writes */
	for (p = 0; p < BUNlast(uid) && ok == GDK_SUCCEED; p++) {
		const oid id = BUNtoid(uid, p);

		ok = wh(&id, lg->output_log, 1);
	}
	for (p = 0; p < BUNlast(uid) && ok == GDK_SUCCEED; p++) {
		const void *val = BUNtail(vi, p);

		ok = wt(val, lg->output_log, 1);
	}

	if (lg->debug & 1)
		fprintf(stderr, "#Logged %d " LLFMT " inserts\n", id, nr);

	if (ok != GDK_SUCCEED)
		fprintf(stderr, "!ERROR: log_delta: write failed\n");
	return ok;
}

gdk_return
log_bat(logger *lg, BAT *b, log_id id, lng offset)
{
	char tpe = find_type(lg, b->ttype);
	gdk_return ok = GDK_SUCCEED;
	logformat l;
	BUN p;
	lng nr;
	int is_row = 0;

	if (lg->row_insert_nrcols != 0) {
		lg->row_insert_nrcols--;
		is_row = 1;
	}
	l.flag = LOG_UPDATE_BULK;
	l.id = id;
	nr = (BUNlast(b) - b->batInserted);
	lg->changes += (b->batInserted)?nr:1; /* initial large inserts is counted as 1 change */

	if (LOG_DISABLED(lg) || lg->inmemory || !nr) {
		/* logging is switched off */
		return GDK_SUCCEED;
	}

	BATiter bi = bat_iterator(b);
	gdk_return (*wt) (const void *, stream *, size_t) = BATatoms[b->ttype].atomWrite;

	if (is_row)
		l.flag = tpe; 
	if (log_write_format(lg, &l) != GDK_SUCCEED ||
	    (!is_row && !mnstr_writeLng(lg->output_log, nr)) ||
	    (!is_row && mnstr_write(lg->output_log, &tpe, 1, 1) != 1) ||
	    (!is_row && !mnstr_writeLng(lg->output_log, offset))) 
		return GDK_FAIL;

	if (b->ttype < TYPE_str && !isVIEW(b)) {
		const void *t = BUNtail(bi, b->batInserted);

		ok = wt(t, lg->output_log, (size_t)nr);
	} else {
		for (p = b->batInserted; p < BUNlast(b) && ok == GDK_SUCCEED; p++) {
			const void *t = BUNtail(bi, p);

			ok = wt(t, lg->output_log, 1);
		}
	}

	if (lg->debug & 1)
		fprintf(stderr, "#Logged %d " LLFMT " inserts\n", id, nr);

	if (ok != GDK_SUCCEED)
		fprintf(stderr, "!ERROR: log_bat: write failed\n");
	return ok;
}

gdk_return
log_bat_clear(logger *lg, int id)
{
	logformat l;

	if (LOG_DISABLED(lg) || lg->inmemory)
		return GDK_SUCCEED;

	l.flag = LOG_CLEAR;
	l.id = id;
	lg->changes++;

	if (lg->debug & 1)
		fprintf(stderr, "#Logged clear %d\n", id);
	return log_write_format(lg, &l);
}

#define DBLKSZ		8192
#define SEGSZ		(64*DBLKSZ)

#define LOG_LARGE	LL_CONSTANT(2)*1024
//*1024*1024

static gdk_return
pre_allocate(logger *lg)
{
	// FIXME: this causes serious issues on Windows at least with MinGW
	assert(!lg->inmemory && !LOG_DISABLED(lg));
#ifndef WIN32
	lng p;
	p = (lng) getfilepos(getFile(lg->output_log));
	if (p == -1)
		return GDK_FAIL;
	if (p > LOG_LARGE) {
		lg->id++;
		logger_close_output(lg);
		return logger_open_output(lg);
	}
	if (p + DBLKSZ > lg->end) {
		p &= ~(DBLKSZ - 1);
		p += SEGSZ;
		if (GDKextendf(getFileNo(lg->output_log), (size_t) p, "WAL file") != GDK_SUCCEED)
			return GDK_FAIL;
		lg->end = p;
	}
#else
	(void) lg;
#endif
	return GDK_SUCCEED;
}

gdk_return
log_tend(logger *lg)
{
	logformat l;
	gdk_return res = GDK_SUCCEED;

	if (LOG_DISABLED(lg) || lg->inmemory)
		return GDK_SUCCEED;

	if (lg->debug & 1)
		fprintf(stderr, "#log_tend %d\n", lg->tid);

	l.flag = LOG_END;
	l.id = lg->tid;
	lg->changes++;

	if (res != GDK_SUCCEED ||
	    log_write_format(lg, &l) != GDK_SUCCEED ||
	    mnstr_flush(lg->output_log) ||
	    (!(GDKdebug & NOSYNCMASK) && mnstr_fsync(lg->output_log)) ||
	    pre_allocate(lg) != GDK_SUCCEED) {
		fprintf(stderr, "!ERROR: log_tend: write failed\n");
		return GDK_FAIL;
	}
	return GDK_SUCCEED;
}

static gdk_return
log_sequence_(logger *lg, int seq, lng val, int flush)
{
	logformat l;

	if (LOG_DISABLED(lg) || lg->inmemory)
		return GDK_SUCCEED;
	l.flag = LOG_SEQ;
	l.id = seq;

	if (lg->debug & 1)
		fprintf(stderr, "#log_sequence_ (%d," LLFMT ")\n", seq, val);

	if (log_write_format(lg, &l) != GDK_SUCCEED ||
	    !mnstr_writeLng(lg->output_log, val) ||
	    (flush && mnstr_flush(lg->output_log)) ||
	    (flush && !(GDKdebug & NOSYNCMASK) && mnstr_fsync(lg->output_log))) {
		fprintf(stderr, "!ERROR: log_sequence_: write failed\n");
		return GDK_FAIL;
	}
	return GDK_SUCCEED;
}

/* a transaction in it self */
gdk_return
log_sequence(logger *lg, int seq, lng val)
{
	BUN p;

	if (lg->debug & 1)
		fprintf(stderr, "#log_sequence (%d," LLFMT ")\n", seq, val);

	if ((p = log_find(lg->seqs_id, lg->dseqs, seq)) != BUN_NONE &&
	    p >= lg->seqs_id->batInserted) {
		if (BUNinplace(lg->seqs_val, p, &val, false) != GDK_SUCCEED)
			return GDK_FAIL;
	} else {
		if (p != BUN_NONE) {
			oid pos = p;
			if (BUNappend(lg->dseqs, &pos, false) != GDK_SUCCEED)
				return GDK_FAIL;
		}
		if (BUNappend(lg->seqs_id, &seq, false) != GDK_SUCCEED ||
		    BUNappend(lg->seqs_val, &val, false) != GDK_SUCCEED)
			return GDK_FAIL;
	}
	return log_sequence_(lg, seq, val, 1);
}

static gdk_return
bm_commit(logger *lg)
{
	BUN p;
	BAT *b = lg->catalog_bid;
	const log_bid *bids;

	bids = (log_bid *) Tloc(b, 0);
	for (p = b->batInserted; p < BUNlast(b); p++) {
		log_bid bid = bids[p];
		BAT *lb;
		oid pos = p;

		if (BUNfnd(lg->dcatalog, &pos) != BUN_NONE)
			continue;

		if ((lb = BATdescriptor(bid)) == NULL ||
		    BATmode(lb, false) != GDK_SUCCEED) {
			logbat_destroy(lb);
			return GDK_FAIL;
		}

		assert(lb->batRestricted != BAT_WRITE);
		logbat_destroy(lb);

		if (lg->debug & 1)
			fprintf(stderr, "#bm_commit: create %d (%d)\n",
				bid, BBP_lrefs(bid));
	}
	return bm_subcommit(lg);
}

gdk_return
logger_add_bat(logger *lg, BAT *b, log_id id)
{
	log_bid bid = logger_find_bat(lg, id);
	lng cnt = 0;

	assert(b->batRestricted != BAT_WRITE ||
	       b == lg->catalog_bid ||
	       b == lg->catalog_id ||
	       b == lg->dcatalog ||
	       b == lg->seqs_id ||
	       b == lg->seqs_val ||
	       b == lg->dseqs ||
	       b == lg->type_id ||
	       b == lg->type_nme ||
	       b == lg->type_nr);
	assert(b->batRole == PERSISTENT);
	if (bid) {
		assert(0);
		if (bid != b->batCacheid) {
			if (logger_del_bat(lg, bid) != GDK_SUCCEED)
				return GDK_FAIL;
		} else {
			return GDK_SUCCEED;
		}
	}
	bid = b->batCacheid;
	if (lg->debug & 1)
		fprintf(stderr, "#create %d\n", id);
	assert(log_find(lg->catalog_bid, lg->dcatalog, bid) == BUN_NONE);
	lg->changes += BATcount(b) + 1000;
	if (BUNappend(lg->catalog_bid, &bid, false) != GDK_SUCCEED ||
	    BUNappend(lg->catalog_id, &id, false) != GDK_SUCCEED ||
	    BUNappend(lg->catalog_cnt, &cnt, false) != GDK_SUCCEED)
		return GDK_FAIL;
	BBPretain(bid);
	return GDK_SUCCEED;
}

gdk_return
logger_del_bat(logger *lg, log_bid bid)
{
	BAT *b = BATdescriptor(bid);
	BUN p = log_find(lg->catalog_bid, lg->dcatalog, bid);
	oid pos;

	assert(p != BUN_NONE);
	if (p == BUN_NONE) {
		logbat_destroy(b);
		GDKerror("cannot find BAT\n");
		return GDK_FAIL;
	}

	if (p >= lg->catalog_bid->batInserted) {
		BBPrelease(bid);
	}
	if (b) {
		lg->changes += BATcount(b) + 1;
		BBPunfix(b->batCacheid);
	}
	pos = (oid) p;
	return BUNappend(lg->dcatalog, &pos, false);
/*assert(BBP_lrefs(bid) == 0);*/
}

log_bid
logger_find_bat(logger *lg, log_id id)
{
	BATiter cni = bat_iterator(lg->catalog_id);
	BUN p;

	if (BAThash(lg->catalog_id) == GDK_SUCCEED) {
		HASHloop_int(cni, cni.b->thash, p, &id) {
			oid pos = p;
			if (BUNfnd(lg->dcatalog, &pos) == BUN_NONE)
				return *(log_bid *) Tloc(lg->catalog_bid, p);
		}
	}
	return 0;
}


gdk_return
log_tstart(logger *lg)
{
	logformat l;

	if (LOG_DISABLED(lg) || lg->inmemory)
		return GDK_SUCCEED;

	l.flag = LOG_START;
	l.id = ++lg->tid;
	lg->changes++;

	if (lg->debug & 1)
		fprintf(stderr, "#log_tstart %d\n", lg->tid);
	return log_write_format(lg, &l);
}

