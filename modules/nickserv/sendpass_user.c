/*
 * Copyright (c) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the CService SENDPASS function.
 *
 * $Id: sendpass.c 7985 2007-03-25 20:13:20Z jilles $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/sendpass_user", false, _modinit, _moddeinit,
	"$Id: sendpass.c 7985 2007-03-25 20:13:20Z jilles $",
	"Atheme Development Group <http://www.atheme.org>"
);

static void ns_cmd_sendpass(sourceinfo_t *si, int parc, char *parv[]);

command_t ns_sendpass = { "SENDPASS", N_("Email registration passwords."), AC_NONE, 2, ns_cmd_sendpass };

list_t *ns_cmdtree, *ns_helptree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_cmdtree, "nickserv/main", "ns_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");
	MODULE_TRY_REQUEST_DEPENDENCY(m, "nickserv/setpass");

	command_add(&ns_sendpass, ns_cmdtree);
	help_addentry(ns_helptree, "SENDPASS", "help/nickserv/sendpass_user", NULL);
}

void _moddeinit()
{
	command_delete(&ns_sendpass, ns_cmdtree);
	help_delentry(ns_helptree, "SENDPASS");
}

enum specialoperation
{
	op_none,
	op_force,
	op_clear
};

static void ns_cmd_sendpass(sourceinfo_t *si, int parc, char *parv[])
{
	myuser_t *mu;
	char *name = parv[0];
	char *key;
	enum specialoperation op = op_none;
	bool ismarked = false;

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "SENDPASS");
		command_fail(si, fault_needmoreparams, _("Syntax: SENDPASS <account>"));
		return;
	}
	
	if (parc > 1)
	{
		if (!has_priv(si, PRIV_USER_SENDPASS))
		{
			command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
			return;
		}
		if (!strcasecmp(parv[1], "FORCE"))
			op = op_force;
		else if (!strcasecmp(parv[1], "CLEAR"))
			op = op_clear;
		else
		{
			command_fail(si, fault_badparams, STR_INVALID_PARAMS, "SENDPASS");
			command_fail(si, fault_badparams, _("Syntax: SENDPASS <account> [FORCE|CLEAR]"));
			return;
		}
	}

	if (!(mu = myuser_find(name)))
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), name);
		return;
	}

	if (mu->flags & MU_WAITAUTH)
	{
		command_fail(si, fault_badparams, _("\2%s\2 is not verified."), mu->name);
		return;
	}

	if (metadata_find(mu, "private:mark:setter"))
	{
		ismarked = true;
		/* don't want to disclose this, so just go ahead... */
	}

	if (op == op_clear)
	{
		if (metadata_find(mu, "private:setpass:key"))
		{
			metadata_delete(mu, "private:setpass:key");
			logcommand(si, CMDLOG_ADMIN, "SENDPASS:CLEAR: \2%s\2", mu->name);
			command_success_nodata(si, _("The password change key for \2%s\2 has been cleared."), mu->name);
		}
		else
			command_fail(si, fault_nochange, _("\2%s\2 did not have a password change key outstanding."), mu->name);
		return;
	}

	if (LIST_LENGTH(&mu->logins) > 0)
	{
		command_fail(si, fault_noprivs, _("This operation cannot be performed on %s, because someone is logged in to it."), mu->name);
		return;
	}

	if (metadata_find(mu, "private:freeze:freezer"))
	{
		command_success_nodata(si, _("%s has been frozen by the %s administration."), mu->name, me.netname);
		return;
	}

	if (metadata_find(mu, "private:setpass:key"))
	{
		command_fail(si, fault_alreadyexists, _("\2%s\2 already has a password change key outstanding."), mu->name);
		if (has_priv(si, PRIV_USER_SENDPASS))
			command_fail(si, fault_alreadyexists, _("Use SENDPASS %s CLEAR to clear it so that a new one can be sent."), mu->name);
		return;
	}
	key = gen_pw(12);
	if (sendemail(si->su != NULL ? si->su : si->service->me, EMAIL_SETPASS, mu, key))
	{
		metadata_add(mu, "private:setpass:key", crypt_string(key, gen_salt()));
		logcommand(si, CMDLOG_ADMIN, "SENDPASS: \2%s\2 (change key)", name);
		command_success_nodata(si, _("The password change key for \2%s\2 has been sent to the corresponding email address."), mu->name);
		if (ismarked)
			wallops("%s sent the password for the \2MARKED\2 account %s.", get_oper_name(si), mu->name);
	}
	else
		command_fail(si, fault_emailfail, _("Email send failed."));
	free(key);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
