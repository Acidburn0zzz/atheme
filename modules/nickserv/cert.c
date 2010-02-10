/*
 * Copyright (c) 2006-2010 Atheme Development Group
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Changes and shows nickname CertFP authentication lists.
 *
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"nickserv/cert", false, _modinit, _moddeinit,
	"$Id: cert.c 8239 2010-02-09 04:40:03E jdhore $",
	"Atheme Development Group <http://www.atheme.org>"
);

static void ns_cmd_cert(sourceinfo_t *si, int parc, char *parv[]);

command_t ns_cert = { "CERT", N_("Changes and shows your nickname CertFP authentication list."), AC_NONE, 2, ns_cmd_cert };

list_t *ns_cmdtree, *ns_helptree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_cmdtree, "nickserv/main", "ns_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");

	command_add(&ns_cert, ns_cmdtree);
	help_addentry(ns_helptree, "CERT", "help/nickserv/cert", NULL);

}

void _moddeinit()
{
	command_delete(&ns_cert, ns_cmdtree);
	help_delentry(ns_helptree, "CERT");

}

static void ns_cmd_cert(sourceinfo_t *si, int parc, char *parv[])
{
	myuser_t *mu;
	node_t *n;
	char *mcfp;

	if (parc < 1)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CERT");
		command_fail(si, fault_needmoreparams, _("Syntax: CERT ADD|DEL|LIST [fingerprint]"));
		return;
	}

	if (!strcasecmp(parv[0], "LIST"))
	{
		if (parc < 2)
		{
			mu = si->smu;
			if (mu == NULL)
			{
				command_fail(si, fault_noprivs, _("You are not logged in."));
				return;
			}
		}
		else
		{
			if (!has_priv(si, PRIV_USER_AUSPEX))
			{
				command_fail(si, fault_noprivs, _("You are not authorized to use the target argument."));
				return;
			}

			if (!(mu = myuser_find_ext(parv[1])))
			{
				command_fail(si, fault_badparams, _("\2%s\2 is not registered."), parv[1]);
				return;
			}
		}

		if (mu != si->smu)
			logcommand(si, CMDLOG_ADMIN, "CERT:LIST: \2%s\2", mu->name);
		else
			logcommand(si, CMDLOG_GET, "CERT:LIST");

		command_success_nodata(si, _("Fingerprint list for \2%s\2:"), mu->name);

		LIST_FOREACH(n, mu->cert_fingerprints.head)
		{
			mcfp = ((mycertfp_t*)n->data)->certfp;
			command_success_nodata(si, "- %s", mcfp);
		}

		command_success_nodata(si, _("End of \2%s\2 fingerprint list."), mu->name);
	}
	else if (!strcasecmp(parv[0], "ADD"))
	{
		mu = si->smu;
		if (parc < 2)
		{
			mcfp = si->su->certfp;
			
			if (mcfp == NULL)
			{
				command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CERT ADD");
				command_fail(si, fault_needmoreparams, _("Syntax: CERT ADD <fingerprint>"));
				return;
			}
			else
			{
				if (mycertfp_add(mu, mcfp))
				{
					command_success_nodata(si, _("Added fingerprint \2%s\2 to your fingerprint list."), mcfp);
					logcommand(si, CMDLOG_SET, "CERT:ADD: \2%s\2", mcfp);
				}
				else
					command_fail(si, fault_toomany, _("Your fingerprint list is full."));
			}


		}
		else
		{
			mcfp = parv[1];
		}

		if (mu == NULL)
		{
			command_fail(si, fault_noprivs, _("You are not logged in."));
			return;
		}
		
		if (mycertfp_find(mcfp))
		{
			command_fail(si, fault_nochange, _("Fingerprint \2%s\2 is already on your fingerprint list."), mcfp);
			return;
		}
		if (mycertfp_add(mu, mcfp))
		{
			command_success_nodata(si, _("Added fingerprint \2%s\2 to your fingerprint list."), mcfp);
			logcommand(si, CMDLOG_SET, "CERT:ADD: \2%s\2", mcfp);
		}
		else
			command_fail(si, fault_toomany, _("Your fingerprint list is full."));
	}
	else if (!strcasecmp(parv[0], "DEL"))
	{
		if (parc < 2)
		{
			command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "CERT DEL");
			command_fail(si, fault_needmoreparams, _("Syntax: CERT DEL <fingerprint>"));
			return;
		}
		mu = si->smu;
		if (mu == NULL)
		{
			command_fail(si, fault_noprivs, _("You are not logged in."));
			return;
		}
		if ((mcfp = mycertfp_find(parv[1])) == NULL)
		{
			command_fail(si, fault_nochange, _("Fingerprint \2%s\2 is not on your fingerprint list."), parv[1]);
			return;
		}
		command_success_nodata(si, _("Deleted fingerprint \2%s\2 from your fingerprint list."), parv[1]);
		logcommand(si, CMDLOG_SET, "CERT:DEL: \2%s\2", mcfp);
		mycertfp_delete(mcfp);
	}
	else
	{
		command_fail(si, fault_needmoreparams, STR_INVALID_PARAMS, "CERT");
		command_fail(si, fault_needmoreparams, _("Syntax: CERT ADD|DEL|LIST [fingerprint]"));
		return;
	}
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
