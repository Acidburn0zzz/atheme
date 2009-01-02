/*
 * Copyright (C) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Config reader.
 */

#ifndef CONFPROCESS_H
#define CONFPROCESS_H

struct Token
{
	const char *text;
	int value;
};

struct ConfTable;

E void init_confprocess(void);
E struct ConfTable *find_top_conf(const char *name);
E struct ConfTable *find_conf_item(const char *name, list_t *conflist);
E void add_top_conf(const char *name, int (*handler)(config_entry_t *ce));
E void add_subblock_top_conf(const char *name, list_t *list);
E void add_conf_item(const char *name, list_t *conflist, int (*handler)(config_entry_t *ce));
E void add_uint_conf_item(const char *name, list_t *conflist, unsigned int *var, unsigned int min, unsigned int max);
E void add_duration_conf_item(const char *name, list_t *conflist, unsigned int *var, const char *defunit);
E void add_dupstr_conf_item(const char *name, list_t *conflist, char **var);
E void add_bool_conf_item(const char *name, list_t *conflist, bool *var);
E void del_top_conf(const char *name);
E void del_conf_item(const char *name, list_t *conflist);
E int subblock_handler(config_entry_t *ce, list_t *entries);
E bool process_uint_configentry(config_entry_t *ce, unsigned int *var,
		unsigned int min, unsigned int max);
E bool process_duration_configentry(config_entry_t *ce, unsigned int *var,
		const char *defunit);
E void conf_report_warning(config_entry_t *ce, const char *fmt, ...) PRINTFLIKE (2, 3);
/* sort of a hack for servtree.c */
typedef int (*conf_handler_t)(config_entry_t *);
E conf_handler_t conftable_get_conf_handler(struct ConfTable *ct);

E void conf_process(config_file_t *cfp);

E int token_to_value(struct Token token_table[], const char *token);
/* special return values for token_to_value */
#define TOKEN_UNMATCHED -1
#define TOKEN_ERROR -2

#endif

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs ts=8 sw=8 noexpandtab
 */
