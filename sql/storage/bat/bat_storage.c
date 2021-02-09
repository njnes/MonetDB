/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2021 MonetDB B.V.
 */

#include "monetdb_config.h"
#include "bat_storage.h"
#include "bat_utils.h"
#include "sql_string.h"
#include "gdk_atoms.h"
#include "gdk_atoms.h"
#include "matomic.h"

//static MT_Lock segs_lock = MT_LOCK_INITIALIZER(segs_lock);
#define NR_TABLE_LOCKS 64
//static MT_Lock table_locks[NR_TABLE_LOCKS]; /* set of locks to protect table changes (claim) */

static int log_update_col( sql_trans *tr, sql_change *c);
static int log_update_idx( sql_trans *tr, sql_change *c);
static int log_update_del( sql_trans *tr, sql_change *c);
static int commit_update_col( sql_trans *tr, sql_change *c, ulng commit_ts, ulng oldest);
static int commit_update_idx( sql_trans *tr, sql_change *c, ulng commit_ts, ulng oldest);
static int commit_update_del( sql_trans *tr, sql_change *c, ulng commit_ts, ulng oldest);
static int log_create_col(sql_trans *tr, sql_change *change);
static int log_create_idx(sql_trans *tr, sql_change *change);
static int log_create_del(sql_trans *tr, sql_change *change);
static int commit_create_col(sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest);
static int commit_create_idx(sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest);
static int commit_create_del(sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest);
static int log_destroy_col(sql_trans *tr, sql_change *change);
static int log_destroy_idx(sql_trans *tr, sql_change *change);
static int log_destroy_del(sql_trans *tr, sql_change *change);
static int tc_gc_col( sql_store Store, sql_change *c, ulng commit_ts, ulng oldest);
static int tc_gc_idx( sql_store Store, sql_change *c, ulng commit_ts, ulng oldest);
static int tc_gc_del( sql_store Store, sql_change *c, ulng commit_ts, ulng oldest);

static int tr_merge_delta( sql_trans *tr, sql_delta *obat);

#if 0
static void
lock_table(sqlstore *store, sqlid id)
{
	(void)store;
	(void)id;
	//MT_lock_set(&table_locks[id&(NR_TABLE_LOCKS-1)]);
}

static void
unlock_table(sqlstore *store, sqlid id)
{
	(void)store;
	(void)id;
	//MT_lock_unset(&table_locks[id&(NR_TABLE_LOCKS-1)]);
}
#endif

/* used for communication between {append,update}_prepare and {append,update}_execute */
struct prep_exec_cookie {
	sql_delta *delta;
	bool is_new; // only used for updates
};

/* creates a new cookie, backed by sql allocator memory so it is
 * automatically freed even if errors occur
 */
static struct prep_exec_cookie *
make_cookie(sql_allocator *sa, sql_delta *delta, bool is_new)
{
	struct prep_exec_cookie *cookie;
	cookie = SA_NEW(sa, struct prep_exec_cookie);
	if (!cookie)
		return NULL;
	cookie->delta = delta;
	cookie->is_new = is_new;
	return cookie;
}

static BAT *
mask_bat(size_t cnt, msk val)
{
	BAT *b = COLnew(0, TYPE_msk, cnt, TRANSIENT);

	if (b) {
		size_t nr = (cnt+31)/32;
		int *p = (int*)b->T.heap->base;
		for(size_t i = 0; i<nr; i++) {
			p[i] = (val)?0xffffffff:0;
		}
		BATsetcount(b, cnt);
	}
	return b;
}

static segment *
new_segment(segment *o, sql_trans *tr, size_t cnt)
{
	segment *n = (segment*)GDKmalloc(sizeof(segment));

	if (n) {
		n->owner = tr?tr:0;
		n->start = o?o->end:0;
		n->end = n->start + cnt;
		n->next = o;
	}
	return n;
}

static segments *
new_segments(size_t cnt)
{
	segments *n = (segments*)GDKmalloc(sizeof(segments));

	if (n) {
		sql_ref_init(&n->r);
		n->head = new_segment(NULL, NULL, cnt);
	       	n->end = n->head->end;
	}
	return n;
}

static segments*
dup_segments(segments *s)
{
	sql_ref_inc(&s->r);
	return s;
}

static int
temp_dup_cs(column_storage *cs, ulng tid, int type)
{
	BAT *b = bat_new(type, 1024, TRANSIENT);
	if (!b)
		return LOG_ERR;
	cs->bid = temp_create(b);
	bat_destroy(b);
	cs->uibid = e_bat(TYPE_oid);
	cs->uvbid = e_bat(type);
	cs->ucnt = cs->cnt = 0;
	cs->cleared = 0;
	cs->ts = tid;
	cs->refcnt = 1;
	return LOG_OK;
}

static sql_delta *
temp_dup_delta(ulng tid, int type)
{
	sql_delta *bat = ZNEW(sql_delta);
	if (temp_dup_cs(&bat->cs, tid, type)) {
		_DELETE(bat);
		return NULL;
	}
	return bat;
}

static sql_delta *
temp_delta(sql_delta *d, ulng tid)
{
	while (d && d->cs.ts != tid)
		d = d->next;
	return d;
}

static storage *
temp_dup_storage(ulng tid)
{
	storage *bat = ZNEW(storage);
	if (temp_dup_cs(&bat->cs, tid, TYPE_msk)) {
		_DELETE(bat);
		return NULL;
	}
	bat->segs = new_segments(0);
	return bat;
}

static storage *
temp_storage(storage *d, ulng tid)
{
	while (d && d->cs.ts != tid)
		d = d->next;
	return d;
}

static sql_delta *
timestamp_delta( sql_trans *tr, sql_delta *d)
{
	while (d->next && d->cs.ts != tr->tid && (!tr->parent || !tr_version_of_parent(tr, d->cs.ts)) && d->cs.ts > tr->ts)
		d = d->next;
	return d;
}

static sql_delta *
temp_col_timestamp_delta( sql_trans *tr, sql_column *c)
{
	assert(isTempTable(c->t));
	sql_delta *d = temp_delta(ATOMIC_PTR_GET(&c->data), tr->tid);
	if (!d) {
		d = temp_dup_delta(tr->tid, c->type.type->localtype);
		do {
			d->next = ATOMIC_PTR_GET(&c->data);
		} while(!ATOMIC_PTR_CAS(&c->data, (void**)&d->next, d)); /* set c->data = d, when c->data == d->next else d->next = c->data */
	}
	return d;
}

sql_delta *
col_timestamp_delta( sql_trans *tr, sql_column *c)
{
	if (isTempTable(c->t))
		return temp_col_timestamp_delta(tr, c);
	return timestamp_delta( tr, ATOMIC_PTR_GET(&c->data));
}

static sql_delta *
temp_idx_timestamp_delta( sql_trans *tr, sql_idx *i)
{
	assert(isTempTable(i->t));
	sql_delta *d = temp_delta(ATOMIC_PTR_GET(&i->data), tr->tid);
	if (!d) {
		int type = oid_index(i->type)?TYPE_oid:TYPE_lng;
		d = temp_dup_delta(tr->tid, type);
		do {
			d->next = ATOMIC_PTR_GET(&i->data);
		} while(!ATOMIC_PTR_CAS(&i->data, (void**)&d->next, d)); /* set i->data = d, when i->data == d->next else d->next = i->data */
	}
	return d;
}

static sql_delta *
idx_timestamp_delta( sql_trans *tr, sql_idx *i)
{
	if (isTempTable(i->t))
		return temp_idx_timestamp_delta(tr, i);
	return timestamp_delta( tr, ATOMIC_PTR_GET(&i->data));
}

static storage *
timestamp_storage( sql_trans *tr, storage *d)
{
	while (d->next && d->cs.ts != tr->tid && (!tr->parent || !tr_version_of_parent(tr, d->cs.ts)) && d->cs.ts > tr->ts)
		d = d->next;
	return d;
}

static storage *
temp_tab_timestamp_storage( sql_trans *tr, sql_table *t)
{
	assert(isTempTable(t));
	storage *d = temp_storage(ATOMIC_PTR_GET(&t->data), tr->tid);
	if (!d) {
		d = temp_dup_storage(tr->tid);
		do {
			d->next = ATOMIC_PTR_GET(&t->data);
		} while(!ATOMIC_PTR_CAS(&t->data, (void**)&d->next, d)); /* set t->data = d, when t->data == d->next else d->next = t->data */
	}
	return d;
}

static storage *
tab_timestamp_storage( sql_trans *tr, sql_table *t)
{
	if (isTempTable(t))
		return temp_tab_timestamp_storage(tr, t);
	return timestamp_storage( tr, ATOMIC_PTR_GET(&t->data));
}

static sql_delta*
delta_dup(sql_delta *d)
{
	d->cs.refcnt++;
	return d;
}

static void *
col_dup(sql_column *c)
{
	return delta_dup(ATOMIC_PTR_GET(&c->data));
}

static void *
idx_dup(sql_idx *i)
{
	if (!ATOMIC_PTR_GET(&i->data))
		return NULL;
	return delta_dup(ATOMIC_PTR_GET(&i->data));
}

static storage*
storage_dup(storage *d)
{
	d->cs.refcnt++;
	return d;
}

static void *
del_dup(sql_table *t)
{
	return storage_dup(ATOMIC_PTR_GET(&t->data));
}

static size_t
count_inserts( segment *s, sql_trans *tr)
{
	size_t cnt = 0;

	while(s) {
		if (s->owner == tr)
			cnt += s->end - s->start;
		s = s->next;
	}
	return cnt;
}

static size_t
count_col(sql_trans *tr, sql_column *c, int access)
{
	storage *d;
	sql_delta *ds;
	sql_table *t = c->t;

	if (!isTable(c->t))
		return 0;
	d = tab_timestamp_storage(tr, t);
	ds = col_timestamp_delta(tr, c);
	if (!d)
		return 0;
	if (access == 2)
		return ds?ds->cs.ucnt:0;
	if (access == 1)
		return count_inserts(d->segs->head, tr);
	return d->end;
}

static size_t
count_idx(sql_trans *tr, sql_idx *i, int access)
{
	storage *d;
	sql_delta *ds;

	if (!isTable(i->t) || (hash_index(i->type) && list_length(i->columns) <= 1) || !idx_has_column(i->type))
		return 0;
	d = tab_timestamp_storage(tr, i->t);
	ds = idx_timestamp_delta(tr, i);
	if (!d)
		return 0;
	if (access == 2)
		return ds?ds->cs.ucnt:0;
	if (access == 1)
		return count_inserts(d->segs->head, tr);
	return d->end;
}

static BAT *
cs_bind_ubat( column_storage *cs, int access, int type)
{
	BAT *b;

	assert(access == RD_UPD_ID || access == RD_UPD_VAL);
	if (cs->uibid && cs->uvbid) {
		if (access == RD_UPD_ID)
			b = temp_descriptor(cs->uibid);
		else
			b = temp_descriptor(cs->uvbid);
	} else {
		b = e_BAT(access == RD_UPD_ID?TYPE_oid:type);
	}
	return b;
}

static BAT *
bind_ucol(sql_trans *tr, sql_column *c, int access)
{
	assert(tr->active);
	sql_delta *d = col_timestamp_delta(tr, c);
	return cs_bind_ubat(&d->cs, access, c->type.type->localtype);
}

static BAT *
bind_uidx(sql_trans *tr, sql_idx * i, int access)
{
	int type = oid_index(i->type)?TYPE_oid:TYPE_lng;
	assert(tr->active);
	sql_delta *d = idx_timestamp_delta(tr, i);
	return cs_bind_ubat(&d->cs, access, type);
}

static BAT *
cs_bind_bat( column_storage *cs, int access, size_t cnt)
{
	BAT *b;

	assert(access == RDONLY || access == QUICK);
	assert(cs != NULL);
	if (access == QUICK)
		return quick_descriptor(cs->bid);
	assert(cs->bid);
	b = temp_descriptor(cs->bid);
	if (b == NULL)
		return NULL;
	bat_set_access(b, BAT_READ);
	/* return slice */
	BAT *s = BATslice(b, 0, cnt);
	bat_destroy(b);
	return s;
}

static BAT *
bind_col(sql_trans *tr, sql_column *c, int access)
{
	assert(access == QUICK || tr->active);
	if (!isTable(c->t))
		return NULL;
	if (access == RD_UPD_ID || access == RD_UPD_VAL)
		return bind_ucol(tr, c, access);
	sql_delta *d = col_timestamp_delta(tr, c);
	size_t cnt = count_col(tr, c, 0);
	return cs_bind_bat( &d->cs, access, cnt);
}

static BAT *
bind_idx(sql_trans *tr, sql_idx * i, int access)
{
	assert(access == QUICK || tr->active);
	if (!isTable(i->t))
		return NULL;
	if (access == RD_UPD_ID || access == RD_UPD_VAL)
		return bind_uidx(tr, i, access);
	sql_delta *d = idx_timestamp_delta(tr, i);
	size_t cnt = count_idx(tr, i, 0);
	return cs_bind_bat( &d->cs, access, cnt);
}

static BAT *
bind_del(sql_trans *tr, sql_table *t, int access)
{
	assert(access == QUICK || tr->active);
	if (!isTable(t))
		return NULL;
	storage *d = tab_timestamp_storage(tr, t);
	if (access == RD_UPD_ID || access == RD_UPD_VAL) {
		return cs_bind_ubat( &d->cs, access, TYPE_msk);
	} else {
		return cs_bind_bat( &d->cs, access, d->end);
	}
}

static int
cs_real_update_bats( column_storage *cs, BAT **Ui, BAT **Uv)
{
	BAT *ui = temp_descriptor(cs->uibid);
	BAT *uv = temp_descriptor(cs->uvbid);

	if (ui == NULL || uv == NULL) {
		bat_destroy(ui);
		bat_destroy(uv);
		return LOG_ERR;
	}
	assert(ui && uv);
	if (isEbat(ui)){
		temp_destroy(cs->uibid);
		cs->uibid = temp_copy(ui->batCacheid, false);
		bat_destroy(ui);
		if (cs->uibid == BID_NIL ||
		    (ui = temp_descriptor(cs->uibid)) == NULL) {
			bat_destroy(uv);
			return LOG_ERR;
		}
	}
	if (isEbat(uv)){
		temp_destroy(cs->uvbid);
		cs->uvbid = temp_copy(uv->batCacheid, false);
		bat_destroy(uv);
		if (cs->uvbid == BID_NIL ||
		    (uv = temp_descriptor(cs->uvbid)) == NULL) {
			bat_destroy(ui);
			return LOG_ERR;
		}
	}
	*Ui = ui;
	*Uv = uv;
	return LOG_OK;
}

static int
cs_update_bat( column_storage *cs, BAT *tids, BAT *updates, int is_new)
{
	int res = LOG_OK;
	BAT *otids = tids;
	if (!BATcount(tids))
		return LOG_OK;

	if (tids && (tids->ttype == TYPE_msk || mask_cand(tids))) {
		otids = BATunmask(tids);
		if (!otids)
			return LOG_ERR;
	}
	/* handle cleared and updates on just inserted bits */
	if (!is_new && !cs->cleared && cs->uibid && cs->uvbid) {
		BAT *ui, *uv;

		if (cs_real_update_bats(cs, &ui, &uv) == LOG_ERR)
			return LOG_ERR;

		assert(BATcount(otids) == BATcount(updates));
		if (BATappend(ui, otids, NULL, true) != GDK_SUCCEED ||
		    BATappend(uv, updates, NULL, true) != GDK_SUCCEED) {
			if (otids != tids)
				bat_destroy(otids);
			bat_destroy(ui);
			bat_destroy(uv);
			return LOG_ERR;
		}
		assert(BATcount(otids) == BATcount(updates));
		assert(BATcount(ui) == BATcount(uv));
		bat_destroy(ui);
		bat_destroy(uv);
		cs->ucnt += BATcount(otids);
	} else if (is_new || cs->cleared) {
		BAT *b = temp_descriptor(cs->bid);

		if (b == NULL)
			res = LOG_ERR;
		else if (BATcount(b)==0 && BATappend(b, updates, NULL, true) != GDK_SUCCEED) /* alter add column */
			res = LOG_ERR;
		else if (BATreplace(b, otids, updates, true) != GDK_SUCCEED)
			res = LOG_ERR;
		bat_destroy(b);
	}
	if (otids != tids)
		bat_destroy(otids);
	return res;
}

static int
delta_update_bat( sql_delta *bat, BAT *tids, BAT *updates, int is_new)
{
	return cs_update_bat(&bat->cs, tids, updates, is_new);
}

static int
cs_update_val( column_storage *cs, oid rid, void *upd, int is_new)
{
	assert(!is_oid_nil(rid));

	if (!is_new && !cs->cleared && cs->uibid && cs->uvbid) {
		BAT *ui, *uv;

		if (cs_real_update_bats(cs, &ui, &uv) == LOG_ERR)
			return LOG_ERR;

		assert(BATcount(ui) == BATcount(uv));
		if (BUNappend(ui, (ptr) &rid, true) != GDK_SUCCEED ||
		    BUNappend(uv, (ptr) upd, true) != GDK_SUCCEED) {
			assert(0);
			bat_destroy(ui);
			bat_destroy(uv);
			return LOG_ERR;
		}
		assert(BATcount(ui) == BATcount(uv));
		bat_destroy(ui);
		bat_destroy(uv);
		cs->ucnt++;
	} else if (is_new || cs->cleared) {
		BAT *b = NULL;

		if((b = temp_descriptor(cs->bid)) == NULL)
			return LOG_ERR;
		if (void_inplace(b, rid, upd, true) != GDK_SUCCEED) {
			bat_destroy(b);
			return LOG_ERR;
		}
		bat_destroy(b);
	}
	return LOG_OK;
}

static int
delta_update_val( sql_delta *bat, oid rid, void *upd, int is_new)
{
	return cs_update_val(&bat->cs, rid, upd, is_new);
}

static int
dup_cs(sql_trans *tr, column_storage *ocs, column_storage *cs, int type, int temp)
{
	(void)tr;
	if (!ocs)
		return LOG_OK;
	(void)type;
	cs->bid = ocs->bid;
	cs->uibid = ocs->uibid;
	cs->uvbid = ocs->uvbid;
	cs->ucnt = ocs->ucnt;
	//cs->cleared = ocs->cleared;

	if (temp) {
		cs->bid = temp_copy(cs->bid, 1);
		if (cs->bid == BID_NIL)
			return LOG_ERR;
	} else {
		temp_dup(cs->bid);
	}
	if (!temp) {
		if (cs->uibid && cs->uvbid) {
			ocs->uibid = ebat_copy(cs->uibid);
			ocs->uvbid = ebat_copy(cs->uvbid);
			if (ocs->uibid == BID_NIL ||
			    ocs->uvbid == BID_NIL)
				return LOG_ERR;
		} else {
			cs->uibid = e_bat(TYPE_oid);
			cs->uvbid = e_bat(type);
			if (cs->uibid == BID_NIL || cs->uvbid == BID_NIL)
				return LOG_ERR;
		}
	}
	return LOG_OK;
}

static int
dup_bat(sql_trans *tr, sql_table *t, sql_delta *obat, sql_delta *bat, int type)
{
	return dup_cs(tr, &obat->cs, &bat->cs, type, isTempTable(t));
}

static int
destroy_delta(sql_delta *b)
{
	int ok = LOG_OK;

	if (--b->cs.refcnt > 0)
		return LOG_OK;
	if (b->next)
		ok = destroy_delta(b->next);
	if (b->cs.uibid)
		temp_destroy(b->cs.uibid);
	if (b->cs.uvbid)
		temp_destroy(b->cs.uvbid);
	if (b->cs.bid)
		temp_destroy(b->cs.bid);
	b->cs.bid = b->cs.uibid = b->cs.uvbid = 0;
	_DELETE(b);
	return ok;
}

static sql_delta *
bind_col_data(sql_trans *tr, sql_column *c)
{
	sql_delta *obat = ATOMIC_PTR_GET(&c->data);

	if (isTempTable(c->t))
		obat = temp_col_timestamp_delta(tr, c);

	if (obat->cs.ts == tr->tid)
		return obat;
	if ((!tr->parent || !tr_version_of_parent(tr, obat->cs.ts)) && obat->cs.ts >= TRANSACTION_ID_BASE && !isTempTable(c->t))
		/* abort */
		return NULL;
	assert(!isTempTable(c->t));
	obat = timestamp_delta(tr, ATOMIC_PTR_GET(&c->data));
	sql_delta* bat = ZNEW(sql_delta);
	if(!bat)
		return NULL;
	bat->cs.refcnt = 1;
	if(dup_bat(tr, c->t, obat, bat, c->type.type->localtype) == LOG_ERR)
		return NULL;
	bat->cs.ts = tr->tid;
	/* only one writer else abort */
	bat->next = obat;
	if (!ATOMIC_PTR_CAS(&c->data, (void**)&bat->next, bat)) {
		bat->next = NULL;
		destroy_delta(bat);
		return NULL;
	}
	return bat;
}

static void*
update_col_prepare(sql_trans *tr, sql_allocator *sa, sql_column *c)
{
	sql_delta *delta, *odelta = ATOMIC_PTR_GET(&c->data);

	if ((delta = bind_col_data(tr, c)) == NULL)
		return NULL;

	assert(delta && delta->cs.ts == tr->tid);
	if ((!inTransaction(tr, c->t) && (odelta != delta || isTempTable(c->t)) && isGlobal(c->t)) || (!isNew(c->t) && isLocalTemp(c->t)))
		trans_add(tr, &c->base, delta, &tc_gc_col, &commit_update_col, isLocalTemp(c->t)?NULL:&log_update_col);
	return make_cookie(sa, delta, isNew(c));
}

static int
update_col_execute(void *incoming_cookie, void *incoming_tids, void *incoming_values, bool is_bat)
{
	struct prep_exec_cookie *cookie = incoming_cookie;

	if (is_bat) {
		BAT *tids = incoming_tids;
		BAT *values = incoming_values;
		if (BATcount(tids) == 0)
			return LOG_OK;
		return delta_update_bat(cookie->delta, tids, values, cookie->is_new);
	}
	else
		return delta_update_val(cookie->delta, *(oid*)incoming_tids, incoming_values, cookie->is_new);
}

static int
update_col(sql_trans *tr, sql_column *c, void *tids, void *upd, int tpe)
{
	void *cookie = update_col_prepare(tr, NULL, c);
	if (cookie == NULL)
		return LOG_ERR;

	int ok = update_col_execute(cookie, tids, upd, tpe == TYPE_bat);
	_DELETE(cookie);
	return ok;
}

static sql_delta *
bind_idx_data(sql_trans *tr, sql_idx *i)
{
	sql_delta *obat = ATOMIC_PTR_GET(&i->data);

	if (isTempTable(i->t))
		obat = temp_idx_timestamp_delta(tr, i);

	if (obat->cs.ts == tr->tid)
		return obat;
	if ((!tr->parent || !tr_version_of_parent(tr, obat->cs.ts)) && obat->cs.ts >= TRANSACTION_ID_BASE && !isTempTable(i->t))
		/* abort */
		return NULL;
	assert(!isTempTable(i->t));
	obat = timestamp_delta(tr, ATOMIC_PTR_GET(&i->data));
	sql_delta* bat = ZNEW(sql_delta);
	if(!bat)
		return NULL;
	bat->cs.refcnt = 1;
	if(dup_bat(tr, i->t, obat, bat, (oid_index(i->type))?TYPE_oid:TYPE_lng) == LOG_ERR)
		return NULL;
	bat->cs.ts = tr->tid;
	/* only one writer else abort */
	bat->next = obat;
	if (!ATOMIC_PTR_CAS(&i->data, (void**)&bat->next, bat)) {
		bat->next = NULL;
		destroy_delta(bat);
		return NULL;
	}
	return bat;
}

static void*
update_idx_prepare(sql_trans *tr, sql_allocator *sa, sql_idx *i)
{
	sql_delta *delta, *odelta = ATOMIC_PTR_GET(&i->data);

	if ((delta = bind_idx_data(tr, i)) == NULL)
		return NULL;

	assert(delta && delta->cs.ts == tr->tid);
	if ((!inTransaction(tr, i->t) && (odelta != delta || isTempTable(i->t)) && isGlobal(i->t)) || (!isNew(i->t) && isLocalTemp(i->t)))
		trans_add(tr, &i->base, delta, &tc_gc_idx, &commit_update_idx, isLocalTemp(i->t)?NULL:&log_update_idx);
	return make_cookie(sa, delta, isNew(i));
}

static int
update_idx(sql_trans *tr, sql_idx * i, void *tids, void *upd, int tpe)
{
	void *cookie = update_idx_prepare(tr, NULL, i);
	if (cookie == NULL)
		return LOG_ERR;

	int ok = update_col_execute(cookie, tids, upd, tpe == TYPE_bat);
	_DELETE(cookie);
	return ok;
}

static int
delta_append_bat( sql_delta *bat, size_t offset, BAT *i )
{
	BAT *b, *oi = i;

	if (!BATcount(i))
		return LOG_OK;
	b = temp_descriptor(bat->cs.bid);
	if (b == NULL)
		return LOG_ERR;
	if (i && (i->ttype == TYPE_msk || mask_cand(i))) {
		oi = BATunmask(i);
	}
	if (BATcount(b) >= offset+BATcount(oi)){
		BAT *ui = BATdense(0, offset, BATcount(oi));
		if (BATreplace(b, ui, oi, true) != GDK_SUCCEED) {
			if (oi != i)
				bat_destroy(oi);
			bat_destroy(b);
			bat_destroy(ui);
			return LOG_ERR;
		}
		assert(!isVIEW(b));
		bat_destroy(ui);
	} else {
		//assert (isNew(t) || isTempTable(t) || bat->cs.cleared);
		if (BATcount(b) < offset) { /* add space */
			const void *tv = ATOMnilptr(b->ttype);
			lng d = offset - BATcount(b);
			for(lng j=0;j<d;j++) {
				if (BUNappend(b, tv, true) != GDK_SUCCEED) {
					if (oi != i)
						bat_destroy(oi);
					bat_destroy(b);
					return LOG_ERR;
				}
			}
		}
		if (isVIEW(oi) && b->batCacheid == VIEWtparent(oi)) {
			BAT *ic = COLcopy(oi, oi->ttype, true, TRANSIENT);

			if (ic == NULL || BATappend(b, ic, NULL, true) != GDK_SUCCEED) {
				if (oi != i)
					bat_destroy(oi);
				bat_destroy(ic);
                		bat_destroy(b);
                		return LOG_ERR;
            		}
            		bat_destroy(ic);
		} else if (BATappend(b, oi, NULL, true) != GDK_SUCCEED) {
			if (oi != i)
				bat_destroy(oi);
			bat_destroy(b);
			return LOG_ERR;
		}
	}
	if (oi != i)
		bat_destroy(oi);
	assert(!isVIEW(b));
	bat_destroy(b);
	return LOG_OK;
}

static int
delta_append_val( sql_delta *bat, size_t offset, void *i )
{
	BAT *b = temp_descriptor(bat->cs.bid);

	if(b == NULL)
		return LOG_ERR;

	if (BATcount(b) > offset){
		if (BUNreplace(b, offset, i, true) != GDK_SUCCEED) {
			bat_destroy(b);
			return LOG_ERR;
		}
	} else {
		if (BATcount(b) < offset) { /* add space */
			const void *tv = ATOMnilptr(b->ttype);
			lng i, d = offset - BATcount(b);
			for(i=0;i<d;i++) {
				if (BUNappend(b, tv, true) != GDK_SUCCEED) {
					bat_destroy(b);
					return LOG_ERR;
				}
			}
		}
		if (BUNappend(b, i, true) != GDK_SUCCEED) {
			bat_destroy(b);
			return LOG_ERR;
		}
	}
	bat_destroy(b);
	return LOG_OK;
}

static int
dup_storage( sql_trans *tr, storage *obat, storage *bat, int temp)
{
	if (temp) {
		bat->segs = new_segments(0);
		bat->end = bat->segs->end;
	} else {
		bat->end = obat->end = obat->segs->end;
		//MT_lock_set(&segs_lock);
		bat->segs = dup_segments(obat->segs);
		//MT_lock_unset(&segs_lock);
		assert(bat->end <= bat->segs->end);
	}
	bat->cached_cnt = obat->cached_cnt;
	bat->cnt = obat->cnt + obat->ucnt;
	bat->ucnt = 0;
	bat->icnt = 0;
	return dup_cs(tr, &obat->cs, &bat->cs, TYPE_msk, temp);
}


static void*
append_col_prepare(sql_trans *tr, sql_allocator *sa, sql_column *c)
{
	sql_delta *delta, *odelta = ATOMIC_PTR_GET(&c->data);

	if ((delta = bind_col_data(tr, c)) == NULL)
		return NULL;

	assert(delta && delta->cs.ts == tr->tid);
	if ((!inTransaction(tr, c->t) && (odelta != delta || isTempTable(c->t)) && isGlobal(c->t)) || (!isNew(c->t) && isLocalTemp(c->t)))
		trans_add(tr, &c->base, delta, &tc_gc_col, &commit_update_col, isLocalTemp(c->t)?NULL:&log_update_col);
	return make_cookie(sa, delta, false);
}

static int
append_col_execute(void *incoming_cookie, size_t offset, void *incoming_data, bool is_bat)
{
	struct prep_exec_cookie *cookie = incoming_cookie;
	int ok;

	//lock_table(c->t->base.id);
	if (is_bat) {
		BAT *bat = incoming_data;

		if (!BATcount(bat))
			return LOG_OK;
		ok = delta_append_bat(cookie->delta, offset, bat);
	} else {
		ok = delta_append_val(cookie->delta, offset, incoming_data);
	}
	//unlock_table(c->t->base.id);
	return ok;
}

static int
append_col(sql_trans *tr, sql_column *c, size_t offset, void *i, int tpe)
{
	void *cookie = append_col_prepare(tr, NULL, c);
	if (cookie == NULL)
		return LOG_ERR;

	int ok = append_col_execute(cookie, offset, i, tpe == TYPE_bat);
	_DELETE(cookie);
	return ok;
}

static void*
append_idx_prepare(sql_trans *tr, sql_allocator *sa, sql_idx *i)
{
	sql_delta *delta, *odelta = ATOMIC_PTR_GET(&i->data);

	if ((delta = bind_idx_data(tr, i)) == NULL)
		return NULL;

	assert(delta && delta->cs.ts == tr->tid);
	if ((!inTransaction(tr, i->t) && (odelta != delta || isTempTable(i->t)) && isGlobal(i->t)) || (!isNew(i->t) && isLocalTemp(i->t)))
		trans_add(tr, &i->base, delta, &tc_gc_idx, &commit_update_idx, isLocalTemp(i->t)?NULL:&log_update_idx);
	return make_cookie(sa, delta, false);
}

static int
append_idx(sql_trans *tr, sql_idx * i, size_t offset, void *data, int tpe)
{
	void *cookie = append_idx_prepare(tr, NULL, i);
	if (cookie == NULL)
		return LOG_ERR;

	int ok = append_col_execute(cookie, offset, data, tpe == TYPE_bat);
	_DELETE(cookie);
	return ok;
}

static int
delta_delete_bat( storage *bat, BAT *i, int is_new)
{
	/* update ids */
	msk T = TRUE;
	BAT *t, *oi = i;

	if (i->ttype == TYPE_msk || mask_cand(i))
		i = BATunmask(i);
	bat->ucnt+=BATcount(i);
	t = BATconstant(i->hseqbase, TYPE_msk, &T, BATcount(i), TRANSIENT);
	int ok = LOG_OK;

	assert(i->ttype != TYPE_msk);
	if (t)
		ok = cs_update_bat( &bat->cs, i, t, is_new);
	bat_destroy(t);
	if (i != oi)
		bat_destroy(i);
	return ok;
}

static int
delta_delete_val( storage *bat, oid rid, int is_new)
{
	/* update pos */
	msk T = TRUE;
	bat->ucnt++;
	return cs_update_val(&bat->cs, rid, &T, is_new);
}

static void
destroy_segments(segments *s)
{
	if (!s || sql_ref_dec(&s->r) > 0)
		return;
	segment *seg = s->head;
	while(seg) {
		segment *n = seg->next;
		_DELETE(seg);
		seg = n;
	}
	_DELETE(s);
}

static int
destroy_storage(storage *bat)
{
	int ok = LOG_OK;

	if (--bat->cs.refcnt > 0)
		return LOG_OK;
	if (bat->next)
		ok = destroy_storage(bat->next);
	destroy_segments(bat->segs);
	if (bat->cs.uibid)
		temp_destroy(bat->cs.uibid);
	if (bat->cs.uvbid)
		temp_destroy(bat->cs.uvbid);
	if (bat->cs.bid)
		temp_destroy(bat->cs.bid);
	bat->cs.bid = bat->cs.uibid = bat->cs.uvbid = 0;
	_DELETE(bat);
	return ok;
}

static storage *
bind_del_data(sql_trans *tr, sql_table *t)
{
	storage *obat = ATOMIC_PTR_GET(&t->data);

	if (isTempTable(t))
		obat = temp_tab_timestamp_storage(tr, t);

	if (obat->cs.ts == tr->tid)
		return obat;
	if ((!tr->parent || !tr_version_of_parent(tr, obat->cs.ts)) && obat->cs.ts >= TRANSACTION_ID_BASE && !isTempTable(t))
		/* abort */
		return NULL;
	assert(!isTempTable(t));
	obat = timestamp_storage(tr, ATOMIC_PTR_GET(&t->data));
	storage *bat = ZNEW(storage);
	if(!bat)
		return NULL;
	bat->cs.refcnt = 1;
	dup_storage(tr, obat, bat, isTempTable(t));
	bat->cs.ts = tr->tid;
	/* only one writer else abort */
	bat->next = obat;
	if (!ATOMIC_PTR_CAS(&t->data, (void**)&bat->next, bat)) {
		bat->next = NULL;
		destroy_storage(bat);
		return NULL;
	}
	return bat;
}

static int
delete_tab(sql_trans *tr, sql_table * t, void *ib, int tpe)
{
	int ok = LOG_OK;
	BAT *b = ib;
	storage *bat, *obat = ATOMIC_PTR_GET(&t->data);

	if (tpe == TYPE_bat && !BATcount(b))
		return ok;

	if ((bat = bind_del_data(tr, t)) == NULL)
		return LOG_ERR;

	assert(bat && bat->cs.ts == tr->tid);
	/* deletes only write */
	if (tpe == TYPE_bat)
		ok = delta_delete_bat(bat, ib, isNew(t));
	else
		ok = delta_delete_val(bat, *(oid*)ib, isNew(t));
	if ((!inTransaction(tr, t) && (obat != bat || isTempTable(t)) && isGlobal(t)) || (!isNew(t) && isLocalTemp(t)))
		trans_add(tr, &t->base, bat, &tc_gc_del, &commit_update_del, isLocalTemp(t)?NULL:&log_update_del);
	return ok;
}

static size_t
dcount_col(sql_trans *tr, sql_column *c)
{
	sql_delta *b;
	assert(0);

	if (!isTable(c->t))
		return 0;
	b = col_timestamp_delta(tr, c);
	if (!b)
		return 1;
	assert(0);
	return 0;

	/* TDOO
		size_t dcnt = 0;
		dbl f = 1.0;
		BAT *v = cs_bind_bat( &b->cs, RDONLY, b->cnt), *o = v, *u;

		if ((dcnt = (size_t) BATcount(v)) > 1024*1024) {
			v = BATsample(v, 1024);
			f = dcnt/1024.0;
		}
		u = BATunique(v, NULL);
		bat_destroy(o);
		if (v!=o)
			bat_destroy(v);
		dcnt = (size_t) (BATcount(u) * f);
		bat_destroy(u);
		return dcnt;
	 * */
}

static size_t count_deletes(storage *d)
{
	if (d->cached_cnt)
		return d->cnt+d->ucnt;

	lng cnt = 0;
	BAT *b = temp_descriptor(d->cs.bid);
	if (!d->cs.ucnt) {
		if (BATsum(&cnt, TYPE_lng, b, NULL, true, false, false) != GDK_SUCCEED) {
			bat_destroy(b);
			return 0;
		}
		bat_destroy(b);
	} else {
		BAT *ui, *uv, *c;

		if (cs_real_update_bats(&d->cs, &ui, &uv) == LOG_ERR) {
			assert(0);
			bat_destroy(b);
			return 0;
		}
		c = COLcopy(b, b->ttype, true, TRANSIENT);
		bat_destroy(b);
		if (BATreplace(c, ui, uv, true) != GDK_SUCCEED) {
			bat_destroy(ui);
			bat_destroy(uv);
			bat_destroy(c);
			return 0;
		}
		bat_destroy(ui);
		bat_destroy(uv);
		if (BATsum(&cnt, TYPE_lng, c, NULL, true, false, false) != GDK_SUCCEED) {
			bat_destroy(c);
			return 0;
		}
		bat_destroy(c);
	}
	d->cached_cnt = 1;
	d->cnt = (size_t)cnt;
	return d->cnt;
}

static size_t
count_del(sql_trans *tr, sql_table *t, int access)
{
	storage *d;

	if (!isTable(t))
		return 0;
	d = tab_timestamp_storage(tr, t);
	if (!d)
		return 0;
	if (access == 2)
		return d->cs.ucnt;
	if (access == 1)
		return count_inserts(d->segs->head, tr);
	//lock_table(t->base.id);
	size_t cnt = count_deletes(d);
	//unlock_table(t->base.id);
	return cnt;
}

static int
sorted_col(sql_trans *tr, sql_column *col)
{
	int sorted = 0;

	assert(tr->active);
	if (!isTable(col->t) || !col->t->s)
		return 0;

	if (col && ATOMIC_PTR_GET(&col->data)) {
		BAT *b = bind_col(tr, col, QUICK);

		if (b)
			sorted = BATtordered(b) || BATtrevordered(b);
	}
	return sorted;
}

static int
unique_col(sql_trans *tr, sql_column *col)
{
	int distinct = 0;

	assert(tr->active);
	if (!isTable(col->t) || !col->t->s)
		return 0;

	if (col && ATOMIC_PTR_GET(&col->data)) {
		BAT *b = bind_col(tr, col, QUICK);

		if (b)
			distinct = b->tkey;
	}
	return distinct;
}

static int
double_elim_col(sql_trans *tr, sql_column *col)
{
	int de = 0;

	assert(tr->active);
	if (!isTable(col->t) || !col->t->s)
		return 0;

	if (col && ATOMIC_PTR_GET(&col->data)) {
		BAT *b = bind_col(tr, col, QUICK);

		if (b && b->tvarsized) /* check double elimination */
			de = GDK_ELIMDOUBLES(b->tvheap);
		if (de)
			de = b->twidth;
	}
	return de;
}

static int
load_cs(sql_trans *tr, column_storage *cs, int type, sqlid id)
{
	sqlstore *store = tr->store;
	int bid = logger_find_bat(store->logger, id);
	if (!bid)
		return LOG_ERR;
	cs->bid = temp_dup(bid);
	cs->ucnt = 0;
	cs->uibid = e_bat(TYPE_oid);
	cs->uvbid = e_bat(type);
	assert(cs->uibid && cs->uvbid);
	if(cs->uibid == BID_NIL || cs->uvbid == BID_NIL)
		return LOG_ERR;
	return LOG_OK;
}

static int
log_create_delta(sql_trans *tr, sql_delta *bat, sqlid id)
{
	int res = LOG_OK;
	gdk_return ok;
	BAT *b = temp_descriptor(bat->cs.bid);

	if (b == NULL)
		return LOG_ERR;

	if (!bat->cs.uibid)
		bat->cs.uibid = e_bat(TYPE_oid);
	if (!bat->cs.uvbid)
		bat->cs.uvbid = e_bat(b->ttype);
	if (bat->cs.uibid == BID_NIL || bat->cs.uvbid == BID_NIL)
		res = LOG_ERR;
	if (GDKinmemory(0)) {
		bat_destroy(b);
		return res;
	}

	bat_set_access(b, BAT_READ);
	sqlstore *store = tr->store;
	ok = log_bat_persists(store->logger, b, id);
	bat_destroy(b);
	if(res != LOG_OK)
		return res;
	return ok == GDK_SUCCEED ? LOG_OK : LOG_ERR;
}

static int
new_persistent_delta( sql_delta *bat)
{
	BAT *b = temp_descriptor(bat->cs.bid);

	if (b == NULL) {
		bat_destroy(b);
		return LOG_ERR;
	}
	bat->cs.ucnt = 0;
	bat_destroy(b);
	return LOG_OK;
}

static void
create_delta( sql_delta *d, BAT *b)
{
	d->cs.cnt = BATcount(b);
	bat_set_access(b, BAT_READ);
	d->cs.bid = temp_create(b);
	d->cs.uibid = d->cs.uvbid = 0;
	d->cs.ucnt = 0;
}

static bat
copyBat (bat i, int type, oid seq)
{
	BAT *b, *tb;
	bat res;

	if (!i)
		return i;
	tb = temp_descriptor(i);
	if (tb == NULL)
		return 0;
	b = BATconstant(seq, type, ATOMnilptr(type), BATcount(tb), PERSISTENT);
	bat_destroy(tb);
	if (b == NULL)
		return 0;

	bat_set_access(b, BAT_READ);

	res = temp_create(b);
	bat_destroy(b);
	return res;
}

static int
create_col(sql_trans *tr, sql_column *c)
{
	int ok = LOG_OK, new = 0;
	int type = c->type.type->localtype;
	sql_delta *bat = ATOMIC_PTR_GET(&c->data);

	if (!bat) {
		new = 1;
		bat = ZNEW(sql_delta);
		ATOMIC_PTR_SET(&c->data, bat);
		if(!bat)
			return LOG_ERR;
		bat->cs.refcnt = 1;
	}

	if (new)
		bat->cs.ts = tr->tid;

	if (!isNew(c) && !isTempTable(c->t)){
		bat->cs.ts = tr->ts;
		return load_cs(tr, &bat->cs, type, c->base.id);
	} else if (bat && bat->cs.bid && !isTempTable(c->t)) {
		return new_persistent_delta(ATOMIC_PTR_GET(&c->data));
	} else {
		sql_column *fc = NULL;
		size_t cnt = 0;

		/* alter ? */
		if (c->t->columns.set && (fc = c->t->columns.set->h->data) != NULL)
			cnt = count_col(tr, fc, 1);
		if (cnt && fc != c) {
			sql_delta *d = ATOMIC_PTR_GET(&fc->data);

			if (d->cs.bid) {
				bat->cs.bid = copyBat(d->cs.bid, type, 0);
				if(bat->cs.bid == BID_NIL)
					ok = LOG_ERR;
			}
			bat->cs.cnt = d->cs.cnt;
			if (d->cs.uibid) {
				bat->cs.uibid = e_bat(TYPE_oid);
				if (bat->cs.uibid == BID_NIL)
					ok = LOG_ERR;
			}
			if (d->cs.uvbid) {
				bat->cs.uvbid = e_bat(type);
				if(bat->cs.uvbid == BID_NIL)
					ok = LOG_ERR;
			}
			bat->cs.alter = 1;
		} else {
			BAT *b = bat_new(type, c->t->sz, PERSISTENT);
			if (!b) {
				ok = LOG_ERR;
			} else {
				create_delta(ATOMIC_PTR_GET(&c->data), b);
				bat_destroy(b);
			}

			if (!isTempTable(c->t)) {
				bat->cs.uibid = e_bat(TYPE_oid);
				if (bat->cs.uibid == BID_NIL)
					ok = LOG_ERR;
				bat->cs.uvbid = e_bat(type);
				if(bat->cs.uvbid == BID_NIL)
					ok = LOG_ERR;
			}
		}
		bat->cs.ucnt = 0;

		if (new /*&& !isTempTable(c->t)*/ && !isNew(c->t) /* alter */)
			trans_add(tr, &c->base, bat, &tc_gc_col, &commit_create_col, isTempTable(c->t)?NULL:&log_create_col);
	}
	return ok;
}

static int
log_create_col_(sql_trans *tr, sql_column *c)
{
	assert(!isTempTable(c->t));
	return log_create_delta(tr,  ATOMIC_PTR_GET(&c->data), c->base.id);
}

static int
log_create_col(sql_trans *tr, sql_change *change)
{
	return log_create_col_(tr, (sql_column*)change->obj);
}

static int
commit_create_col_( sql_trans *tr, sql_column *c, ulng commit_ts, ulng oldest)
{
	int ok = LOG_OK;
	(void)oldest;

	if(!isTempTable(c->t)) {
		sql_delta *delta = ATOMIC_PTR_GET(&c->data);
		assert(delta->cs.ts == tr->tid);
		delta->cs.ts = commit_ts;

		assert(delta->next == NULL);
		if (!delta->cs.alter)
			ok = tr_merge_delta(tr, delta);
		delta->cs.alter = 0;
		c->base.flags = 0;
	}
	return ok;
}

static int
commit_create_col( sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest)
{
	sql_column *c = (sql_column*)change->obj;
	c->base.flags = 0;
	return commit_create_col_( tr, c, commit_ts, oldest);
}

/* will be called for new idx's and when new index columns are created */
static int
create_idx(sql_trans *tr, sql_idx *ni)
{
	int ok = LOG_OK, new = 0;
	sql_delta *bat = ATOMIC_PTR_GET(&ni->data);
	int type = TYPE_lng;

	if (oid_index(ni->type))
		type = TYPE_oid;

	if (!bat) {
		new = 1;
		bat = ZNEW(sql_delta);
		ATOMIC_PTR_SET(&ni->data, bat);
		if(!bat)
			return LOG_ERR;
		bat->cs.refcnt = 1;
	}

	if (new)
		bat->cs.ts = tr->tid;

	if (!isNew(ni) && !isTempTable(ni->t)){
		bat->cs.ts = tr->ts;
		return load_cs(tr, &bat->cs, type, ni->base.id);
	} else if (bat && bat->cs.bid && !isTempTable(ni->t)) {
		return new_persistent_delta(ATOMIC_PTR_GET(&ni->data));
	} else {
		sql_column *c = ni->t->columns.set->h->data;
		sql_delta *d;

		d = col_timestamp_delta(tr, c);
		/* Here we also handle indices created through alter stmts */
		/* These need to be created aligned to the existing data */
		if (d->cs.bid) {
			bat->cs.bid = copyBat(d->cs.bid, type, 0);
			if(bat->cs.bid == BID_NIL)
				ok = LOG_ERR;
		}
		bat->cs.cnt = d->cs.cnt;
		bat->cs.ucnt = 0;
		if (!isNew(c))
			bat->cs.alter = 1;

		if (!isTempTable(ni->t)) {
			bat->cs.uibid = e_bat(TYPE_oid);
			if (bat->cs.uibid == BID_NIL)
				ok = LOG_ERR;
			bat->cs.uvbid = e_bat(type);
			if(bat->cs.uvbid == BID_NIL)
				ok = LOG_ERR;
		}
		bat->cs.ucnt = 0;
		if (new /*&& !isTempTable(ni->t)*/ && !isNew(ni->t) /* alter */)
			trans_add(tr, &ni->base, bat, &tc_gc_idx, &commit_create_idx, isTempTable(ni->t)?NULL:&log_create_idx);
	}
	return ok;
}

static int
log_create_idx_(sql_trans *tr, sql_idx *i)
{
	assert(!isTempTable(i->t));
	return log_create_delta(tr, ATOMIC_PTR_GET(&i->data), i->base.id);
}

static int
log_create_idx(sql_trans *tr, sql_change *change)
{
	return log_create_idx_(tr, (sql_idx*)change->obj);
}

static int
commit_create_idx_( sql_trans *tr, sql_idx *i, ulng commit_ts, ulng oldest)
{
	int ok = LOG_OK;
	(void)oldest;

	if(!isTempTable(i->t)) {
		sql_delta *delta = ATOMIC_PTR_GET(&i->data);
		assert(delta->cs.ts == tr->tid);
		delta->cs.ts = commit_ts;

		assert(delta->next == NULL);
		ok = tr_merge_delta(tr, delta);
		i->base.flags = 0;
	}
	return ok;
}

static int
commit_create_idx( sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest)
{
	sql_idx *i = (sql_idx*)change->obj;
	i->base.flags = 0;
	return commit_create_idx_(tr, i, commit_ts, oldest);
}

static int
load_storage(sql_trans *tr, storage *bat, sqlid id)
{
	int ok = load_cs(tr, &bat->cs, TYPE_msk, id);

	if (ok == LOG_OK) {
		bat->segs = new_segments(BATcount(quick_descriptor(bat->cs.bid)));
		bat->end = bat->segs->end;
	}
	return ok;
}

static int
create_del(sql_trans *tr, sql_table *t)
{
	int ok = LOG_OK, new = 0;
	BAT *b;
	storage *bat = ATOMIC_PTR_GET(&t->data);

	if (!bat) {
		new = 1;
		bat = ZNEW(storage);
		ATOMIC_PTR_SET(&t->data, bat);
		if(!bat)
			return LOG_ERR;
		bat->cs.refcnt = 1;
	}
	if (new)
		bat->cs.ts = tr->tid;

	if (!isNew(t) && !isTempTable(t)) {
		bat->cs.ts = tr->ts;
		return load_storage(tr, bat, t->base.id);
	} else if (bat->cs.bid && !isTempTable(t)) {
		return ok;
	} else if (!bat->cs.bid) {
		assert(!bat->segs && !bat->end);
		bat->segs = new_segments(0);
		bat->end = 0;
		bat->cnt = bat->ucnt = bat->icnt = 0;
		bat->cached_cnt = 1;

		b = bat_new(TYPE_msk, t->sz, PERSISTENT);
		if(b != NULL) {
			bat_set_access(b, BAT_READ);
			bat->cs.bid = temp_create(b);
			bat_destroy(b);
		} else {
			ok = LOG_ERR;
		}
		if (new /*&& !isTempTable(t)*/)
			trans_add(tr, &t->base, bat, &tc_gc_del, &commit_create_del, isTempTable(t)?NULL:&log_create_del);
	}
	return ok;
}

static int
log_create_storage(sql_trans *tr, storage *bat, oid id)
{
	BAT *b;
	gdk_return ok;

	if (GDKinmemory(0))
		return LOG_OK;

	b = temp_descriptor(bat->cs.bid);
	if (b == NULL)
		return LOG_ERR;

	sqlstore *store = tr->store;
	bat_set_access(b, BAT_READ);
	ok = log_bat_persists(store->logger, b, id);
	bat_destroy(b);
	return ok == GDK_SUCCEED ? LOG_OK : LOG_ERR;
}

static int
log_create_del(sql_trans *tr, sql_change *change)
{
	int ok = LOG_OK;
	sql_table *t = (sql_table*)change->obj;

	/*
	//sql_column *fc = ft->columns.set->h->data;
	if (log_batgroup(bat_logger, ft->bootstrap?0:LOG_TAB, ft->base.id, ft->cleared,
				log_get_nr_inserted(fc, &offset_inserted), offset_inserted,
				log_get_nr_deleted(ft, &offset_deleted), offset_deleted) != GDK_SUCCEED)
		ok = LOG_ERR;
		*/
	/* offset/end */

	assert(!isTempTable(t));
	ok = log_create_storage(tr, ATOMIC_PTR_GET(&t->data), t->base.id);
	//	ok = tr_log_storage(tr, ft->data, s?s->segs->head:NULL, ft->cleared, ft->base.id);
	if (ok == LOG_OK) {
		for(node *n = t->columns.set->h; n && ok == LOG_OK; n = n->next) {
			sql_column *c = n->data;

			ok = log_create_col_(tr, c);
		    //ok = tr_log_delta(tr, cc->data, s?s->segs->head:NULL, ft->cleared, cc->base.id);
		}
		if (t->idxs.set) {
			for(node *n = t->idxs.set->h; n && ok == LOG_OK; n = n->next) {
				sql_idx *i = n->data;

				if (ATOMIC_PTR_GET(&i->data))
					ok = log_create_idx_(tr, i);
			//ok = tr_log_delta(tr, ci->data, s?s->segs->head:NULL, ft->cleared, ci->base.id);
			}
		}
	}
	/*
	if (s)
		for (segment *segs = s->segs->head; segs; segs=segs->next)
			if (segs->owner == tr)
				segs->owner = NULL;
				*/
	return ok;
}

static int
commit_create_del( sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest)
{
	int ok = LOG_OK;
	sql_table *t = (sql_table*)change->obj;

	if (!commit_ts) /* rollback handled by ? */
		return ok;
	if(!isTempTable(t)) {
		storage *dbat = ATOMIC_PTR_GET(&t->data);
		assert(dbat->cs.ts == tr->tid);
		dbat->cs.ts = commit_ts;
		if (ok == LOG_OK) {
			for(node *n = t->columns.set->h; n && ok == LOG_OK; n = n->next) {
				sql_column *c = n->data;

				ok = commit_create_col_(tr, c, commit_ts, oldest);
			}
			if (t->idxs.set) {
				for(node *n = t->idxs.set->h; n && ok == LOG_OK; n = n->next) {
					sql_idx *i = n->data;

					if (ATOMIC_PTR_GET(&i->data))
						ok = commit_create_idx_(tr, i, commit_ts, oldest);
				}
			}
			t->base.flags = 0;
		}
	}
			t->base.flags = 0;
	return ok;
}

static int
log_destroy_delta(sql_trans *tr, sql_delta *b, sqlid id)
{
	gdk_return ok = GDK_SUCCEED;

	sqlstore *store = tr->store;
	if (!GDKinmemory(0) && b && b->cs.bid)
		ok = log_bat_transient(store->logger, id);
	return ok == GDK_SUCCEED ? LOG_OK : LOG_ERR;
}

static int
destroy_col(sqlstore *store, sql_column *c)
{
	(void)store;
	int ok = LOG_OK;
	if (ATOMIC_PTR_GET(&c->data))
		ok = destroy_delta(ATOMIC_PTR_GET(&c->data));
	ATOMIC_PTR_SET(&c->data, NULL);
	return ok;
}

static int
log_destroy_col_(sql_trans *tr, sql_column *c)
{
	int ok = LOG_OK;
	assert(!isTempTable(c->t));
	//delta->cs.ts = commit_ts;
	if (!tr->parent) /* don't write save point commits */
		ok = log_destroy_delta(tr, ATOMIC_PTR_GET(&c->data), c->base.id);
	return ok;
}

static int
log_destroy_col(sql_trans *tr, sql_change *change)
{
	return log_destroy_col_(tr, (sql_column*)change->obj);
}

static int
destroy_idx(sqlstore *store, sql_idx *i)
{
	(void)store;
	int ok = LOG_OK;
	if (ATOMIC_PTR_GET(&i->data))
		ok = destroy_delta(ATOMIC_PTR_GET(&i->data));
	ATOMIC_PTR_SET(&i->data, NULL);
	return ok;
}

static int
log_destroy_idx_(sql_trans *tr, sql_idx *i)
{
	int ok = LOG_OK;
	assert(!isTempTable(i->t));
	if (ATOMIC_PTR_GET(&i->data)) {
		//delta->cs.ts = commit_ts;
		if (!tr->parent) /* don't write save point commits */
			ok = log_destroy_delta(tr, ATOMIC_PTR_GET(&i->data), i->base.id);
	}
	return ok;
}

static int
log_destroy_idx(sql_trans *tr, sql_change *change)
{
	return log_destroy_idx_(tr, (sql_idx*)change->obj);
}


static int
cleanup(void)
{
	int ok = LOG_OK;
	return ok;
}

static int
destroy_del(sqlstore *store, sql_table *t)
{
	(void)store;
	int ok = LOG_OK;
	if (ATOMIC_PTR_GET(&t->data))
		ok = destroy_storage(ATOMIC_PTR_GET(&t->data));
	ATOMIC_PTR_SET(&t->data, NULL);
	return ok;
}

static int
log_destroy_storage(sql_trans *tr, storage *bat, oid id)
{
	gdk_return ok = GDK_SUCCEED;

	sqlstore *store = tr->store;
	if (!GDKinmemory(0) && !tr->parent && /* don't write save point commits */
	    bat && bat->cs.bid)
		ok = log_bat_transient(store->logger, id);
	return ok == GDK_SUCCEED ? LOG_OK : LOG_ERR;
}

static int
log_destroy_del(sql_trans *tr, sql_change *change)
{
	int ok = LOG_OK;
	sql_table *t = (sql_table*)change->obj;
	assert(!isTempTable(t));
	storage *dbat = ATOMIC_PTR_GET(&t->data);
	if (dbat->cs.ts < tr->ts) /* no changes ? */
		return ok;
	//dbat->cs.ts = commit_ts;
	ok = log_destroy_storage(tr, ATOMIC_PTR_GET(&t->data), t->base.id);

	if (ok == LOG_OK) {
		for(node *n = t->columns.set->h; n && ok == LOG_OK; n = n->next) {
			sql_column *c = n->data;

			ok = log_destroy_col_(tr, c);
		}
		if (t->idxs.set) {
			for(node *n = t->idxs.set->h; n && ok == LOG_OK; n = n->next) {
				sql_idx *i = n->data;

				ok = log_destroy_idx_(tr, i);
			}
		}
	}
	return ok;
}

static BUN
clear_cs(sql_trans *tr, column_storage *cs)
{
	BAT *b;
	BUN sz = 0;

	(void)tr;
	if (cs->bid) {
		b = temp_descriptor(cs->bid);
		if (b) {
			sz += BATcount(b);
			bat bid = cs->bid;
			cs->bid = temp_copy(bid, 1); /* create empty copy */
			temp_destroy(bid);
			bat_destroy(b);
		}
	}
	if (cs->uibid) {
		b = temp_descriptor(cs->uibid);
		if (b && !isEbat(b)) {
			bat_clear(b);
			BATcommit(b, BUN_NONE);
		}
		bat_destroy(b);
	}
	if (cs->uvbid) {
		b = temp_descriptor(cs->uvbid);
		if(b && !isEbat(b)) {
			bat_clear(b);
			BATcommit(b, BUN_NONE);
		}
		bat_destroy(b);
	}
	cs->cleared = 1;
	cs->ucnt = 0;
	return sz;
}

static BUN
clear_col(sql_trans *tr, sql_column *c)
{
	sql_delta *delta, *odelta = ATOMIC_PTR_GET(&c->data);

	if ((delta = bind_col_data(tr, c)) == NULL)
		return BUN_NONE;
	if ((!inTransaction(tr, c->t) && (odelta != delta || isTempTable(c->t)) && isGlobal(c->t)) || (!isNew(c->t) && isLocalTemp(c->t)))
		trans_add(tr, &c->base, delta, &tc_gc_col, &commit_update_col, isLocalTemp(c->t)?NULL:&log_update_col);
	if (delta)
		return clear_cs(tr, &delta->cs);
	return 0;
}

static BUN
clear_idx(sql_trans *tr, sql_idx *i)
{
	sql_delta *delta, *odelta = ATOMIC_PTR_GET(&i->data);

	if (!isTable(i->t) || (hash_index(i->type) && list_length(i->columns) <= 1) || !idx_has_column(i->type))
		return 0;
	if ((delta = bind_idx_data(tr, i)) == NULL)
		return BUN_NONE;
	if ((!inTransaction(tr, i->t) && (odelta != delta || isTempTable(i->t)) && isGlobal(i->t)) || (!isNew(i->t) && isLocalTemp(i->t)))
		trans_add(tr, &i->base, delta, &tc_gc_idx, &commit_update_idx, isLocalTemp(i->t)?NULL:&log_update_idx);
	if (delta)
		return clear_cs(tr, &delta->cs);
	return 0;
}

static BUN
clear_storage(sql_trans *tr, storage *s)
{
	BUN sz = s->cnt;

	clear_cs(tr, &s->cs);
	s->cs.cleared = 1;
	s->cs.cnt = 0;
	if (s->segs)
		destroy_segments(s->segs);
	s->segs = new_segments(0);
	s->end = 0;
	s->cnt = s->ucnt = s->icnt = 0;
	return sz;
}

static BUN
clear_del(sql_trans *tr, sql_table *t)
{
	storage *bat, *obat = ATOMIC_PTR_GET(&t->data);

	if ((bat = bind_del_data(tr, t)) == NULL)
		return BUN_NONE;
	if ((!inTransaction(tr, t) && (obat != bat || isTempTable(t)) && isGlobal(t)) || (!isNew(t) && isLocalTemp(t)))
		trans_add(tr, &t->base, bat, &tc_gc_del, &commit_update_del, isLocalTemp(t)?NULL:&log_update_del);
	return clear_storage(tr, bat);
}

static BUN
clear_table(sql_trans *tr, sql_table *t)
{
	node *n = t->columns.set->h;
	sql_column *c = n->data;
	BUN sz = count_col(tr, c, 0);

	sz -= count_del(tr, t, 0);
	if ((clear_del(tr, t)) == BUN_NONE)
		return BUN_NONE;

	for (; n; n = n->next) {
		c = n->data;

		if (clear_col(tr, c) == BUN_NONE)
			return BUN_NONE;
	}
	if (t->idxs.set) {
		for (n = t->idxs.set->h; n; n = n->next) {
			sql_idx *ci = n->data;

			if (isTable(ci->t) && idx_has_column(ci->type) &&
				clear_idx(tr, ci) == BUN_NONE)
				return BUN_NONE;
		}
	}
	return sz;
}

static gdk_return
tr_log_cs( sql_trans *tr, column_storage *cs, segment *segs, sqlid id)
{
	sqlstore *store = tr->store;
	gdk_return ok = GDK_SUCCEED;

	if (GDKinmemory(0))
		return LOG_OK;

	if (cs->cleared && log_bat_clear(store->logger, id) != GDK_SUCCEED)
		return LOG_ERR;

	if (cs->cleared) {
		assert(cs->ucnt == 0);
		BAT *ins = temp_descriptor(cs->bid);
		if (isEbat(ins)) {
			temp_destroy(cs->bid);
			cs->bid = temp_copy(ins->batCacheid, false);
			bat_destroy(ins);
			ins = temp_descriptor(cs->bid);
		}
		bat_set_access(ins, BAT_READ);
		ok = log_bat_persists(store->logger, ins, id);
		bat_destroy(ins);
		return ok == GDK_SUCCEED ? LOG_OK : LOG_ERR;
	}

	for (; segs; segs=segs->next) {
		if (segs->owner == tr) {
			BAT *ins = temp_descriptor(cs->bid);
			assert(ins);
			ok = log_bat(store->logger, ins, id, segs->start, segs->end-segs->start);
			bat_destroy(ins);
		}
	}

	if (ok == GDK_SUCCEED && cs->ucnt && cs->uibid) {
		BAT *ui = temp_descriptor(cs->uibid);
		BAT *uv = temp_descriptor(cs->uvbid);
		/* any updates */
		if (ui == NULL || uv == NULL) {
			ok = GDK_FAIL;
		} else if (BUNlast(uv) > uv->batInserted || BATdirty(uv))
			ok = log_delta(store->logger, ui, uv, id);
		bat_destroy(ui);
		bat_destroy(uv);
	}
	return ok == GDK_SUCCEED ? LOG_OK : LOG_ERR;
}

static int
tr_log_delta( sql_trans *tr, sql_delta *cbat, segment *segs, sqlid id)
{
	return tr_log_cs( tr, &cbat->cs, segs, id);
}

static int
tr_log_storage(sql_trans *tr, storage *fdb, sqlid id)
{
	return tr_log_cs( tr, &fdb->cs, fdb->segs->head, id);
}

static int
tr_merge_cs( sql_trans *tr, column_storage *cs)
{
	int ok = LOG_OK;
	BAT *cur = NULL;

	(void)tr;
	if (cs->bid) {
		cur = temp_descriptor(cs->bid);
		if(!cur)
			return LOG_ERR;
	}

	if (cs->ucnt) {
		BAT *ui = temp_descriptor(cs->uibid);
		BAT *uv = temp_descriptor(cs->uvbid);

		if(!ui || !uv) {
			bat_destroy(ui);
			bat_destroy(uv);
			bat_destroy(cur);
			return LOG_ERR;
		}
		assert(BATcount(ui) == BATcount(uv));

		/* any updates */
		assert(!isEbat(cur));
		if (BATreplace(cur, ui, uv, true) != GDK_SUCCEED) {
			bat_destroy(ui);
			bat_destroy(uv);
			bat_destroy(cur);
			return LOG_ERR;
		}
		/* cleanup the old deltas */
		temp_destroy(cs->uibid);
		temp_destroy(cs->uvbid);
		cs->uibid = e_bat(TYPE_oid);
		cs->uvbid = e_bat(cur->ttype);
		if(cs->uibid == BID_NIL || cs->uvbid == BID_NIL)
			ok = LOG_ERR;
		cs->ucnt = 0;
		bat_destroy(ui);
		bat_destroy(uv);
	}
	cs->cleared = 0;
	bat_destroy(cur);
	return ok;
}

static int
tr_merge_delta( sql_trans *tr, sql_delta *obat)
{
	return tr_merge_cs(tr, &obat->cs);
}

static int
tr_merge_storage(sql_trans *tr, storage *tdb)
{
	int ok = tr_merge_cs(tr, &tdb->cs);

	if (tdb->next) {
		ok = destroy_storage(tdb->next);
		tdb->next = NULL;
	}
	return ok;
}

static sql_delta *
savepoint_commit_delta( sql_delta *delta, ulng commit_ts)
{
	/* commit ie copy back to the parent transaction */
	if (delta && delta->cs.ts == commit_ts && delta->next) {
		sql_delta *od = delta->next;
		if (od->cs.ts == commit_ts) {
			sql_delta t = *od, *n = od->next;
			*od = *delta;
			od->next = n;
			*delta = t;
			delta->next = NULL;
			destroy_delta(delta);
			return od;
		}
	}
	return delta;
}

static int
rollback_delta(sql_trans *tr, sql_delta *delta, int type)
{
	(void)tr;
	if (delta->cs.ucnt) {
		delta->cs.ucnt = 0;
		temp_destroy(delta->cs.uibid);
		temp_destroy(delta->cs.uvbid);
		delta->cs.uibid = e_bat(TYPE_oid);
		delta->cs.uvbid = e_bat(type);
	}
	return LOG_OK;
}

static int
commit_delta(sql_trans *tr, sql_delta *delta)
{
	return tr_merge_delta(tr, delta);
}

static int
log_update_col( sql_trans *tr, sql_change *change)
{
	sql_column *c = (sql_column*)change->obj;

	if (!isTempTable(c->t) && !tr->parent) {/* don't write save point commits */
		storage *s = ATOMIC_PTR_GET(&c->t->data);
		return tr_log_delta(tr, ATOMIC_PTR_GET(&c->data), s->segs->head, c->base.id);
	}
	return LOG_OK;
}

static int
commit_update_col_( sql_trans *tr, sql_column *c, ulng commit_ts, ulng oldest)
{
	int ok = LOG_OK;
	sql_delta *delta = ATOMIC_PTR_GET(&c->data);

	(void)oldest;
	if (isTempTable(c->t)) {
		if (commit_ts) { /* commit */
			if (c->t->commit_action == CA_COMMIT || c->t->commit_action == CA_PRESERVE)
				commit_delta(tr, delta);
			else /* CA_DELETE as CA_DROP's are gone already (or for globals are equal to a CA_DELETE) */
				clear_cs(tr, &delta->cs);
		} else { /* rollback */
			if (c->t->commit_action == CA_COMMIT/* || c->t->commit_action == CA_PRESERVE*/)
				rollback_delta(tr, delta, c->type.type->localtype);
			else /* CA_DELETE as CA_DROP's are gone already (or for globals are equal to a CA_DELETE) */
				clear_cs(tr, &delta->cs);
		}
		c->t->base.flags = c->base.flags = 0;
	}
	return ok;
}

static int
commit_update_col( sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest)
{
	int ok = LOG_OK;
	sql_column *c = (sql_column*)change->obj;
	sql_delta *delta = ATOMIC_PTR_GET(&c->data);

	if (isTempTable(c->t))
		return commit_update_col_(tr, c, commit_ts, oldest);
	if (commit_ts)
		delta->cs.ts = commit_ts;
	if (!commit_ts) { /* rollback */
		sql_delta *d = change->data, *o = ATOMIC_PTR_GET(&c->data);

		if (o != d) {
			while(o && o->next != d)
				o = o->next;
		}
		if (o == ATOMIC_PTR_GET(&c->data))
			ATOMIC_PTR_SET(&c->data, d->next);
		else
			o->next = d->next;
		d->next = NULL;
		destroy_delta(d);
	} else if (ok == LOG_OK && !tr->parent) {
		sql_delta *d = delta;
		/* clean up and merge deltas */
		while (delta && delta->cs.ts > oldest) {
			delta = delta->next;
		}
		if (delta && delta != d) {
			if (delta->next) {
				ok = destroy_delta(delta->next);
				delta->next = NULL;
			}
		}
		if (ok == LOG_OK && delta == d && oldest == commit_ts)
			ok = tr_merge_delta(tr, delta);
	} else if (ok == LOG_OK && tr->parent) /* move delta into older and cleanup current save points */
		ATOMIC_PTR_SET(&c->data, savepoint_commit_delta(delta, commit_ts));
	return ok;
}

static int
log_update_idx( sql_trans *tr, sql_change *change)
{
	sql_idx *i = (sql_idx*)change->obj;

	if (!isTempTable(i->t) && !tr->parent) { /* don't write save point commits */
		storage *s = ATOMIC_PTR_GET(&i->t->data);
		return tr_log_delta(tr, ATOMIC_PTR_GET(&i->data), s->segs->head, i->base.id);
	}
	return LOG_OK;
}

static int
commit_update_idx_( sql_trans *tr, sql_idx *i, ulng commit_ts, ulng oldest)
{
	int ok = LOG_OK;
	sql_delta *delta = ATOMIC_PTR_GET(&i->data);
	int type = (oid_index(i->type))?TYPE_oid:TYPE_lng;

	(void)oldest;
	if (isTempTable(i->t)) {
		if (commit_ts) { /* commit */
			if (i->t->commit_action == CA_COMMIT || i->t->commit_action == CA_PRESERVE)
				commit_delta(tr, delta);
			else /* CA_DELETE as CA_DROP's are gone already */
				clear_cs(tr, &delta->cs);
		} else { /* rollback */
			if (i->t->commit_action == CA_COMMIT/* || i->t->commit_action == CA_PRESERVE*/)
				rollback_delta(tr, delta, type);
			else /* CA_DELETE as CA_DROP's are gone already */
				clear_cs(tr, &delta->cs);
		}
		i->t->base.flags = i->base.flags = 0;
	}
	return ok;
}

static int
commit_update_idx( sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest)
{
	int ok = LOG_OK;
	sql_idx *i = (sql_idx*)change->obj;
	sql_delta *delta = ATOMIC_PTR_GET(&i->data);

	if (isTempTable(i->t))
		return commit_update_idx_( tr, i, commit_ts, oldest);
	if (commit_ts)
		delta->cs.ts = commit_ts;
	if (!commit_ts) { /* rollback */
		sql_delta *d = change->data, *o = ATOMIC_PTR_GET(&i->data);

		if (o != d) {
			while(o && o->next != d)
				o = o->next;
		}
		if (o == ATOMIC_PTR_GET(&i->data))
			ATOMIC_PTR_SET(&i->data, d->next);
		else
			o->next = d->next;
		d->next = NULL;
		destroy_delta(d);
	} else if (ok == LOG_OK && !tr->parent) {
		sql_delta *d = delta;
		/* clean up and merge deltas */
		while (delta && delta->cs.ts > oldest) {
			delta = delta->next;
		}
		if (delta && delta != d) {
			if (delta->next) {
				ok = destroy_delta(delta->next);
				delta->next = NULL;
			}
		}
		if (ok == LOG_OK && delta == d && oldest == commit_ts)
			ok = tr_merge_delta(tr, delta);
	} else if (ok == LOG_OK && tr->parent) /* cleanup older save points */
		ATOMIC_PTR_SET(&i->data, savepoint_commit_delta(delta, commit_ts));
	return ok;
}

static storage *
savepoint_commit_storage( storage *dbat, ulng commit_ts)
{
	if (dbat && dbat->cs.ts == commit_ts && dbat->next) {
		storage *od = dbat->next;
		if (od->cs.ts == commit_ts) {
			storage t = *od, *n = od->next;
			*od = *dbat;
			od->next = n;
			*dbat = t;
			dbat->next = NULL;
			destroy_storage(dbat);
			return od;
		}
	}
	return dbat;
}

static int
log_update_del( sql_trans *tr, sql_change *change)
{
	sql_table *t = (sql_table*)change->obj;

	if (!isTempTable(t) && !tr->parent) /* don't write save point commits */
		return tr_log_storage(tr, ATOMIC_PTR_GET(&t->data), t->base.id);
	return LOG_OK;
}

static int
rollback_storage(sql_trans *tr, storage *dbat)
{
	(void)tr;
	(void)dbat;
	return LOG_OK;
}

static int
commit_storage(sql_trans *tr, storage *dbat)
{
	(void)tr;
	(void)dbat;
	return LOG_OK;
}

static int
commit_update_del( sql_trans *tr, sql_change *change, ulng commit_ts, ulng oldest)
{
	int ok = LOG_OK;
	sql_table *t = (sql_table*)change->obj;
	storage *dbat = ATOMIC_PTR_GET(&t->data);

	if (isTempTable(t)) {
		if (commit_ts) { /* commit */
			if (t->commit_action == CA_COMMIT || t->commit_action == CA_PRESERVE)
				commit_storage(tr, dbat);
			else /* CA_DELETE as CA_DROP's are gone already */
				clear_storage(tr, dbat);
		} else { /* rollback */
			if (t->commit_action == CA_COMMIT/* || t->commit_action == CA_PRESERVE*/)
				rollback_storage(tr, dbat);
			else /* CA_DELETE as CA_DROP's are gone already */
				clear_storage(tr, dbat);
		}
		t->base.flags = 0;
		return ok;
	}
	if (!isTempTable(t))
		dbat->cs.ts = commit_ts;
	if (!commit_ts) { /* rollback */
		storage *d = change->data, *o = ATOMIC_PTR_GET(&t->data);

		if (o != d) {
			while(o && o->next != d)
				o = o->next;
		}
		if (o == ATOMIC_PTR_GET(&t->data))
			ATOMIC_PTR_SET(&t->data, d->next);
		else
			o->next = d->next;
		d->next = NULL;
		destroy_storage(d);
	} else if (ok == LOG_OK && !tr->parent) {
		storage *d = dbat;
		/* clean up and merge deltas */
		while (dbat && dbat->cs.ts > oldest) {
			dbat = dbat->next;
		}
		if (dbat && dbat != d) {
			if (dbat->next) {
				ok = destroy_storage(dbat->next);
				dbat->next = NULL;
			}
		}
		if (ok == LOG_OK && dbat == d && oldest == commit_ts)
			ok = tr_merge_storage(tr, dbat);
	} else if (ok == LOG_OK && tr->parent) {/* cleanup older save points */
		ATOMIC_PTR_SET(&t->data, savepoint_commit_storage(dbat, commit_ts));
	}
	return ok;
}

/* only rollback (content version) case for now */
static int
tc_gc_col( sql_store Store, sql_change *change, ulng commit_ts, ulng oldest)
{
	sqlstore *store = Store;
	sql_column *c = (sql_column*)change->obj;

	(void)store;
	/* savepoint commit (did it merge ?) */
	if (ATOMIC_PTR_GET(&c->data) != change->data || isTempTable(c->t)) /* data is freed by commit */
		return 1;
	if (commit_ts && commit_ts >= TRANSACTION_ID_BASE) /* cannot cleanup older stuff on savepoint commits */
		return 0;
	sql_delta *d = (sql_delta*)change->data;
	if (d->next) {
		if (d->cs.ts > oldest)
			return LOG_OK; /* cannot cleanup yet */

		destroy_delta(d->next);
		d->next = NULL;
	}
	return 1;
}

static int
tc_gc_idx( sql_store Store, sql_change *change, ulng commit_ts, ulng oldest)
{
	sqlstore *store = Store;
	sql_idx *i = (sql_idx*)change->obj;

	(void)store;
	/* savepoint commit (did it merge ?) */
	if (ATOMIC_PTR_GET(&i->data) != change->data || isTempTable(i->t)) /* data is freed by commit */
		return 1;
	if (commit_ts && commit_ts >= TRANSACTION_ID_BASE) /* cannot cleanup older stuff on savepoint commits */
		return 0;
	sql_delta *d = (sql_delta*)change->data;
	if (d->next) {
		if (d->cs.ts > oldest)
			return LOG_OK; /* cannot cleanup yet */

		destroy_delta(d->next);
		d->next = NULL;
	}
	return 1;
}

static int
tc_gc_del( sql_store Store, sql_change *change, ulng commit_ts, ulng oldest)
{
	sqlstore *store = Store;
	sql_table *t = (sql_table*)change->obj;

	(void)store;
	/* savepoint commit (did it merge ?) */
	if (ATOMIC_PTR_GET(&t->data) != change->data || isTempTable(t)) /* data is freed by commit */
		return 1;
	if (commit_ts && commit_ts >= TRANSACTION_ID_BASE) /* cannot cleanup older stuff on savepoint commits */
		return 0;
	storage *d = (storage*)change->data;
	if (d->next) {
		if (d->cs.ts > oldest)
			return LOG_OK; /* cannot cleanup yet */

		destroy_storage(d->next);
		d->next = NULL;
	}
	return 1;
}

static int
claim_cs(column_storage *cs, size_t cnt)
{
	BAT *b = temp_descriptor(cs->bid);

	if (!b)
		return LOG_ERR;
	const void *nilptr = ATOMnilptr(b->ttype);

	for(size_t i=0; i<cnt; i++){
		if (BUNappend(b, nilptr, TRUE) != GDK_SUCCEED) {
			bat_destroy(b);
			return LOG_ERR;
		}
	}
	bat_destroy(b);
	return LOG_OK;
}

static int
table_claim_space(sql_trans *tr, sql_table *t, size_t cnt)
{
	node *n = t->columns.set->h;
	sql_column *c = n->data;
	sql_delta *d;

	for (n = t->columns.set->h; n; n = n->next) {
		c = n->data;

		if ((d=bind_col_data(tr, c)) == NULL)
			return LOG_ERR;
		if (claim_cs(&d->cs, cnt) == LOG_ERR)
			return LOG_ERR;
	}
	if (t->idxs.set) {
		for (n = t->idxs.set->h; n; n = n->next) {
			sql_idx *ci = n->data;

			if (isTable(ci->t) && idx_has_column(ci->type)) {
				if ((d=bind_idx_data(tr, ci)) == NULL)
					return LOG_ERR;
				if (claim_cs(&d->cs, cnt) == LOG_ERR)
					return LOG_ERR;
			}
		}
	}
	return LOG_OK;
}

/*
 * Claim cnt slots to store the tuples. The claim_tab should claim storage on the level
 * of the global transaction and mark the newly added storage slots unused on the global
 * level but used on the local transaction level. Besides this the local transaction needs
 * to update (and mark unused) any slot inbetween the old end and new slots.
 * */
static size_t
claim_tab(sql_trans *tr, sql_table *t, size_t cnt)
{
	storage *s, *ps = ATOMIC_PTR_GET(&t->data);
	BUN slot = 0;

	if ((s = bind_del_data(tr, t)) == NULL)
		return BUN_NONE;

	/* use (resizeable) array of locks like BBP */
	//lock_table(t->base.id);
	/* make lockless ? */
	slot = ps->end;
	if (isNew(t) || isTempTable(t) || s->cs.cleared) {
		ps->end += cnt;
		if (ps->segs->head)
			ps->segs->end = ps->segs->head->end = ps->end;
	} else {
		assert(ps->end <= ps->segs->end);
		if (ps->segs->head->owner == tr) {
			ps->segs->head->end += cnt;
		} else {
			ps->segs->head = new_segment(ps->segs->head, tr, cnt);
		}
		s->end = ps->end = ps->segs->end = ps->segs->head->end;
	}

	BAT *b = temp_descriptor(s->cs.bid); /* use s->cs.bid, as its equal ps->cs.bid or for cleared tables its a private bid */

	assert(isNew(t) || isTempTable(t) || s->cs.cleared || BATcount(b) == slot);

	msk deleted = FALSE;
	/* general case, write deleted in the central bat (ie others don't see these values) and
	 * insert rows into the update bats */
	if (!s->cs.cleared && ps != s && !isTempTable(t)) {
		/* add updates */
		BAT *ui, *uv;

		if (/* DISABLES CODE */ (0) && table_claim_space(tr, t, cnt) == LOG_ERR) {
			//unlock_table(t->base.id);
			bat_destroy(b);
			return LOG_ERR;
		}
		if (cs_real_update_bats(&s->cs, &ui, &uv) == LOG_ERR) {
			//unlock_table(t->base.id);
			bat_destroy(b);
			return LOG_ERR;
		}

		oid id = slot;
		BAT *uin = BATdense(0, id, cnt);
		BAT *uvn = mask_bat(cnt, deleted);
		if (!uin || !uvn ||
				BATappend(ui, uin, NULL, true) != GDK_SUCCEED ||
				BATappend(uv, uvn, NULL, true) != GDK_SUCCEED) {
			assert(!isVIEW(uin) && !isVIEW(uvn));
			bat_destroy(uin);
			bat_destroy(uvn);
			bat_destroy(ui);
			bat_destroy(uv);
			bat_destroy(b);
			//unlock_table(t->base.id);
			return LOG_ERR;
		}
		bat_destroy(uin);
		bat_destroy(uvn);
		bat_destroy(ui);
		bat_destroy(uv);
#if 0
		for(lng i=0; i<(lng)cnt; i++, id++) {
			/* create void-bat ui (id,cnt), msk-0's (write in chunks of 32bit */
			if (BUNappend(ui, &id, true) != GDK_SUCCEED ||
			    BUNappend(uv, &deleted, true) != GDK_SUCCEED) {
				bat_destroy(ui);
				bat_destroy(uv);
				//unlock_table(t->base.id);
				return LOG_ERR;
			}
		}
#endif
		s->cs.ucnt += cnt;
		s->icnt += cnt;
	}

	assert(isNew(t) || isTempTable(t) || s->cs.cleared || BATcount(b) == slot);
	if (isNew(t) || isTempTable(t) || s->cs.cleared || s == ps)
		deleted = FALSE;
	else { /* persistent central copy needs space marked deleted (such that other transactions don't see these rows) */
		deleted = TRUE;
		ps->cnt += cnt;
	}
	/* TODO first up to 32 boundary, then int writes */
	for(lng i=0; i<(lng)cnt; i++) {
		if (BUNappend(b, &deleted, true) != GDK_SUCCEED) {
			bat_destroy(b);
			//unlock_table(t->base.id);
			return LOG_ERR;
		}
	}
	assert(!isVIEW(b));
	bat_destroy(b);
	assert(isTempTable(t) || s->cs.cleared || ps->cs.bid == s->cs.bid);

	/* inserts only write */
	//unlock_table(t->base.id);
	if ((!inTransaction(tr, t) && (ps != s || isTempTable(t)) && isGlobal(t)) || (!isNew(t) && isLocalTemp(t)))
		trans_add(tr, &t->base, s, &tc_gc_del, &commit_update_del, isLocalTemp(t)?NULL:&log_update_del);
	return (size_t)slot;
}

void
bat_storage_init( store_functions *sf)
{
	sf->bind_col = (bind_col_fptr)&bind_col;
	sf->bind_idx = (bind_idx_fptr)&bind_idx;
	sf->bind_del = (bind_del_fptr)&bind_del;

	sf->claim_tab = (claim_tab_fptr)&claim_tab;

	sf->append_col = (append_col_fptr)&append_col;
	sf->append_idx = (append_idx_fptr)&append_idx;

	sf->append_col_prep = (modify_col_prep_fptr)&append_col_prepare;
	sf->append_idx_prep = (modify_idx_prep_fptr)&append_idx_prepare;
	sf->append_col_exec = (append_col_exec_fptr)&append_col_execute;
	sf->update_col = (update_col_fptr)&update_col;
	sf->update_idx = (update_idx_fptr)&update_idx;

	sf->update_col_prep = (modify_col_prep_fptr)&update_col_prepare;
	sf->update_idx_prep = (modify_idx_prep_fptr)&update_idx_prepare;
	sf->update_col_exec = (update_col_exec_fptr)&update_col_execute;

	sf->delete_tab = (delete_tab_fptr)&delete_tab;

	sf->count_del = (count_del_fptr)&count_del;
	sf->count_col = (count_col_fptr)&count_col;
	sf->count_idx = (count_idx_fptr)&count_idx;
	sf->dcount_col = (dcount_col_fptr)&dcount_col;
	sf->sorted_col = (prop_col_fptr)&sorted_col;
	sf->unique_col = (prop_col_fptr)&unique_col;
	sf->double_elim_col = (prop_col_fptr)&double_elim_col;

	sf->col_dup = (col_dup_fptr)&col_dup;
	sf->idx_dup = (idx_dup_fptr)&idx_dup;
	sf->del_dup = (del_dup_fptr)&del_dup;

	sf->create_col = (create_col_fptr)&create_col;
	sf->create_idx = (create_idx_fptr)&create_idx;
	sf->create_del = (create_del_fptr)&create_del;

	sf->destroy_col = (destroy_col_fptr)&destroy_col;
	sf->destroy_idx = (destroy_idx_fptr)&destroy_idx;
	sf->destroy_del = (destroy_del_fptr)&destroy_del;

	/* change into drop_* */
	sf->log_destroy_col = (log_destroy_col_fptr)&log_destroy_col;
	sf->log_destroy_idx = (log_destroy_idx_fptr)&log_destroy_idx;
	sf->log_destroy_del = (log_destroy_del_fptr)&log_destroy_del;

	sf->clear_table = (clear_table_fptr)&clear_table;

	sf->cleanup = (cleanup_fptr)&cleanup;
	/*
	for(int i=0;i<NR_TABLE_LOCKS;i++)
		MT_lock_init(&table_locks[i], "table_lock");
		*/
}

#if 0
static int
tr_update_storage( sql_trans *tr, storage *ts, storage *fs)
{
	if (fs->cs.cleared) {
		destroy_segments(ts->segs);
		MT_lock_set(&segs_lock);
		ts->segs = dup_segments(fs->segs);
		MT_lock_unset(&segs_lock);
		ts->end = ts->segs->end;
		assert(ts->segs->head);
		ts->cnt = 0;
		ts->ucnt = 0;
		ts->icnt = 0;
	} else {
		assert(ts->segs == fs->segs);
		/* merge segments or cleanup ? */
		segment *segs = ts->segs->head, *seg = segs;
		for (; segs; segs = segs->next) {
			if (segs->owner == tr || !segs->owner) {
				/* merge range */
				segs->owner = NULL;
				if (seg == segs) /* skip first */
					continue;
				if (seg->end == segs->start) {
					seg->end = segs->end;
					seg->next = segs->next;
					segs->next = NULL;
					destroy_segs(segs);
					segs = seg;
				} else {
					seg = segs; /* begin of new merge */
				}
			}
		}
		ts->end = ts->segs->end;
	}
	ts->cnt += fs->ucnt;
	ts->cnt -= fs->icnt;
	int ok = tr_update_cs( tr, &ts->cs, &fs->cs, ts->end);
	if (ok == LOG_OK && ts->next) {
		ok = destroy_storage(tr, ts->next);
		ts->next = NULL;
	}
	return ok;
}

#endif

#if 0
static lng
log_get_nr_inserted(sql_column *fc, lng *offset)
{
	lng cnt = 0;

	if (!fc || GDKinmemory(0))
		return 0;

	if (fc->base.atime && fc->base.allocated) {
		sql_delta *fb = fc->data;
		BAT *ins = temp_descriptor(fb->cs.bid);

		if (ins && BUNlast(ins) > 0 && BUNlast(ins) > ins->batInserted) {
			cnt = BUNlast(ins) - ins->batInserted;
		}
		bat_destroy(ins);
	}
	return cnt;
}

static lng
log_get_nr_deleted(sql_table *ft, lng *offset)
{
	lng cnt = 0;

	if (!ft || GDKinmemory(0))
		return 0;

	if (ft->base.atime && ft->base.allocated) {
		storage *fdb = ft->data;
		BAT *db = temp_descriptor(fdb->cs.bid);

		if (db && BUNlast(db) > 0 && BUNlast(db) > db->batInserted) {
			cnt = BUNlast(db) - db->batInserted;
			*offset = db->batInserted;
		}
		bat_destroy(db);
	}
	return cnt;
}
#endif
