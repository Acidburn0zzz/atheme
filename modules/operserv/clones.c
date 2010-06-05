/*
 * Copyright (c) 2006-2007 Atheme Development Group
 * Rights to this code are documented in doc/LICENCE.
 *
 * This file contains functionality implementing clone detection.
 *
 * $Id: clones.c 8027 2007-04-02 10:47:18Z nenolod $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"operserv/clones", false, _modinit, _moddeinit,
	PACKAGE_STRING,
	"Atheme Development Group <http://www.atheme.org>"
);

#define DEFAULT_WARN_CLONES	3 /* IPs with more than this are warned about */
#define EXEMPT_GRACE		10 /* exempt IPs exceeding their allowance by this are banned */

static void clones_newuser(hook_user_nick_t *data);
static void clones_userquit(user_t *u);

static void os_cmd_clones(sourceinfo_t *si, int parc, char *parv[]);
static void os_cmd_clones_kline(sourceinfo_t *si, int parc, char *parv[]);
static void os_cmd_clones_list(sourceinfo_t *si, int parc, char *parv[]);
static void os_cmd_clones_addexempt(sourceinfo_t *si, int parc, char *parv[]);
static void os_cmd_clones_delexempt(sourceinfo_t *si, int parc, char *parv[]);
static void os_cmd_clones_listexempt(sourceinfo_t *si, int parc, char *parv[]);
static void os_cmd_clones_duration(sourceinfo_t *si, int parc, char *parv[]);

static void write_exemptdb(void);
static void load_exemptdb(void);

list_t *os_cmdtree;
list_t *os_helptree;
list_t os_clones_cmds;

static list_t clone_exempts;
bool kline_enabled;
mowgli_patricia_t *hostlist;
BlockHeap *hostentry_heap;
static long kline_duration;

typedef struct cexcept_ cexcept_t;
struct cexcept_
{
	char *ip;
	unsigned int clones;
	char *reason;
};

typedef struct hostentry_ hostentry_t;
struct hostentry_
{
	char ip[HOSTIPLEN];
	list_t clients;
	time_t lastaction;
	unsigned int lastaction_clones;
};


command_t os_clones = { "CLONES", N_("Manages network wide clones."), PRIV_AKILL, 4, os_cmd_clones };

command_t os_clones_kline = { "KLINE", N_("Enables/disables klines for excessive clones."), AC_NONE, 1, os_cmd_clones_kline };
command_t os_clones_list = { "LIST", N_("Lists clones on the network."), AC_NONE, 0, os_cmd_clones_list };
command_t os_clones_addexempt = { "ADDEXEMPT", N_("Adds a clones exemption."), AC_NONE, 3, os_cmd_clones_addexempt };
command_t os_clones_delexempt = { "DELEXEMPT", N_("Deletes a clones exemption."), AC_NONE, 1, os_cmd_clones_delexempt };
command_t os_clones_listexempt = { "LISTEXEMPT", N_("Lists clones exemptions."), AC_NONE, 0, os_cmd_clones_listexempt };
command_t os_clones_duration = { "DURATION", N_("Sets a custom duration to ban clones for."), AC_NONE, 1, os_cmd_clones_duration };

void _modinit(module_t *m)
{
	user_t *u;
	mowgli_patricia_iteration_state_t state;

	MODULE_USE_SYMBOL(os_cmdtree, "operserv/main", "os_cmdtree");
	MODULE_USE_SYMBOL(os_helptree, "operserv/main", "os_helptree");

	command_add(&os_clones, os_cmdtree);

	command_add(&os_clones_kline, &os_clones_cmds);
	command_add(&os_clones_list, &os_clones_cmds);
	command_add(&os_clones_addexempt, &os_clones_cmds);
	command_add(&os_clones_delexempt, &os_clones_cmds);
	command_add(&os_clones_listexempt, &os_clones_cmds);
	command_add(&os_clones_duration, &os_clones_cmds);

	help_addentry(os_helptree, "CLONES", "help/oservice/clones", NULL);

	hook_add_event("user_add");
	hook_add_user_add(clones_newuser);
	hook_add_event("user_delete");
	hook_add_user_delete(clones_userquit);

	hostlist = mowgli_patricia_create(noopcanon);
	hostentry_heap = BlockHeapCreate(sizeof(hostentry_t), HEAP_USER);

	kline_duration = 3600; // set a default

	load_exemptdb();

	/* add everyone to host hash */
	MOWGLI_PATRICIA_FOREACH(u, &state, userlist)
	{
		clones_newuser(&(hook_user_nick_t){ .u = u });
	}
}

static void free_hostentry(const char *key, void *data, void *privdata)
{
	node_t *n, *tn;
	hostentry_t *he = data;

	LIST_FOREACH_SAFE(n, tn, he->clients.head)
	{
		node_del(n, &he->clients);
		node_free(n);
	}
	BlockHeapFree(hostentry_heap, he);
}

void _moddeinit(void)
{
	node_t *n, *tn;

	mowgli_patricia_destroy(hostlist, free_hostentry, NULL);
	BlockHeapDestroy(hostentry_heap);

	LIST_FOREACH_SAFE(n, tn, clone_exempts.head)
	{
		cexcept_t *c = n->data;

		free(c->ip);
		free(c->reason);
		free(c);

		node_del(n, &clone_exempts);
		node_free(n);
	}

	command_delete(&os_clones, os_cmdtree);

	command_delete(&os_clones_kline, &os_clones_cmds);
	command_delete(&os_clones_list, &os_clones_cmds);
	command_delete(&os_clones_addexempt, &os_clones_cmds);
	command_delete(&os_clones_delexempt, &os_clones_cmds);
	command_delete(&os_clones_listexempt, &os_clones_cmds);
	command_delete(&os_clones_duration, &os_clones_cmds);

	help_delentry(os_helptree, "CLONES");

	hook_del_user_add(clones_newuser);
	hook_del_user_delete(clones_userquit);
}

static void write_exemptdb(void)
{
	FILE *f;
	node_t *n;
	cexcept_t *c;
	int was_errored;

	if (!(f = fopen(DATADIR "/exempts.db.new", "w")))
	{
		slog(LG_ERROR, "write_exemptdb(): cannot open exempts database for writing: %s", strerror(errno));
		return;
	}

	fprintf(f, "CK %d\n", kline_enabled ? 1 : 0);
	fprintf(f, "CD %ld\n", kline_duration);
	LIST_FOREACH(n, clone_exempts.head)
	{
		c = n->data;
		fprintf(f, "EX %s %d %s\n", c->ip, c->clones, c->reason);
	}

	was_errored = ferror(f);
	if (!was_errored)
		was_errored = fflush(f);
	/* fsync it before deleting the old */
	if (!was_errored)
		was_errored = fsync(fileno(f));
	was_errored |= fclose(f);
	if (was_errored)
	{
		slog(LG_ERROR, "write_exemptdb(): couldn't write exempts database: %s", strerror(errno));
		return;
	}

	if ((rename(DATADIR "/exempts.db.new", DATADIR "/exempts.db")) < 0)
	{
		slog(LG_ERROR, "write_exemptdb(): couldn't rename exempts database: %s", strerror(errno));
		return;
	}
}

static void load_exemptdb(void)
{
	FILE *f;
	char *item, rBuf[BUFSIZE * 2], *p;

	if (!(f = fopen(DATADIR "/exempts.db", "r")))
	{
		slog(LG_DEBUG, "load_exemptdb(): cannot open exempt database: %s", strerror(errno));
		return;
	}

	while (fgets(rBuf, BUFSIZE * 2, f))
	{
		item = strtok(rBuf, " ");
		strip(item);

		if (!strcmp(item, "CD"))
		{
			char *s = strtok(NULL, " ");
			if (s)
				kline_duration = atol(s);
		}
		if (!strcmp(item, "EX"))
		{
			char *ip = strtok(NULL, " ");
			int clones = atoi(strtok(NULL, " "));
			char *reason = strtok(NULL, "");

			if (!ip || clones <= 0 || !reason)
				; /* erroneous, don't add */
			else
			{
				cexcept_t *c = (cexcept_t *)malloc(sizeof(cexcept_t));

				c->ip = sstrdup(ip);
				c->clones = clones;
				p = strchr(reason, '\n');
				if (p != NULL)
					*p = '\0';
				c->reason = sstrdup(reason);
				node_add(c, node_create(), &clone_exempts);
			}
		}
		else if (!strcmp(item, "CK"))
		{
			char *enable = strtok(NULL, " ");

			if (enable != NULL)
				kline_enabled = atoi(enable) != 0;
		}
	}

	fclose(f);
}

static unsigned int is_exempt(const char *ip)
{
	node_t *n;

	/* first check for an exact match */
	LIST_FOREACH(n, clone_exempts.head)
	{
		cexcept_t *c = n->data;

		if (!strcmp(ip, c->ip))
			return c->clones;
	}
	/* then look for cidr */
	LIST_FOREACH(n, clone_exempts.head)
	{
		cexcept_t *c = n->data;

		if (!match_ips(c->ip, ip))
			return c->clones;
	}

	return 0;
}

static void os_cmd_clones(sourceinfo_t *si, int parc, char *parv[])
{
	command_t *c;
	char *cmd = parv[0];
	
	/* Bad/missing arg */
	if (!cmd)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CLONES");
		command_fail(si, fault_needmoreparams, _("Syntax: CLONES KLINE|LIST|ADDEXEMPT|DELEXEMPT|LISTEXEMPT [parameters]"));
		return;
	}
	
	c = command_find(&os_clones_cmds, cmd);
	if (c == NULL)
	{
		command_fail(si, fault_badparams, _("Invalid command. Use \2/%s%s help\2 for a command listing."), (ircd->uses_rcommand == false) ? "msg " : "", opersvs.me->disp);
		return;
	}

	command_exec(si->service, si, c, parc + 1, parv + 1);
}

static void os_cmd_clones_kline(sourceinfo_t *si, int parc, char *parv[])
{
	const char *arg = parv[0];

	if (arg == NULL)
		arg = "";

	if (!strcasecmp(arg, "ON"))
	{
		if (kline_enabled)
		{
			command_fail(si, fault_nochange, _("CLONES klines are already enabled."));
			return;
		}
		kline_enabled = true;
		command_success_nodata(si, _("Enabled CLONES klines."));
		wallops("\2%s\2 enabled CLONES klines", get_oper_name(si));
		logcommand(si, CMDLOG_ADMIN, "CLONES:KLINE:ON");
		write_exemptdb();
	}
	else if (!strcasecmp(arg, "OFF"))
	{
		if (!kline_enabled)
		{
			command_fail(si, fault_nochange, _("CLONES klines are already disabled."));
			return;
		}
		kline_enabled = false;
		command_success_nodata(si, _("Disabled CLONES klines."));
		wallops("\2%s\2 disabled CLONES klines", get_oper_name(si));
		logcommand(si, CMDLOG_ADMIN, "CLONES:KLINE:OFF");
		write_exemptdb();
	}
	else
	{
		if (kline_enabled)
			command_success_string(si, "ON", _("CLONES klines are currently enabled."));
		else
			command_success_string(si, "OFF", _("CLONES klines are currently disabled."));
	}
}

static void os_cmd_clones_list(sourceinfo_t *si, int parc, char *parv[])
{
	hostentry_t *he;
	int k = 0;
	mowgli_patricia_iteration_state_t state;
	int allowed = 0;

	MOWGLI_PATRICIA_FOREACH(he, &state, hostlist)
	{
		k = LIST_LENGTH(&he->clients);

		if (k > 3)
		{
			if ((allowed = is_exempt(he->ip)))
				command_success_nodata(si, _("%d from %s (\2EXEMPT\2; allowed %d)"), k, he->ip, allowed);
			else
				command_success_nodata(si, _("%d from %s"), k, he->ip);
		}
	}
	command_success_nodata(si, _("End of CLONES LIST"));
	logcommand(si, CMDLOG_ADMIN, "CLONES:LIST");
}

static void os_cmd_clones_addexempt(sourceinfo_t *si, int parc, char *parv[])
{
	node_t *n;
	char *ip = parv[0];
	char *clonesstr = parv[1];
	int clones;
	char *reason = parv[2];
	cexcept_t *c = NULL;

	if (!ip || !clonesstr)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CLONES ADDEXEMPT");
		command_fail(si, fault_needmoreparams, _("Syntax: CLONES ADDEXEMPT <ip> <clones> <reason>"));
		return;
	}

	clones = atoi(clonesstr);
	if (clones <= DEFAULT_WARN_CLONES)
	{
		command_fail(si, fault_badparams, _("Allowed clones count must be more than %d"), DEFAULT_WARN_CLONES);
		return;
	}

	LIST_FOREACH(n, clone_exempts.head)
	{
		cexcept_t *t = n->data;

		if (!strcmp(ip, t->ip))
			c = t;
	}

	if (c == NULL)
	{
		if (!reason)
		{
			command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CLONES ADDEXEMPT");
			command_fail(si, fault_needmoreparams, _("Syntax: CLONES ADDEXEMPT <ip> <clones> <reason>"));
			return;
		}
		c = smalloc(sizeof(cexcept_t));
		c->ip = sstrdup(ip);
		c->reason = sstrdup(reason);
		node_add(c, node_create(), &clone_exempts);
		command_success_nodata(si, _("Added \2%s\2 to clone exempt list."), ip);
	}
	else
	{
		if (reason)
		{
			free(c->reason);
			c->reason = sstrdup(reason);
		}
		command_success_nodata(si, _("Updated \2%s\2 in clone exempt list."), ip);
	}
	c->clones = clones;

	logcommand(si, CMDLOG_ADMIN, "CLONES:ADDEXEMPT: \2%s\2 \2%d\2 (reason: \2%s\2)", ip, clones, c->reason);
	write_exemptdb();
}

static void os_cmd_clones_delexempt(sourceinfo_t *si, int parc, char *parv[])
{
	node_t *n, *tn;
	char *arg = parv[0];

	if (!arg)
		return;

	LIST_FOREACH_SAFE(n, tn, clone_exempts.head)
	{
		cexcept_t *c = n->data;

		if (!strcmp(c->ip, arg))
		{
			free(c->ip);
			free(c->reason);
			free(c);
			node_del(n, &clone_exempts);
			node_free(n);
			command_success_nodata(si, _("Removed \2%s\2 from clone exempt list."), arg);
			logcommand(si, CMDLOG_ADMIN, "CLONES:DELEXEMPT: \2%s\2", arg);
			write_exemptdb();
			return;
		}
	}

	command_fail(si, fault_nosuch_target, _("\2%s\2 not found in clone exempt list."), arg);
}

static void os_cmd_clones_duration(sourceinfo_t *si, int parc, char *parv[])
{
	char *s = parv[0];
	long duration;

	if (!s)
	{
		command_success_nodata(si, _("Clone ban duration set to \2%ld\2 (%ld seconds)"), kline_duration / 60, kline_duration);
		return;
	}

	duration = (atol(s) * 60);
	while (isdigit(*s))
		s++;
	if (*s == 'h' || *s == 'H')
		duration *= 60;
	else if (*s == 'd' || *s == 'D')
		duration *= 1440;
	else if (*s == 'w' || *s == 'W')
		duration *= 10080;
	else if (*s == '\0' || *s == 'm' || *s == 'M')
		;
	else
		duration = 0;

	if (duration <= 0)
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "CLONES DURATION");
		return;
	}

	kline_duration = duration;
	command_success_nodata(si, _("Clone ban duration set to \2%s\2 (%ld seconds)"), parv[0], kline_duration);
}

static void os_cmd_clones_listexempt(sourceinfo_t *si, int parc, char *parv[])
{
	node_t *n;

	LIST_FOREACH(n, clone_exempts.head)
	{
		cexcept_t *c = n->data;

		command_success_nodata(si, "%s (%d, %s)", c->ip, c->clones, c->reason);
	}
	command_success_nodata(si, _("End of CLONES LISTEXEMPT"));
	logcommand(si, CMDLOG_ADMIN, "CLONES:LISTEXEMPT");
}

static void clones_newuser(hook_user_nick_t *data)
{
	user_t *u = data->u;
	unsigned int i;
	hostentry_t *he;
	unsigned int allowed = 0;

	/* If the user has been killed, don't do anything. */
	if (!u)
		return;

	/* User has no IP, ignore him */
	if (is_internal_client(u) || *u->ip == '\0')
		return;

	he = mowgli_patricia_retrieve(hostlist, u->ip);
	if (he == NULL)
	{
		he = BlockHeapAlloc(hostentry_heap);
		strlcpy(he->ip, u->ip, sizeof he->ip);
		mowgli_patricia_add(hostlist, he->ip, he);
	}
	node_add(u, node_create(), &he->clients);
	i = LIST_LENGTH(&he->clients);

	if (i > DEFAULT_WARN_CLONES)
	{
		allowed = is_exempt(u->ip);

		if (allowed == 0 || i > allowed)
		{
			if (he->lastaction + 300 < CURRTIME)
				he->lastaction_clones = i;
			else if (i <= he->lastaction_clones)
				return;
			else
				he->lastaction_clones = i;
			he->lastaction = CURRTIME;
			if (allowed == 0 && i < config_options.default_clone_limit)
				slog(LG_INFO, "CLONES: \2%d\2 clones on \2%s\2 (%s!%s@%s)", i, u->ip, u->nick, u->user, u->host);
			else if (allowed != 0 && i < allowed + EXEMPT_GRACE)
				slog(LG_INFO, "CLONES: \2%d\2 clones on \2%s\2 (%s!%s@%s) (\2%d\2 allowed)", i, u->ip, u->nick, u->user, u->host, allowed);
			else if (!kline_enabled)
				slog(LG_INFO, "CLONES: \2%d\2 clones on \2%s\2 (%s!%s@%s) (TKLINE disabled)", i, u->ip, u->nick, u->user, u->host);
			else if (is_autokline_exempt(u))
				slog(LG_INFO, "CLONES: \2%d\2 clones on \2%s\2 (%s!%s@%s) (user is autokline exempt)", i, u->ip, u->nick, u->user, u->host);
			else
			{
				slog(LG_INFO, "CLONES: \2%d\2 clones on \2%s\2 (%s!%s@%s) (TKLINE due to excess clones)", i, u->ip, u->nick, u->user, u->host);
				kline_sts("*", "*", u->ip, kline_duration, "Excessive clones");
			}
		}
	}
}

static void clones_userquit(user_t *u)
{
	node_t *n;
	hostentry_t *he;

	/* User has no IP, ignore him */
	if (is_internal_client(u) || *u->ip == '\0')
		return;

	he = mowgli_patricia_retrieve(hostlist, u->ip);
	if (he == NULL)
	{
		slog(LG_DEBUG, "clones_userquit(): hostentry for %s not found??", u->ip);
		return;
	}
	n = node_find(u, &he->clients);
	if (n)
	{
		node_del(n, &he->clients);
		node_free(n);
		if (LIST_LENGTH(&he->clients) == 0)
		{
			mowgli_patricia_delete(hostlist, he->ip);
			BlockHeapFree(hostentry_heap, he);
		}
	}
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
