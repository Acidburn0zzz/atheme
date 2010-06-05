/*
 * Copyright (c) 2005 William Pitcock, et al.
 * Rights to this code are documented in doc/LICENSE.
 *
 * This file contains routines to handle the NickServ HELP command.
 *
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/help", false, _modinit, _moddeinit,
	PACKAGE_STRING,
	"Atheme Development Group <http://www.atheme.org>"
);

list_t *ns_cmdtree, *ns_helptree;

static void ns_cmd_help(sourceinfo_t *si, int parc, char *parv[]);

command_t ns_help = { "HELP", N_("Displays contextual help information."), AC_NONE, 1, ns_cmd_help };

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_cmdtree, "nickserv/main", "ns_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");

	command_add(&ns_help, ns_cmdtree);
	help_addentry(ns_helptree, "HELP", "help/help", NULL);
}

void _moddeinit()
{
	command_delete(&ns_help, ns_cmdtree);
	help_delentry(ns_helptree, "HELP");
}

/* HELP <command> [params] */
void ns_cmd_help(sourceinfo_t *si, int parc, char *parv[])
{
	char *command = parv[0];

	if (!command)
	{
		command_success_nodata(si, _("***** \2%s Help\2 *****"), nicksvs.nick);
		if (nicksvs.no_nick_ownership)
		{
			command_success_nodata(si, _("\2%s\2 allows users to \2'register'\2 an account for use with \2%s\2."), nicksvs.nick, chansvs.nick);
			if (nicksvs.expiry > 0)
			{
				command_success_nodata(si, _("If a registered account is not used by the owner for %d days,\n"
							"\2%s\2 will drop the account, allowing it to be reregistered."),
						(nicksvs.expiry / 86400), nicksvs.nick);
			}
		}
		else
		{
			command_success_nodata(si, _("\2%s\2 allows users to \2'register'\2 a nickname, and stop\n"
						"others from using that nick. \2%s\2 allows the owner of a\n"
						"nickname to disconnect a user from the network that is using\n"
						"their nickname."), nicksvs.nick, nicksvs.nick);
			if (nicksvs.expiry > 0)
			{
				command_success_nodata(si, _("If a registered nick is not used by the owner for %d days,\n"
							"\2%s\2 will drop the nickname, allowing it to be reregistered."),
						(nicksvs.expiry / 86400), nicksvs.nick);
			}
		}
		command_success_nodata(si, " ");
		command_success_nodata(si, _("For more information on a command, type:"));
		command_success_nodata(si, "\2/%s%s help <command>\2", (ircd->uses_rcommand == false) ? "msg " : "", nicksvs.me->disp);
		command_success_nodata(si, _("For a verbose listing of all commands, type:"));
		command_success_nodata(si, "\2/%s%s help commands\2", (ircd->uses_rcommand == false) ? "msg " : "", nicksvs.me->disp);
		command_success_nodata(si, " ");

		command_help_short(si, ns_cmdtree, "REGISTER IDENTIFY GHOST RELEASE INFO LISTCHANS SET GROUP UNGROUP FDROP FUNGROUP MARK FREEZE SENDPASS VHOST");

		command_success_nodata(si, _("***** \2End of Help\2 *****"));
		return;
	}

	if (!strcasecmp("COMMANDS", command))
	{
		command_success_nodata(si, _("***** \2%s Help\2 *****"), nicksvs.nick);
		command_help(si, ns_cmdtree);
		command_success_nodata(si, _("***** \2End of Help\2 *****"));
		return;
	}

	/* take the command through the hash table */
	help_display(si, command, ns_helptree);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
