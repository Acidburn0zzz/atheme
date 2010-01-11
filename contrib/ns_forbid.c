/*
 * Copyright (c) 2005-2007 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the NickServ FORBID function.
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/forbid", false, _modinit, _moddeinit,
	"$Id$",
	"Atheme Development Group <http://www.atheme.org>"
);

#define FORBID_EMAIL "noemail"

static void ns_cmd_forbid(sourceinfo_t *si, int parc, char *parv[]);

command_t ns_forbid = { "FORBID", "Disallows use of a nickname.", PRIV_USER_ADMIN, 3, ns_cmd_forbid };

list_t *ns_cmdtree, *ns_helptree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_cmdtree, "nickserv/main", "ns_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");

	command_add(&ns_forbid, ns_cmdtree);
	help_addentry(ns_helptree, "FORBID", "help/contrib/forbid", NULL);
}

void _moddeinit()
{
	command_delete(&ns_forbid, ns_cmdtree);
	help_delentry(ns_helptree, "FORBID");
}

static void make_forbid(sourceinfo_t *si, const char *account, const char *reason)
{
	myuser_t *mu;
	mynick_t *mn = NULL;
	user_t *u;

	if (!nicksvs.no_nick_ownership && IsDigit(*account))
	{
		command_fail(si, fault_badparams, "For security reasons, you can't forbid a UID.");
		return;
	}

	if (strchr(account, ' ') || strchr(account, '\n') || strchr(account, '\r') || account[0] == '=' || account[0] == '#' || account[0] == '@' || account[0] == '+' || account[0] == '%' || account[0] == '!' || strchr(account, ','))
	{
		command_fail(si, fault_badparams, "The account name \2%s\2 is invalid.", account);
		return;
	}

	/* make sure it isn't registered already */
	if (nicksvs.no_nick_ownership ? myuser_find(account) != NULL : mynick_find(account) != NULL)
	{
		command_fail(si, fault_alreadyexists, "\2%s\2 is already registered.", account);
		return;
	}

	mu = myuser_add(account, "*", FORBID_EMAIL, MU_CRYPTPASS | MU_ENFORCE | MU_HOLD | MU_NOBURSTLOGIN);
	mu->registered = CURRTIME;
	mu->lastlogin = CURRTIME;
	metadata_add(mu, "private:freeze:freezer", get_oper_name(si));
	metadata_add(mu, "private:freeze:reason", reason);
	metadata_add(mu, "private:freeze:timestamp", itoa(CURRTIME));
	if (!nicksvs.no_nick_ownership)
	{
		mn = mynick_add(mu, mu->name);
		mn->registered = CURRTIME;
		mn->lastseen = CURRTIME;
		u = user_find_named(mu->name);
		if (u != NULL)
		{
			notice(si->service->nick, u->nick,
					_("The nick \2%s\2 is now forbidden."),
					mu->name);
			hook_call_nick_enforce((&(hook_nick_enforce_t){ .u = u, .mn = mn }));
		}
	}

	snoop("FORBID:ON: \2%s\2 by \2%s\2 (%s)",
			account, get_oper_name(si), reason);
	logcommand(si, CMDLOG_ADMIN | CMDLOG_REGISTER, "FORBID set ON \2%s\2 (reason: %s)", account, reason);
	wallops("%s forbade the nickname \2%s\2 (%s).", get_oper_name(si), account, reason);
	command_success_nodata(si, "\2%s\2 is now forbidden.", mu->name);
	/* don't call hooks, hmm */
}

static void destroy_forbid(sourceinfo_t *si, const char *account)
{
	myuser_t *mu;
	metadata_t *md;

	mu = myuser_find(account);
	if (!mu)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), account);
		return;
	}

	md = metadata_find(mu, "private:freeze:freezer");
	if (md == NULL || mu->registered != mu->lastlogin ||
			LIST_LENGTH(&mu->nicks) != 1 ||
			strcmp(mu->email, FORBID_EMAIL))
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not a forbidden nickname."), account);
		return;
	}
	snoop("FORBID:OFF: \2%s\2 by \2%s\2", mu->name, get_oper_name(si));
	logcommand(si, CMDLOG_ADMIN | CMDLOG_REGISTER, "FORBID turned OFF for \2%s\2", mu->name);
	wallops("%s unforbade the nickname \2%s\2.", get_oper_name(si), account);
	command_success_nodata(si, "\2%s\2 is no longer forbidden.", mu->name);
	/* no hooks here either, hmm */
	object_unref(mu);
}

static void ns_cmd_forbid(sourceinfo_t *si, int parc, char *parv[])
{
	const char *account;
	const char *action;
	const char *reason;

	account = parv[0], action = parv[1], reason = parv[2];

	if (!account || !action)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FORBID");
		command_fail(si, fault_needmoreparams, "Syntax: FORBID <nickname> ON|OFF [reason]");
		return;
	}

	if (!strcasecmp(action, "ON"))
	{
		if (!reason)
		{
			command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FORBID");
			command_fail(si, fault_needmoreparams, _("Usage: FORBID <nickname> ON <reason>"));
			return;
		}
		make_forbid(si, account, reason);
	}
	else if (!strcasecmp(action, "OFF"))
		destroy_forbid(si, account);
	else
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FORBID");
		command_fail(si, fault_needmoreparams, _("Usage: FORBID <nickname> ON|OFF [reason]"));
	}
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
