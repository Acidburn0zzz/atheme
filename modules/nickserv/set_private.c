/*
 * Copyright (c) 2006-2007 William Pitcock, et al.
 * Rights to this code are documented in doc/LICENSE.
 *
 * This file contains routines to handle the NickServ SET PRIVATE command.
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/set_private", FALSE, _modinit, _moddeinit,
	"$Id$",
	"Atheme Development Group <http://www.atheme.org>"
);

list_t *ns_set_cmdtree, *ns_helptree;

/* SET PRIVATE ON|OFF */
static void _ns_set_private(sourceinfo_t *si, int parc, char *parv[])
{
	char *params = strtok(parv[0], " ");

	if (si->smu == NULL)
		return;

	if (params == NULL)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "PRIVATE");
		return;
	}

	if (!strcasecmp("ON", params))
	{
		if (MU_PRIVATE & si->smu->flags)
		{
			command_fail(si, fault_nochange, _("The \2%s\2 flag is already set for \2%s\2."), "PRIVATE", si->smu->name);
			return;
		}

		logcommand(si, CMDLOG_SET, "SET PRIVATE ON");

		si->smu->flags |= MU_PRIVATE;
		si->smu->flags |= MU_HIDEMAIL;

		command_success_nodata(si, _("The \2%s\2 flag has been set for \2%s\2."), "PRIVATE" ,si->smu->name);

		return;
	}
	else if (!strcasecmp("OFF", params))
	{
		if (!(MU_PRIVATE & si->smu->flags))
		{
			command_fail(si, fault_nochange, _("The \2%s\2 flag is not set for \2%s\2."), "PRIVATE", si->smu->name);
			return;
		}

		logcommand(si, CMDLOG_SET, "SET PRIVATE OFF");

		si->smu->flags &= ~MU_PRIVATE;

		command_success_nodata(si, _("The \2%s\2 flag has been removed for \2%s\2."), "PRIVATE", si->smu->name);

		return;
	}
	else
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "PRIVATE");
		return;
	}
}

command_t ns_set_private = { "PRIVATE", N_("Hides information about you from other users."), AC_NONE, 1, _ns_set_private };

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_set_cmdtree, "nickserv/set", "ns_set_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");
	command_add(&ns_set_private, ns_set_cmdtree);

	help_addentry(ns_helptree, "SET PRIVATE", "help/nickserv/set_private", NULL);

	use_private++;
}

void _moddeinit()
{
	command_delete(&ns_set_private, ns_set_cmdtree);
	help_delentry(ns_helptree, "SET PRIVATE");

	use_private--;
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
