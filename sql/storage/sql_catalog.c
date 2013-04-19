/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2013 MonetDB B.V.
 * All Rights Reserved.
 */

#include "monetdb_config.h"
#include "sql_catalog.h"

char *TID = "%TID%";

static void *
_list_find_name(list *l, char *name)
{
	node *n;

	if (l && !l->ht && list_length(l) > HASH_MIN_SIZE && l->sa) {
		l->ht = hash_new(l->sa, list_length(l), (fkeyvalue)&base_key);

		for (n = l->h; n; n = n->next ) {
			sql_base *b = n->data;
			int key = base_key(b);

			hash_add(l->ht, key, b);
		}
	}
	if (l && l->ht) {
		int key = hash_key(name);
		sql_hash_e *he = l->ht->buckets[key&(l->ht->size-1)]; 

		for (; he; he = he->chain) {
			sql_base *b = he->value;

			if (b->name && strcmp(b->name, name) == 0) 
				return b;
		}
		return NULL;
	}

	if (l)
		for (n = l->h; n; n = n->next) {
			sql_base *b = n->data;

			/* check if names match */
			if (name[0] == b->name[0] && strcmp(name, b->name) == 0) {
				return b;
			}
		}
	return NULL;
}

static void *
_cs_find_name(changeset * cs, char *name)
{
	return _list_find_name(cs->set, name);
}


node *
cs_find_name(changeset * cs, char *name)
{
	return list_find_name(cs->set, name);
}

node *
list_find_name(list *l, char *name)
{
	node *n;

	if (l)
		for (n = l->h; n; n = n->next) {
			sql_base *b = n->data;

			/* check if names match */
			if (name[0] == b->name[0] && strcmp(name, b->name) == 0) {
				return n;
			}
		}
	return NULL;
}

node *
cs_find_id(changeset * cs, int id)
{
	node *n;
	list *l = cs->set;

	if (l)
		for (n = l->h; n; n = n->next) {
			sql_base *b = n->data;

			/* check if names match */
			if (b->id == id) {
				return n;
			}
		}
	if (cs->dset) {
		l = cs->dset;
		for (n = l->h; n; n = n->next) {
			sql_base *b = n->data;

			/* check if names match */
			if (b->id == id) {
				return n;
			}
		}
	}
	return NULL;
}

node *
list_find_id(list *l, int id)
{
	if (l) {
		node *n;
		for (n = l->h; n; n = n->next) {

			/* check if ids match */
			if (id == *(int *) n->data) {
				return n;
			}
		}
	}
	return NULL;
}

node *
list_find_base_id(list *l, int id)
{
	if (l) {
		node *n;
		for (n = l->h; n; n = n->next) {
			sql_base *b = n->data;

			if (id == b->id) 
				return n;
		}
	}
	return NULL;
}


sql_key *
find_sql_key(sql_table *t, char *kname)
{
	return _cs_find_name(&t->keys, kname);
}

sql_idx *
find_sql_idx(sql_table *t, char *iname)
{
	return _cs_find_name(&t->idxs, iname);
}

sql_column *
find_sql_column(sql_table *t, char *cname)
{
	return _cs_find_name(&t->columns, cname);
}

sql_table *
find_sql_table(sql_schema *s, char *tname)
{
	return _cs_find_name(&s->tables, tname);
}

sql_table *
find_sql_table_id(sql_schema *s, int id)
{
	node *n = cs_find_id(&s->tables, id);

	if (n)
		return n->data;
	return NULL;
}

node *
find_sql_table_node(sql_schema *s, int id)
{
	return cs_find_id(&s->tables, id);
}

sql_sequence *
find_sql_sequence(sql_schema *s, char *sname)
{
	return _cs_find_name(&s->seqs, sname);
}

sql_schema *
find_sql_schema(sql_trans *t, char *sname)
{
	return _cs_find_name(&t->schemas, sname);
}

sql_schema *
find_sql_schema_id(sql_trans *t, int id)
{
	node *n = cs_find_id(&t->schemas, id);

	if (n)
		return n->data;
	return NULL;
}

node *
find_sql_schema_node(sql_trans *t, int id)
{
	return cs_find_id(&t->schemas, id);
}

static sql_type *
find_sqlname(list *l, char *name)
{
	if (l) {
		node *n;

		for (n = l->h; n; n = n->next) {
			sql_type *t = n->data;

			if (strcmp(t->sqlname, name) == 0)
				return t;
		} 
	}
	return NULL;
}

sql_type *
find_sql_type(sql_schema * s, char *tname)
{
	return find_sqlname(s->types.set, tname);
}

sql_type *
sql_trans_bind_type(sql_trans *tr, sql_schema *c, char *name)
{
	node *n;
	sql_type *t = NULL;

	if (tr->schemas.set)
		for (n = tr->schemas.set->h; n && !t; n = n->next) {
			sql_schema *s = n->data;

			t = find_sql_type(s, name);
		}

	if (!t && c)
		t = find_sql_type(c, name);
	return t;
}

node *
find_sql_func_node(sql_schema * s, int id)
{
	return cs_find_id(&s->funcs, id);
}

sql_func *
find_sql_func(sql_schema * s, char *tname)
{
	return _cs_find_name(&s->funcs, tname);
}

list *
find_all_sql_func(sql_schema * s, char *name, int type)
{
	list *l = s->funcs.set, *res = NULL;
	node *n = NULL;

	if (l) {
		for (n = l->h; n; n = n->next) {
			sql_base *b = n->data;
			sql_func *f = n->data;

			/* check if names match */
			if (f->type == type && name[0] == b->name[0] && strcmp(name, b->name) == 0) {
				if (!res)
					res = list_create((fdestroy)NULL);
				list_append(res, n->data);
			}
		}
	}
	return res;
}

sql_func *
sql_trans_bind_func(sql_trans *tr, char *name)
{
	node *n;
	sql_func *t = NULL;

	if (tr->schemas.set)
		for (n = tr->schemas.set->h; n && !t; n = n->next) {
			sql_schema *s = n->data;

			t = find_sql_func(s, name);
		}
	if (!t)
		return NULL;
	return t;
}

/* returns the name of a temp_t as a string */
char *tt2string(int tt)
{
	switch(tt) {
	case tt_table:         return "TABLE";
	case tt_view:          return "VIEW";
	case tt_generated:     return "GENERATED";
	case tt_merge_table:   return "MERGE_TABLE";
	case tt_stream:        return "STREAM";
	case tt_remote:        return "REMOTE";
	case tt_replica_table: return "REPLICA_TABLE";
	case tt_array:         return "ARRAY";
	}
	return "UNKNOWN_TT";
}

