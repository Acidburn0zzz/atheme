/*
 * Copyright (c) 2005 Atheme Development Group
 * Rights to this code are documented in doc/LICENSE.
 *
 * This file contains the main() routine.
 *
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"memoserv/main", false, _modinit, _moddeinit,
	PACKAGE_STRING,
	"Atheme Development Group <http://www.atheme.org>"
);

static void on_user_identify(user_t *u);
static void on_user_away(user_t *u);

list_t ms_cmdtree;
list_t ms_helptree;
list_t ms_conftable;

/* main services client routine */
static void memoserv(sourceinfo_t *si, int parc, char *parv[])
{
	char *cmd;
        char *text;
	char orig[BUFSIZE];

	/* this should never happen */
	if (parv[0][0] == '&')
	{
		slog(LG_ERROR, "services(): got parv with local channel: %s", parv[0]);
		return;
	}

	/* make a copy of the original for debugging */
	strlcpy(orig, parv[parc - 1], BUFSIZE);

	/* lets go through this to get the command */
	cmd = strtok(parv[parc - 1], " ");
	text = strtok(NULL, "");

	if (!cmd)
		return;
	if (*cmd == '\001')
	{
		handle_ctcp_common(si, cmd, text);
		return;
	}

	/* take the command through the hash table */
	command_exec_split(si->service, si, cmd, text, &ms_cmdtree);
}

static void memoserv_config_ready(void *unused)
{
	memosvs.nick = memosvs.me->nick;
	memosvs.user = memosvs.me->user;
	memosvs.host = memosvs.me->host;
	memosvs.real = memosvs.me->real;
}

void _modinit(module_t *m)
{
        hook_add_event("config_ready");
        hook_add_config_ready(memoserv_config_ready);
	
	hook_add_event("user_identify");
	hook_add_user_identify(on_user_identify);

	hook_add_event("user_away");
	hook_add_user_away(on_user_away);

	memosvs.me = service_add("memoserv", memoserv, &ms_cmdtree, &ms_conftable);
}

void _moddeinit(void)
{
        if (memosvs.me)
	{
		memosvs.nick = NULL;
		memosvs.user = NULL;
		memosvs.host = NULL;
		memosvs.real = NULL;
                service_delete(memosvs.me);
		memosvs.me = NULL;
	}
	hook_del_config_ready(memoserv_config_ready);
}

static void on_user_identify(user_t *u)
{
	myuser_t *mu = u->myuser;
	
	if (mu->memoct_new > 0)
	{
		notice(memosvs.nick, u->nick, ngettext(N_("You have %d new memo."),
						       N_("You have %d new memos."),
						       mu->memoct_new), mu->memoct_new);
		notice(memosvs.nick, u->nick, _("To read them, type /%s%s READ NEW"),
					ircd->uses_rcommand ? "" : "msg ", memosvs.me->disp);
	}
}

static void on_user_away(user_t *u)
{
	myuser_t *mu;
	mynick_t *mn;

	if (u->flags & UF_AWAY)
		return;
	mu = u->myuser;
	if (mu == NULL)
	{
		mn = mynick_find(u->nick);
		if (mn != NULL && myuser_access_verify(u, mn->owner))
			mu = mn->owner;
	}
	if (mu == NULL)
		return;
	if (mu->memoct_new > 0)
	{
		notice(memosvs.nick, u->nick, ngettext(N_("You have %d new memo."),
						       N_("You have %d new memos."),
						       mu->memoct_new), mu->memoct_new);
		notice(memosvs.nick, u->nick, _("To read them, type /%s%s READ NEW"),
					ircd->uses_rcommand ? "" : "msg ", memosvs.me->disp);
	}
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
