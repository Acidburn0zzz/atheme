/*
 * Copyright (c) 2008 Atheme Development Group
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the CService UP/DOWN functions.
 *
 * $Id: cs_sync.c 8027 2007-04-02 10:47:18Z nenolod $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"chanserv/updown", FALSE, _modinit, _moddeinit,
	"$Id: cs_sync.c 8027 2007-04-02 10:47:18Z nenolod $",
	"Atheme Development Group <http://www.atheme.org>"
);

static void cs_cmd_up(sourceinfo_t *si, int parc, char *parv[]);
static void cs_cmd_down(sourceinfo_t *si, int parc, char *parv[]);

command_t cs_up = { "UP", "Grants all access you have permission to on a given channel.", AC_NONE, 1, cs_cmd_up };
command_t cs_down = { "DOWN", "Removes all current access you posess on a given channel.", AC_NONE, 1, cs_cmd_down };

list_t *cs_cmdtree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(cs_cmdtree, "chanserv/main", "cs_cmdtree");
	command_add(&cs_up, cs_cmdtree);
	command_add(&cs_down, cs_cmdtree);
}

void _moddeinit()
{
	command_delete(&cs_up, cs_cmdtree);
	command_delete(&cs_down, cs_cmdtree);
}


static void cs_cmd_up(sourceinfo_t *si, int parc, char *parv[])
{
	chanuser_t *cu;
	mychan_t *mc;
	node_t *n, *tn;
	char *name = parv[0];
	int fl;
	boolean_t noop;

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "UP");
		command_fail(si, fault_needmoreparams, "Syntax: UP <#channel>");
		return;
	}

	if (!(mc = mychan_find(name)))
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not registered.", name);
		return;
	}
	
	if (metadata_find(mc, METADATA_CHANNEL, "private:close:closer"))
	{
		command_fail(si, fault_noprivs, "\2%s\2 is closed.", name);
		return;
	}

	if (!mc->chan)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 does not exist.", name);
		return;
	}

	if (!si->su)
		return; // needs to be done over IRC

	cu = chanuser_find(mc->chan, si->su);

	if (!cu)
	{
		command_fail(si, fault_nosuch_target, "You are not on \2%s\2.", mc->name);
		return;
	}

	fl = chanacs_user_flags(mc, cu->user);

	// Don't check NOOP, because they are explicitly requesting status
	if (ircd->uses_owner)
	{
		if (fl & CA_USEOWNER)
		{
			if (fl & CA_AUTOOP && !(ircd->owner_mode & cu->modes))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->owner_mchar[1], CLIENT_NAME(cu->user));
				cu->modes |= ircd->owner_mode;
			}
		}
	}

	if (ircd->uses_protect)
	{
		if (fl & CA_USEPROTECT)
		{
			if (fl & CA_AUTOOP && !(ircd->protect_mode & cu->modes))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->protect_mchar[1], CLIENT_NAME(cu->user));
				cu->modes |= ircd->protect_mode;
			}
		}
	}

	if (fl & (CA_AUTOOP | CA_OP))
	{
		if (fl & CA_AUTOOP && !(CMODE_OP & cu->modes))
		{
			modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, 'o', CLIENT_NAME(cu->user));
			cu->modes |= CMODE_OP;
		}
	}

	if (ircd->uses_halfops)
	{
		if (fl & (CA_AUTOHALFOP | CA_HALFOP))
		{
			if (fl & CA_AUTOHALFOP && !(ircd->halfops_mode & cu->modes))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->halfops_mchar[1], CLIENT_NAME(cu->user));
				cu->modes |= ircd->halfops_mode;
			}
		}
	}

	if (fl & (CA_AUTOVOICE | CA_VOICE))
	{
		if (fl & CA_AUTOVOICE && !(CMODE_VOICE & cu->modes))
		{
			modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, 'v', CLIENT_NAME(cu->user));
			cu->modes |= CMODE_VOICE;
		}
	}

	command_success_nodata(si, "Upped successfully on \2%s\2.", mc->name);
}


static void cs_cmd_down(sourceinfo_t *si, int parc, char *parv[])
{
	chanuser_t *cu;
	mychan_t *mc;
	node_t *n, *tn;
	char *name = parv[0];
	int fl;
	boolean_t noop;

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "UP");
		command_fail(si, fault_needmoreparams, "Syntax: UP <#channel>");
		return;
	}

	if (!(mc = mychan_find(name)))
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not registered.", name);
		return;
	}
	
	if (metadata_find(mc, METADATA_CHANNEL, "private:close:closer"))
	{
		command_fail(si, fault_noprivs, "\2%s\2 is closed.", name);
		return;
	}

	if (!mc->chan)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 does not exist.", name);
		return;
	}

	if (!si->su)
		return; // needs to be done over IRC

	cu = chanuser_find(mc->chan, si->su);

	if (!cu)
	{
		command_fail(si, fault_nosuch_target, "You are not on \2%s\2.", mc->name);
		return;
	}

	fl = chanacs_user_flags(mc, cu->user);

	// Don't check NOOP, because they are explicitly requesting status
	if (ircd->uses_owner)
	{
		if (ircd->owner_mode & cu->modes)
		{
			modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, ircd->owner_mchar[1], CLIENT_NAME(cu->user));
			cu->modes &= ~ircd->owner_mode;
		}
	}

	if (ircd->uses_protect)
	{
		if (ircd->protect_mode & cu->modes)
		{
			modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, ircd->protect_mchar[1], CLIENT_NAME(cu->user));
			cu->modes &= ~ircd->protect_mode;
		}
	}

	if ((CMODE_OP & cu->modes))
	{
		modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, 'o', CLIENT_NAME(cu->user));
		cu->modes &= ~CMODE_OP;
	}

	if (ircd->uses_halfops)
	{
		if (ircd->halfops_mode & cu->modes)
		{
			modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, ircd->halfops_mchar[1], CLIENT_NAME(cu->user));
			cu->modes &= ~ircd->halfops_mode;
		}
	}

	if ((CMODE_VOICE & cu->modes))
	{
		modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, 'v', CLIENT_NAME(cu->user));
		cu->modes &= ~CMODE_VOICE;
	}

	command_success_nodata(si, "Downed successfully on \2%s\2.", mc->name);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
