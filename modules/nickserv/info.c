/*
 * Copyright (c) 2005-2006 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the NickServ INFO functions.
 *
 * $Id: info.c 7895 2007-03-06 02:40:03Z pippijn $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/info", FALSE, _modinit, _moddeinit,
	"$Id: info.c 7895 2007-03-06 02:40:03Z pippijn $",
	"Atheme Development Group <http://www.atheme.org>"
);

static void ns_cmd_info(sourceinfo_t *si, int parc, char *parv[]);

command_t ns_info = { "INFO", N_("Displays information on registrations."), AC_NONE, 2, ns_cmd_info };

list_t *ns_cmdtree, *ns_helptree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_cmdtree, "nickserv/main", "ns_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");

	command_add(&ns_info, ns_cmdtree);
	help_addentry(ns_helptree, "INFO", "help/nickserv/info", NULL);
}

void _moddeinit()
{
	command_delete(&ns_info, ns_cmdtree);
	help_delentry(ns_helptree, "INFO");
}

static void ns_cmd_info(sourceinfo_t *si, int parc, char *parv[])
{
	myuser_t *mu;
	mynick_t *mn = NULL;
	myuser_name_t *mun;
	user_t *u = NULL;
	char *name = parv[0];
	char buf[BUFSIZE], strfbuf[32], lastlogin[32], *p;
	time_t registered;
	struct tm tm, tm2;
	metadata_t *md;
	node_t *n;
	const char *vhost;
	bool hide_info;
	hook_user_req_t req;

	/* On IRC, default the name to something.
	 * Not currently documented.
	 */
	if (!name && si->su)
	{
		if (!nicksvs.no_nick_ownership)
			name = si->su->nick;
		else if (si->su->myuser)
			name = si->su->myuser->name;
	}

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "INFO");
		command_fail(si, fault_needmoreparams, _("Syntax: INFO <nickname>"));
		return;
	}

	if (!(mu = myuser_find_ext(name)))
	{
		if (has_priv(si, PRIV_USER_AUSPEX) && (mun = myuser_name_find(name)) != NULL)
		{
			char *setter;
			char *reason;
			time_t ts;

			md = metadata_find(mun, METADATA_USER_NAME, "private:mark:setter");
			setter = md != NULL ? md->value : "unknown";
			md = metadata_find(mun, METADATA_USER_NAME, "private:mark:reason");
			reason = md != NULL ? md->value : "unknown";
			md = metadata_find(mun, METADATA_USER_NAME, "private:mark:timestamp");
			ts = md != NULL ? atoi(md->value) : 0;

			tm = *localtime(&ts);
			strftime(strfbuf, sizeof(strfbuf) - 1, "%b %d %H:%M:%S %Y", &tm);

			command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered anymore, but was marked by %s on %s (%s)."), mun->name, setter, strfbuf, reason);
		}
		else
			command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), name);
		return;
	}

	hide_info = use_account_private && mu->flags & MU_PRIVATE &&
		mu != si->smu && !has_priv(si, PRIV_USER_AUSPEX);

	if (!nicksvs.no_nick_ownership)
	{
		mn = mynick_find(*name == '=' ? name + 1 : name);
		if (mn != NULL && mn->owner != mu)
			mn = NULL;
		if (mn == NULL)
			mn = mynick_find(mu->name);
		u = user_find_named(mn->nick);
		if (u != NULL && u->myuser != mu)
			u = NULL;
	}

	if (mn != NULL)
		command_success_nodata(si, _("Information on \2%s\2 (account \2%s\2):"), mn->nick, mu->name);
	else
		command_success_nodata(si, _("Information on \2%s\2:"), mu->name);

	registered = mn != NULL ? mn->registered : mu->registered;
	tm = *localtime(&registered);
	strftime(strfbuf, sizeof(strfbuf) - 1, "%b %d %H:%M:%S %Y", &tm);
	command_success_nodata(si, _("Registered : %s (%s ago)"), strfbuf, time_ago(registered));
	/* show account's time if it's different from nick's time */
	if (mu->registered != registered)
	{
		tm = *localtime(&mu->registered);
		strftime(strfbuf, sizeof(strfbuf) - 1, "%b %d %H:%M:%S %Y", &tm);
		command_success_nodata(si, _("User reg.  : %s (%s ago)"), strfbuf, time_ago(mu->registered));
	}

	if ((md = metadata_find(mu, METADATA_USER, "private:usercloak")))
		vhost = md->value;
	else
		vhost = NULL;
	if (!hide_info && (md = metadata_find(mu, METADATA_USER, "private:host:vhost")))
	{
		strlcpy(buf, md->value, sizeof buf);
		if (vhost != NULL)
		{
			p = strchr(buf, '@');
			if (p == NULL)
				p = buf;
			else
				p++;
			strlcpy(p, vhost, sizeof buf - (p - buf));
		}
		command_success_nodata(si, _("Last addr  : %s"), buf);
	}
	if (vhost && (si->smu == mu || has_priv(si, PRIV_USER_AUSPEX)))
		command_success_nodata(si, _("vHost      : %s"), vhost);
	if (has_priv(si, PRIV_USER_AUSPEX))
	{
		if ((md = metadata_find(mu, METADATA_USER, "private:host:actual")))
			command_success_nodata(si, _("Real addr  : %s"), md->value);
	}

	/* show nick's lastseen/online, if we have a nick */
	if (u != NULL)
		command_success_nodata(si, _("Last seen  : now"));
	else if (mn != NULL)
	{
		tm2 = *localtime(&mn->lastseen);
		strftime(lastlogin, sizeof(lastlogin) -1, "%b %d %H:%M:%S %Y", &tm2);
		if (hide_info)
			command_success_nodata(si, _("Last seen  : (about %d weeks ago)"), (int)((CURRTIME - mn->lastseen) / 604800));
		else
			command_success_nodata(si, _("Last seen  : %s (%s ago)"), lastlogin, time_ago(mn->lastseen));
	}

	/* if noone is logged in to this account, show account's lastseen,
	 * unless we have a nick and it quit at the same time as the account
	 */
	if (LIST_LENGTH(&mu->logins) == 0)
	{
		tm2 = *localtime(&mu->lastlogin);
		strftime(lastlogin, sizeof(lastlogin) -1, "%b %d %H:%M:%S %Y", &tm2);
		if (mn == NULL)
		{
			if (hide_info)
				command_success_nodata(si, _("Last seen  : (about %d weeks ago)"), (int)((CURRTIME - mu->lastlogin) / 604800));
			else
				command_success_nodata(si, _("Last seen  : %s (%s ago)"), lastlogin, time_ago(mu->lastlogin));
		}
		else if (mn->lastseen != mu->lastlogin)
		{
			if (hide_info)
				command_success_nodata(si, _("User seen  : (about %d weeks ago)"), (int)((CURRTIME - mu->lastlogin) / 604800));
			else
				command_success_nodata(si, _("User seen  : %s (%s ago)"), lastlogin, time_ago(mu->lastlogin));
		}
	}
	/* someone is logged in to this account
	 * if they're privileged, show them the sessions
	 */
	else if (mu == si->smu || has_priv(si, PRIV_USER_AUSPEX))
	{
		buf[0] = '\0';
		LIST_FOREACH(n, mu->logins.head)
		{
			if (strlen(buf) > 80)
			{
				command_success_nodata(si, _("Logins from: %s"), buf);
				buf[0] = '\0';
			}
			if (buf[0])
				strcat(buf, " ");
			strcat(buf, ((user_t *)(n->data))->nick);
		}
		if (buf[0])
			command_success_nodata(si, _("Logins from: %s"), buf);
	}
	/* tell them this account is online, but not which nick
	 * unless we have already told them above
	 */
	else if (u == NULL)
	{
		if (mn != NULL)
			command_success_nodata(si, _("User seen  : now"));
		else
			command_success_nodata(si, _("Last seen  : now"));
	}

	if (!nicksvs.no_nick_ownership)
	{
		/* list registered nicks if privileged */
		if (mu == si->smu || has_priv(si, PRIV_USER_AUSPEX))
		{
			buf[0] = '\0';
			LIST_FOREACH(n, mu->nicks.head)
			{
				if (strlen(buf) > 80)
				{
					command_success_nodata(si, _("Nicks      : %s"), buf);
					buf[0] = '\0';
				}
				if (buf[0])
					strcat(buf, " ");
				strcat(buf, ((mynick_t *)(n->data))->nick);
			}
			if (buf[0])
				command_success_nodata(si, _("Nicks      : %s"), buf);
		}
	}

	if (!(mu->flags & MU_HIDEMAIL)
		|| (si->smu == mu || has_priv(si, PRIV_USER_AUSPEX)))
		command_success_nodata(si, _("Email      : %s%s"), mu->email,
					(mu->flags & MU_HIDEMAIL) ? " (hidden)": "");

	*buf = '\0';

	if (MU_HIDEMAIL & mu->flags)
		strcat(buf, "HideMail");

	if (MU_HOLD & mu->flags)
	{
		if (*buf)
			strcat(buf, ", ");

		strcat(buf, "Hold");
	}
	if (MU_NEVEROP & mu->flags)
	{
		if (*buf)
			strcat(buf, ", ");

		strcat(buf, "NeverOp");
	}
	if (MU_NOOP & mu->flags)
	{
		if (*buf)
			strcat(buf, ", ");

		strcat(buf, "NoOp");
	}
	if (MU_NOMEMO & mu->flags)
	{
		if (*buf)
			strcat(buf, ", ");

		strcat(buf, "NoMemo");
	}
	if (MU_EMAILMEMOS & mu->flags)
	{
		if (*buf)
			strcat(buf, ", ");

		strcat(buf, "EMailMemos");
	}
	if (use_account_private && MU_PRIVATE & mu->flags)
	{
		if (*buf)
			strcat(buf, ", ");

		strcat(buf, "Private");
	}

	if (*buf)
		command_success_nodata(si, _("Flags      : %s"), buf);

	if (mu->soper && (mu == si->smu || has_priv(si, PRIV_VIEWPRIVS)))
	{
		command_success_nodata(si, _("Oper class : %s"), mu->soper->operclass ? mu->soper->operclass->name : mu->soper->classname);
	}

	if (has_priv(si, PRIV_USER_AUSPEX) || has_priv(si, PRIV_CHAN_AUSPEX))
	{
		chanacs_t *ca;
		int founder = 0, other = 0;

		LIST_FOREACH(n, mu->chanacs.head)
		{
			ca = n->data;
			if (ca->level & CA_FOUNDER)
				founder++;
			else if (ca->level != CA_AKICK)
				other++;
		}
		command_success_nodata(si, _("Channels   : %d founder, %d other"), founder, other);
	}

	if (has_priv(si, PRIV_USER_AUSPEX) && (md = metadata_find(mu, METADATA_USER, "private:freeze:freezer")))
	{
		char *setter = md->value;
		char *reason;
		time_t ts;

		md = metadata_find(mu, METADATA_USER, "private:freeze:reason");
		reason = md != NULL ? md->value : "unknown";

		md = metadata_find(mu, METADATA_USER, "private:freeze:timestamp");
		ts = md != NULL ? atoi(md->value) : 0;

		tm = *localtime(&ts);
		strftime(strfbuf, sizeof(strfbuf) - 1, "%b %d %H:%M:%S %Y", &tm);

		command_success_nodata(si, _("%s was \2FROZEN\2 by %s on %s (%s)"), mu->name, setter, strfbuf, reason);
	}

	if (has_priv(si, PRIV_USER_AUSPEX) && (md = metadata_find(mu, METADATA_USER, "private:mark:setter")))
	{
		char *setter = md->value;
		char *reason;
		time_t ts;

		md = metadata_find(mu, METADATA_USER, "private:mark:reason");
		reason = md != NULL ? md->value : "unknown";

		md = metadata_find(mu, METADATA_USER, "private:mark:timestamp");
		ts = md != NULL ? atoi(md->value) : 0;

		tm = *localtime(&ts);
		strftime(strfbuf, sizeof(strfbuf) - 1, "%b %d %H:%M:%S %Y", &tm);

		command_success_nodata(si, _("%s was \2MARKED\2 by %s on %s (%s)"), mu->name, setter, strfbuf, reason);
	}

	if (MU_WAITAUTH & mu->flags)
		command_success_nodata(si, _("%s has not completed registration verification"), mu->name);

	req.si = si;
	req.mu = mu;
	req.mn = mn;
	hook_call_event("user_info", &req);

	command_success_nodata(si, _("*** \2End of Info\2 ***"));

	logcommand(si, CMDLOG_GET, "INFO %s", mn != NULL ? mn->nick : mu->name);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
