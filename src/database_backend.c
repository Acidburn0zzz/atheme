/*
 * Copyright (c) 2010 William Pitcock <nenolod@atheme.org>
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Platform-agnostic database backend layer.
 */

#include "atheme.h"

database_module_t *db_mod = NULL;
mowgli_patricia_t *db_types = NULL;

database_handle_t *
db_open(database_transaction_t txn)
{
	return_val_if_fail(db_mod != NULL, NULL);

	return db_mod->db_open(txn);
}

void
db_close(database_handle_t *db)
{
	return_if_fail(db_mod != NULL);

	return db_mod->db_close(db);
}

const char *
db_read_word(database_handle_t *db)
{
	return_val_if_fail(db != NULL, NULL);
	return_val_if_fail(db->vt != NULL, NULL);
	return_val_if_fail(db->vt->read_word != NULL, NULL);

	return db->vt->read_word(db);
}

const char *
db_read_multiword(database_handle_t *db)
{
	return_val_if_fail(db != NULL, NULL);
	return_val_if_fail(db->vt != NULL, NULL);
	return_val_if_fail(db->vt->read_multiword != NULL, NULL);

	return db->vt->read_multiword(db);
}

int
db_read_int(database_handle_t *db)
{
	return_val_if_fail(db != NULL, 0);
	return_val_if_fail(db->vt != NULL, 0);
	return_val_if_fail(db->vt->read_int != NULL, 0);

	return db->vt->read_int(db);
}

bool
db_start_row(database_handle_t *db, const char *type)
{
	return_val_if_fail(db != NULL, false);
	return_val_if_fail(db->vt != NULL, false);
	return_val_if_fail(db->vt->start_row != NULL, false);

	return db->vt->start_row(db, type);
}

bool
db_write_word(database_handle_t *db, const char *word)
{
	return_val_if_fail(db != NULL, false);
	return_val_if_fail(db->vt != NULL, false);
	return_val_if_fail(db->vt->write_word != NULL, false);

	return db->vt->write_word(db, word);
}

bool
db_write_multiword(database_handle_t *db, const char *str)
{
	return_val_if_fail(db != NULL, false);
	return_val_if_fail(db->vt != NULL, false);
	return_val_if_fail(db->vt->write_multiword != NULL, false);

	return db->vt->write_multiword(db, str);
}

bool
db_write_int(database_handle_t *db, int num)
{
	return_val_if_fail(db != NULL, false);
	return_val_if_fail(db->vt != NULL, false);
	return_val_if_fail(db->vt->write_int != NULL, false);

	return db->vt->write_int(db, num);
}

bool
db_commit_row(database_handle_t *db)
{
	return_val_if_fail(db != NULL, false);
	return_val_if_fail(db->vt != NULL, false);
	return_val_if_fail(db->vt->commit_row != NULL, false);

	return db->vt->commit_row(db);
}

void
db_register_type_handler(const char *type, database_handler_f fun)
{
	return_if_fail(db_types != NULL);
	return_if_fail(type != NULL);
	return_if_fail(fun != NULL);

	mowgli_patricia_add(db_types, type, fun);
}

void
db_unregister_type_handler(const char *type)
{
	return_if_fail(db_types != NULL);
	return_if_fail(type != NULL);

	mowgli_patricia_delete(db_types, type);
}

void
db_process(database_handle_t *db, const char *type)
{
	database_handler_f fun;

	return_if_fail(db_types != NULL);
	return_if_fail(db != NULL);
	return_if_fail(type != NULL);

	fun = mowgli_patricia_retrieve(db_types, type);
	fun(db, type);
}

bool
db_write_format(database_handle_t *db, const char *fmt, ...)
{
	va_list va;
	char buf[BUFSIZE];

	va_start(va, fmt);
	vsnprintf(buf, BUFSIZE, fmt, va);
	va_end(va);

	return db_write_word(db, buf);
}

void
db_init(void)
{
	db_types = mowgli_patricia_create(strcasecanon);

	if (db_types == NULL)
	{
		slog(LG_ERROR, "db_init(): object allocator failure");
		exit(EXIT_FAILURE);
	}
}
