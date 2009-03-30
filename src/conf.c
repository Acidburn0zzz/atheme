/*
 * atheme-services: A collection of minimalist IRC services   
 * conf.c: Services-specific configuration processing.
 *
 * Copyright (c) 2005-2008 Atheme Project (http://www.atheme.org)           
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "atheme.h"
#include "conf.h"
#include "uplink.h"
#include "pmodule.h"
#include "privs.h"
#include "datastream.h"
#include <limits.h>

static int c_uplink(config_entry_t *);
static int c_loadmodule(config_entry_t *);
static int c_operclass(config_entry_t *);
static int c_operator(config_entry_t *);
static int c_language(config_entry_t *);
static int c_string(config_entry_t *);
static int c_logfile(config_entry_t *);

static int c_si_name(config_entry_t *);
static int c_si_numeric(config_entry_t *);
static int c_si_loglevel(config_entry_t *);
static int c_si_auth(config_entry_t *);
static int c_si_casemapping(config_entry_t *);

/* CService client information. */
static int c_ci_vop(config_entry_t *);
static int c_ci_hop(config_entry_t *);
static int c_ci_aop(config_entry_t *);
static int c_ci_sop(config_entry_t *);

static int c_gi_uflags(config_entry_t *);
static int c_gi_cflags(config_entry_t *);
static int c_gi_expire(config_entry_t *);

/* *INDENT-OFF* */

static struct Token uflags[] = {
  { "HOLD",      MU_HOLD         },
  { "NEVEROP",   MU_NEVEROP      },
  { "NOOP",      MU_NOOP         },
  { "HIDEMAIL",  MU_HIDEMAIL     },
  { "NOMEMO",    MU_NOMEMO       },
  { "ENFORCE",   MU_ENFORCE      },
  { "PRIVMSG",   MU_USE_PRIVMSG  },
  { "PRIVATE",   MU_PRIVATE      },
  { "QUIETCHG",  MU_QUIETCHG     },
  { "NONE",      0               },
  { NULL, 0 }
};

static struct Token cflags[] = {
  { "HOLD",        MC_HOLD        },
  { "SECURE",      MC_SECURE      },
  { "VERBOSE",     MC_VERBOSE     },
  { "KEEPTOPIC",   MC_KEEPTOPIC   },
  { "VERBOSE_OPS", MC_VERBOSE_OPS },
  { "TOPICLOCK",   MC_TOPICLOCK   },
  { "GUARD",       MC_GUARD       },
  { "PRIVATE",     MC_PRIVATE     },
  { "LIMITFLAGS",  MC_LIMITFLAGS  },
  { "NONE",        0              },
  { NULL, 0 }
};

static struct Token logflags[] = {
  { "DEBUG",       LG_ALL                                                                                     },
  { "TRACE",       LG_INFO | LG_ERROR | LG_CMD_ALL | LG_NETWORK | LG_WALLOPS | LG_REGISTER                    },
  { "MISC",        LG_INFO | LG_ERROR | LG_CMD_ADMIN | LG_CMD_REGISTER | LG_CMD_SET | LG_NETWORK | LG_WALLOPS | LG_REGISTER },
  { "NOTICE",      LG_INFO | LG_ERROR | LG_CMD_ADMIN | LG_CMD_REGISTER | LG_NETWORK | LG_REGISTER             },
  { "ALL",         LG_ALL                                                                                     },
  { "INFO",        LG_INFO                                                                                    },
  { "ERROR",       LG_ERROR                                                                                   },
  { "COMMANDS",    LG_CMD_ALL                                                                                 },
  { "ADMIN",       LG_CMD_ADMIN                                                                               },
  { "REGISTER",    LG_CMD_REGISTER | LG_REGISTER                                                              },
  { "SET",         LG_CMD_SET                                                                                 },
  { "NETWORK",     LG_NETWORK                                                                                 },
  { "WALLOPS",     LG_WALLOPS                                                                                 },
  { "RAWDATA",     LG_RAWDATA                                                                                 },
  { NULL,          0                                                                                          }
};

list_t conf_si_table;
list_t conf_ci_table;
list_t conf_ni_table;
list_t conf_gi_table;
list_t conf_ms_table;
list_t conf_la_table;

/* *INDENT-ON* */

const char *get_conf_opts(void)
{
	static char opts[53];

	snprintf(opts, sizeof opts, "%s%s%s%s%s%s%s%s%s%s%s",
			match_mapping ? "A" : "",
			auth_module_loaded ? "a" : "",
			log_debug_enabled() ? "d" : "",
			me.auth ? "e" : "",
			config_options.flood_msgs ? "F" : "",
			config_options.leave_chans ? "l" : "", 
			config_options.join_chans ? "j" : "", 
			chansvs.changets ? "t" : "",
			!match_mapping ? "R" : "",
			config_options.raw ? "r" : "",
			runflags & RF_LIVE ? "n" : "");
	return opts;
}

bool conf_parse(const char *file)
{
	config_file_t *cfp;

	cfp = config_load(file);
	if (cfp == NULL)
		return false;

	conf_process(cfp);
	config_free(cfp);

	if (!pmodule_loaded)
	{
		slog(LG_ERROR, "No protocol module loaded, aborting");
		exit(EXIT_FAILURE);
	}

	hook_call_event("config_ready", NULL);
	return true;
}

void conf_init(void)
{
	if (me.netname)
		free(me.netname);
	if (me.hidehostsuffix)
		free(me.hidehostsuffix);
	if (me.adminname)
		free(me.adminname);
	if (me.adminemail)
		free(me.adminemail);
	if (me.mta)
		free(me.mta);
	if (config_options.chan)
		free(config_options.chan);
	if (config_options.global)
		free(config_options.global);
	if (config_options.languagefile)
		free(config_options.languagefile);

	me.netname = me.hidehostsuffix = me.adminname = me.adminemail = me.mta = config_options.chan = 
		config_options.global = config_options.languagefile = NULL;

	me.recontime = me.restarttime = me.maxlogins = me.maxusers = me.maxnicks = me.maxchans = me.emaillimit = me.emailtime = 
		config_options.flood_msgs = config_options.flood_time = config_options.kline_time = config_options.commit_interval = config_options.default_clone_limit = 0;

	config_options.uplink_sendq_limit = 1048576;

	nicksvs.expiry = nicksvs.enforce_expiry = chansvs.expiry = 0;
	nicksvs.enforce_delay = 30;

	config_options.defuflags = config_options.defcflags = 0x00000000;

	config_options.silent = config_options.join_chans = config_options.leave_chans = config_options.raw = false;

	me.auth = AUTH_NONE;

	me.mdlimit = 30;

	chansvs.fantasy = false;
	chansvs.ca_vop = CA_VOP_DEF & ca_all;
	chansvs.ca_hop = CA_HOP_DEF & ca_all;
	chansvs.ca_aop = CA_AOP_DEF & ca_all;
	chansvs.ca_sop = CA_SOP_DEF & ca_all;
	chansvs.changets = false;
	if (chansvs.trigger != NULL)
		free(chansvs.trigger);
	chansvs.trigger = sstrdup("!");
	chansvs.maxchanacs = 0;
	chansvs.maxfounders = 4;
	if (chansvs.deftemplates != NULL)
		free(chansvs.deftemplates);
	chansvs.deftemplates = NULL;

	if (!(runflags & RF_REHASHING))
	{
		if (me.name)
			free(me.name);
		if (me.desc)
			free(me.desc);
		if (me.vhost)
			free(me.vhost);

		me.name = me.desc = me.vhost = NULL;

		set_match_mapping(MATCH_RFC1459);	/* default to RFC compliancy */
	}
}

void init_newconf(void)
{
	/* First we set up the blocks. */
	add_subblock_top_conf("SERVERINFO", &conf_si_table);
	add_subblock_top_conf("CHANSERV", &conf_ci_table);
	add_subblock_top_conf("NICKSERV", &conf_ni_table);
	add_top_conf("UPLINK", c_uplink);
	add_subblock_top_conf("GENERAL", &conf_gi_table);
	add_top_conf("LOADMODULE", c_loadmodule);
	add_top_conf("OPERCLASS", c_operclass);
	add_top_conf("OPERATOR", c_operator);
	add_top_conf("LANGUAGE", c_language);
	add_top_conf("STRING", c_string);
	add_top_conf("LOGFILE", c_logfile);

	/* serverinfo{} block */
	add_conf_item("NAME", &conf_si_table, c_si_name);
	add_dupstr_conf_item("DESC", &conf_si_table, &me.desc);
	add_conf_item("NUMERIC", &conf_si_table, c_si_numeric);
	add_dupstr_conf_item("VHOST", &conf_si_table, &me.vhost);
	add_duration_conf_item("RECONTIME", &conf_si_table, &me.recontime, "s");
	add_duration_conf_item("RESTARTTIME", &conf_si_table, &me.restarttime, "s");
	add_conf_item("EXPIRE", &conf_si_table, c_gi_expire);
	add_dupstr_conf_item("NETNAME", &conf_si_table, &me.netname);
	add_dupstr_conf_item("HIDEHOSTSUFFIX", &conf_si_table, &me.hidehostsuffix);
	add_dupstr_conf_item("ADMINNAME", &conf_si_table, &me.adminname);
	add_dupstr_conf_item("ADMINEMAIL", &conf_si_table, &me.adminemail);
	add_dupstr_conf_item("MTA", &conf_si_table, &me.mta);
	add_conf_item("LOGLEVEL", &conf_si_table, c_si_loglevel);
	add_uint_conf_item("MAXLOGINS", &conf_si_table, &me.maxlogins, 3, INT_MAX);
	add_uint_conf_item("MAXUSERS", &conf_si_table, &me.maxusers, 0, INT_MAX);
	add_uint_conf_item("MAXNICKS", &conf_si_table, &me.maxnicks, 1, INT_MAX);
	add_uint_conf_item("MAXCHANS", &conf_si_table, &me.maxchans, 1, INT_MAX);
	add_uint_conf_item("EMAILLIMIT", &conf_si_table, &me.emaillimit, 1, INT_MAX);
	add_duration_conf_item("EMAILTIME", &conf_si_table, &me.emailtime, "s");
	add_conf_item("AUTH", &conf_si_table, c_si_auth);
	add_uint_conf_item("MDLIMIT", &conf_si_table, &me.mdlimit, 0, INT_MAX);
	add_conf_item("CASEMAPPING", &conf_si_table, c_si_casemapping);

	/* general{} block */
	add_dupstr_conf_item("CHAN", &conf_gi_table, &config_options.chan);
	add_bool_conf_item("VERBOSE_WALLOPS", &conf_gi_table, &config_options.verbose_wallops);
	add_bool_conf_item("SILENT", &conf_gi_table, &config_options.silent);
	add_bool_conf_item("JOIN_CHANS", &conf_gi_table, &config_options.join_chans);
	add_bool_conf_item("LEAVE_CHANS", &conf_gi_table, &config_options.leave_chans);
	add_conf_item("UFLAGS", &conf_gi_table, c_gi_uflags);
	add_conf_item("CFLAGS", &conf_gi_table, c_gi_cflags);
	add_bool_conf_item("RAW", &conf_gi_table, &config_options.raw);
	add_bool_conf_item("SECURE", &conf_gi_table, &config_options.secure);
	add_uint_conf_item("FLOOD_MSGS", &conf_gi_table, &config_options.flood_msgs, 0, INT_MAX);
	add_duration_conf_item("FLOOD_TIME", &conf_gi_table, &config_options.flood_time, "s");
	add_duration_conf_item("KLINE_TIME", &conf_gi_table, &config_options.kline_time, "d");
	add_duration_conf_item("COMMIT_INTERVAL", &conf_gi_table, &config_options.commit_interval, "m");
	add_conf_item("EXPIRE", &conf_gi_table, c_gi_expire);
	add_uint_conf_item("DEFAULT_CLONE_LIMIT", &conf_gi_table, &config_options.default_clone_limit, 1, INT_MAX);
	add_uint_conf_item("UPLINK_SENDQ_LIMIT", &conf_gi_table, &config_options.uplink_sendq_limit, 10240, INT_MAX);

	/* chanserv{} block */
	add_bool_conf_item("FANTASY", &conf_ci_table, &chansvs.fantasy);
	add_conf_item("VOP", &conf_ci_table, c_ci_vop);
	add_conf_item("HOP", &conf_ci_table, c_ci_hop);
	add_conf_item("AOP", &conf_ci_table, c_ci_aop);
	add_conf_item("SOP", &conf_ci_table, c_ci_sop);
	add_bool_conf_item("CHANGETS", &conf_ci_table, &chansvs.changets);
	add_dupstr_conf_item("TRIGGER", &conf_ci_table, &chansvs.trigger);
	add_duration_conf_item("EXPIRE", &conf_ci_table, &chansvs.expiry, "d");
	add_uint_conf_item("MAXCHANACS", &conf_ci_table, &chansvs.maxchanacs, 0, INT_MAX);
	add_uint_conf_item("MAXFOUNDERS", &conf_ci_table, &chansvs.maxfounders, 1, (512 - 60) / (9 + 2)); /* fit on a line */
	add_dupstr_conf_item("DEFTEMPLATES", &conf_ci_table, &chansvs.deftemplates);

	/* nickserv{} block */
	add_bool_conf_item("SPAM", &conf_ni_table, &nicksvs.spam);
	add_bool_conf_item("NO_NICK_OWNERSHIP", &conf_ni_table, &nicksvs.no_nick_ownership);
	add_duration_conf_item("EXPIRE", &conf_ni_table, &nicksvs.expiry, "d");
	add_duration_conf_item("ENFORCE_EXPIRE", &conf_ni_table, &nicksvs.enforce_expiry, "d");
	add_duration_conf_item("ENFORCE_DELAY", &conf_ni_table, &nicksvs.enforce_delay, "s");

	/* language:: stuff */
	add_dupstr_conf_item("NAME", &conf_la_table, &me.language_name);
	add_dupstr_conf_item("TRANSLATOR", &conf_la_table, &me.language_translator);
}

static int c_loadmodule(config_entry_t *ce)
{
	char pathbuf[4096];
	char *name;

	if (cold_start == false)
		return 0;

	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	name = ce->ce_vardata;

	if (*name == '/')
	{
		module_load(name);
		return 0;
	}
	else
	{
		snprintf(pathbuf, 4096, "%s/%s", MODDIR, name);
		module_load(pathbuf);
		return 0;
	}
}

static int c_uplink(config_entry_t *ce)
{
	char *name;
	char *host = NULL, *vhost = NULL, *password = NULL;
	unsigned int port = 0;

	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	if (me.name != NULL && !irccasecmp(ce->ce_vardata, me.name))
		conf_report_warning(ce, "uplink's server name %s should not be the same as our server name, continuing anyway", ce->ce_vardata);
	else if (!strchr(ce->ce_vardata, '.'))
		conf_report_warning(ce, "uplink's server name %s is invalid, continuing anyway", ce->ce_vardata);
	else if (isdigit(ce->ce_vardata[0]))
		conf_report_warning(ce, "uplink's server name %s starts with a digit, probably invalid (continuing anyway)", ce->ce_vardata);

	name = ce->ce_vardata;

	for (ce = ce->ce_entries; ce; ce = ce->ce_next)
	{
		if (!strcasecmp("HOST", ce->ce_varname))
		{
			if (ce->ce_vardata == NULL)
			{
				conf_report_warning(ce, "no parameter for configuration option");
				return 0;
			}

			host = ce->ce_vardata;
		}
		else if (!strcasecmp("VHOST", ce->ce_varname))
		{
			if (ce->ce_vardata == NULL)
			{
				conf_report_warning(ce, "no parameter for configuration option");
				return 0;
			}

			vhost = ce->ce_vardata;
		}
		else if (!strcasecmp("PASSWORD", ce->ce_varname))
		{
			if (ce->ce_vardata == NULL)
			{
				conf_report_warning(ce, "no parameter for configuration option");
				return 0;
			}

			password = ce->ce_vardata;
		}
		else if (!strcasecmp("PORT", ce->ce_varname))
		{
			(void)process_uint_configentry(ce, &port, 1, 65535);
		}
		else
		{
			conf_report_warning(ce, "Invalid configuration option");
			continue;
		}
	}

	uplink_add(name, host, password, vhost, port);
	return 0;
}

static int c_operclass(config_entry_t *ce)
{
	operclass_t *operclass;
	char *name;
	char *privs = NULL, *newprivs;
	int flags = 0;

	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	name = ce->ce_vardata;

	for (ce = ce->ce_entries; ce; ce = ce->ce_next)
	{
		if (!strcasecmp("PRIVS", ce->ce_varname))
		{
			if (ce->ce_vardata == NULL && ce->ce_entries == NULL)
				conf_report_warning(ce, "no parameter for configuration option");

			if (ce->ce_vardata != NULL)
			{
				if (privs == NULL)
					privs = sstrdup(ce->ce_vardata);
				else
				{
					newprivs = smalloc(strlen(privs) + 1 + strlen(ce->ce_vardata) + 1);
					strcpy(newprivs, privs);
					strcat(newprivs, " ");
					strcat(newprivs, ce->ce_vardata);
					free(privs);
					privs = newprivs;
				}
			}
			else
			{
				config_entry_t *conf_p;
				/*
				 * New definition format for operclasses.
				 *
				 * operclass "sra" {
				 *     privs = {
				 *         special:ircop;
				 *     };
				 * };
				 *
				 * - nenolod
				 */
				for (conf_p = ce->ce_entries; conf_p; conf_p = conf_p->ce_next)
				{
					if (privs == NULL)
						privs = sstrdup(conf_p->ce_varname);
					else
					{
						newprivs = smalloc(strlen(privs) + 1 + strlen(conf_p->ce_varname) + 1);
						strcpy(newprivs, privs);
						strcat(newprivs, " ");
						strcat(newprivs, conf_p->ce_varname);
						free(privs);
						privs = newprivs;
					}
				}
			}
		}
		else if (!strcasecmp("NEEDOPER", ce->ce_varname))
			flags |= OPERCLASS_NEEDOPER;
		else
		{
			conf_report_warning(ce, "Invalid configuration option");
			continue;
		}
	}

	operclass = operclass_add(name, privs ? privs : "");
	if (operclass != NULL)
		operclass->flags |= flags;
	free(privs);
	return 0;
}

static int c_operator(config_entry_t *ce)
{
	char *name;
	char *password = NULL;
	operclass_t *operclass = NULL;
	config_entry_t *topce;

	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	topce = ce;
	name = ce->ce_vardata;

	for (ce = ce->ce_entries; ce; ce = ce->ce_next)
	{
		if (!strcasecmp("OPERCLASS", ce->ce_varname))
		{
			if (ce->ce_vardata == NULL)
			{
				conf_report_warning(ce, "no parameter for configuration option");
				return 0;
			}

			operclass = operclass_find(ce->ce_vardata);
			if (operclass == NULL)
				conf_report_warning(ce, "invalid operclass %s for operator %s",
						ce->ce_vardata, name);
		}
		else if (!strcasecmp("PASSWORD", ce->ce_varname))
		{
			if (ce->ce_vardata == NULL)
			{
				conf_report_warning(ce, "no parameter for configuration option");
				return 0;
			}

			password = ce->ce_vardata;
		}
		else
		{
			conf_report_warning(ce, "Invalid configuration option");
			continue;
		}
	}

	if (operclass != NULL)
		soper_add(name, operclass->name, SOPER_CONF, password);
	else
		conf_report_warning(topce, "skipping operator %s because of bad/missing parameters",
						name);
	return 0;
}

/*
 * Ok. This supports:
 *
 * language {
 *         ...
 * };
 *
 * and for the main config:
 *
 * language "translations/blah.language";
 *
 * to set the languagefile setting. So it's rather weird.
 *    --nenolod
 */
static int c_language(config_entry_t *ce)
{
	if (ce->ce_entries)
	{
		subblock_handler(ce, &conf_la_table);
		return 0;
	}
	else
		config_options.languagefile = sstrdup(ce->ce_vardata);

	return 0;
}

static int c_string(config_entry_t *ce)
{
	char *name, *trans = NULL;
	config_entry_t *topce;

	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	topce = ce;
	name = ce->ce_vardata;

	for (ce = ce->ce_entries; ce; ce = ce->ce_next)
	{
		if (!strcasecmp("TRANSLATION", ce->ce_varname))
		{
			if (ce->ce_vardata == NULL)
			{
				conf_report_warning(ce, "no parameter for configuration option");
				return 0;
			}

			trans = ce->ce_vardata;
		}
		else
		{
			conf_report_warning(ce, "Invalid configuration option");
			continue;
		}
	}

	if (trans != NULL)
		translation_create(name, trans);
	else
		conf_report_warning(topce, "missing translation for string");
	return 0;
}

static int c_si_name(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	if (!(runflags & RF_REHASHING))
		me.name = sstrdup(ce->ce_vardata);

	return 0;
}

static int c_si_numeric(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	if (!(runflags & RF_REHASHING))
		me.numeric = sstrdup(ce->ce_vardata);

	return 0;
}

static int c_si_loglevel(config_entry_t *ce)
{
	config_entry_t *flce;
	int val;
	int mask = 0;

	if (ce->ce_vardata != NULL)
	{
		val = token_to_value(logflags, ce->ce_vardata);

		if ((val != TOKEN_UNMATCHED) && (val != TOKEN_ERROR))
			mask |= val;
		else
			conf_report_warning(ce, "unknown flag: %s", ce->ce_vardata);
	}
	for (flce = ce->ce_entries; flce; flce = flce->ce_next)
	{
		val = token_to_value(logflags, flce->ce_varname);

		if ((val != TOKEN_UNMATCHED) && (val != TOKEN_ERROR))
			mask |= val;
		else
			conf_report_warning(flce, "unknown flag: %s", flce->ce_varname);
	}
	log_master_set_mask(mask);

	return 0;
}

static int c_si_auth(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	if (!strcasecmp("EMAIL", ce->ce_vardata))
		me.auth = AUTH_EMAIL;

	else
		me.auth = AUTH_NONE;

	return 0;
}

static int c_si_casemapping(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	if (!strcasecmp("ASCII", ce->ce_vardata))
		set_match_mapping(MATCH_ASCII);

	else
		set_match_mapping(MATCH_RFC1459);

	return 0;
}

static int c_ci_vop(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	chansvs.ca_vop = flags_to_bitmask(ce->ce_vardata, chanacs_flags, 0);

	return 0;
}

static int c_ci_hop(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	chansvs.ca_hop = flags_to_bitmask(ce->ce_vardata, chanacs_flags, 0);

	return 0;
}

static int c_ci_aop(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	chansvs.ca_aop = flags_to_bitmask(ce->ce_vardata, chanacs_flags, 0);

	return 0;
}

static int c_ci_sop(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	chansvs.ca_sop = flags_to_bitmask(ce->ce_vardata, chanacs_flags, 0);

	return 0;
}

static int c_gi_uflags(config_entry_t *ce)
{
	config_entry_t *flce;

	for (flce = ce->ce_entries; flce; flce = flce->ce_next)
	{
		int val;

		val = token_to_value(uflags, flce->ce_varname);

		if ((val != TOKEN_UNMATCHED) && (val != TOKEN_ERROR))
			config_options.defuflags |= val;

		else
			conf_report_warning(flce, "unknown flag: %s", flce->ce_varname);
	}

	return 0;
}

static int c_gi_cflags(config_entry_t *ce)
{
	config_entry_t *flce;

	for (flce = ce->ce_entries; flce; flce = flce->ce_next)
	{
		int val;

		val = token_to_value(cflags, flce->ce_varname);

		if ((val != TOKEN_UNMATCHED) && (val != TOKEN_ERROR))
			config_options.defcflags |= val;

		else
			conf_report_warning(flce, "unknown flag: %s", flce->ce_varname);
	}

	if (config_options.defcflags & MC_TOPICLOCK)
		config_options.defcflags |= MC_KEEPTOPIC;

	return 0;
}

static int c_gi_expire(config_entry_t *ce)
{
	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	conf_report_warning(ce, "general::expire has been deprecated. please use nickserv::expire and chanserv::expire respectively.");

	process_duration_configentry(ce, &nicksvs.expiry, "d");
	process_duration_configentry(ce, &chansvs.expiry, "d");

	return 0;
}

static int c_logfile(config_entry_t *ce)
{
	config_entry_t *flce;
	unsigned int logval = 0;

	if (ce->ce_vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	for (flce = ce->ce_entries; flce; flce = flce->ce_next)
	{
		int val;

		val = token_to_value(logflags, flce->ce_varname);

		if ((val != TOKEN_UNMATCHED) && (val != TOKEN_ERROR))
			logval |= val;
		else
		{
			conf_report_warning(flce, "unknown flag: %s", flce->ce_varname);
		}
	}

	logfile_new(ce->ce_vardata, logval);

	return 0;
}

static void copy_me(struct me *src, struct me *dst)
{
	dst->recontime = src->recontime;
	dst->restarttime = src->restarttime;
	dst->netname = sstrdup(src->netname);
	dst->hidehostsuffix = sstrdup(src->hidehostsuffix);
	dst->adminname = sstrdup(src->adminname);
	dst->adminemail = sstrdup(src->adminemail);
	dst->mta = src->mta ? sstrdup(src->mta) : NULL;
	dst->maxlogins = src->maxlogins;
	dst->maxusers = src->maxusers;
	dst->maxnicks = src->maxnicks;
	dst->maxchans = src->maxchans;
	dst->emaillimit = src->emaillimit;
	dst->emailtime = src->emailtime;
	dst->auth = src->auth;
}

static void free_cstructs(struct me *mesrc)
{
	free(mesrc->netname);
	free(mesrc->hidehostsuffix);
	free(mesrc->adminname);
	free(mesrc->adminemail);
	free(mesrc->mta);
}

bool conf_rehash(void)
{
	struct me *hold_me = scalloc(sizeof(struct me), 1);	/* and keep_me_warm; */
	char *oldsnoop;
	config_file_t *cfp;

	/* we're rehashing */
	slog(LG_INFO, "conf_rehash(): rehashing");
	runflags |= RF_REHASHING;

	cfp = config_load(config_file);
	if (cfp == NULL)
	{
		slog(LG_ERROR, "conf_rehash(): unable to load configuration file, aborting rehash");
		runflags &= ~RF_REHASHING;
		return false;
	}

	copy_me(&me, hold_me);

	oldsnoop = config_options.chan != NULL ? sstrdup(config_options.chan) :
		NULL;

	/* reset everything */
	conf_init();
	mark_all_illegal();
	log_shutdown();

	/* now reload */
	log_open();
	conf_process(cfp);
	config_free(cfp);
	hook_call_event("config_ready", NULL);

	/* now recheck */
	if (!conf_check())
	{
		slog(LG_ERROR, "conf_rehash(): conf file was malformed, aborting rehash");

		/* freeing the new conf strings */
		free_cstructs(&me);

		/* return everything to the way it was before */
		copy_me(hold_me, &me);

		/* not fully ok, oh well */
		unmark_all_illegal();

		free_cstructs(hold_me);
		free(hold_me);
		free(oldsnoop);

		runflags &= ~RF_REHASHING;
		return false;
	}

	if (oldsnoop != NULL || config_options.chan != NULL)
	{
		if (config_options.chan == NULL)
			partall(oldsnoop);
		else if (oldsnoop == NULL)
			joinall(config_options.chan);
		else if (strcmp(oldsnoop, config_options.chan))
		{
			partall(oldsnoop);
			joinall(config_options.chan);
		}
	}

	if (curr_uplink && curr_uplink->conn)
		sendq_set_limit(curr_uplink->conn, config_options.uplink_sendq_limit);

	remove_illegals();

	free_cstructs(hold_me);
	free(hold_me);
	free(oldsnoop);

	runflags &= ~RF_REHASHING;
	return true;
}

bool conf_check(void)
{
	if (!me.name)
	{
		slog(LG_ERROR, "conf_check(): no `name' set in %s", config_file);
		return false;
	}

	/* The following checks could perhaps be stricter */
	if (!strchr(me.name, '.') || strchr("!\"#$%&+,-./:@", me.name[0]) ||
			strchr(me.name, ' '))
	{
		slog(LG_ERROR, "conf_check(): bogus `name' in %s (did you specify a valid server name?)", config_file);
		return false;
	}

	if (isdigit(me.name[0]))
		slog(LG_ERROR, "conf_check(): `name' in %s starts with a digit, probably invalid (continuing anyway)", config_file);

	if (!me.desc)
		me.desc = sstrdup("Atheme IRC Services");

	if ((!me.recontime) || (me.recontime < 10))
	{
		slog(LG_INFO, "conf_check(): invalid `recontime' set in %s; " "defaulting to 10", config_file);
		me.recontime = 10;
	}

	if (!me.netname)
	{
		slog(LG_INFO, "conf_check(): no `netname' set in %s", config_file);
		return false;
	}

	if (!me.adminname)
	{
		slog(LG_INFO, "conf_check(): no `adminname' set in %s", config_file);
		return false;
	}

	if (!me.adminemail)
	{
		slog(LG_INFO, "conf_check(): no `adminemail' set in %s", config_file);
		return false;
	}

	if (!me.mta && me.auth == AUTH_EMAIL)
	{
		slog(LG_INFO, "conf_check(): no `mta' set in %s (but `auth' is email)", config_file);
		return false;
	}

	if (!me.maxlogins)
	{
		slog(LG_INFO, "conf_check(): no `maxlogins' set in %s; " "defaulting to 5", config_file);
		me.maxlogins = 5;
	}

	if (!me.maxnicks)
	{
		if (!nicksvs.no_nick_ownership)
			slog(LG_INFO, "conf_check(): no `maxnicks' set in %s; " "defaulting to 5", config_file);
		me.maxnicks = 5;
	}

	if (!me.maxchans)
	{
		slog(LG_INFO, "conf_check(): no `maxchans' set in %s; " "defaulting to 5", config_file);
		me.maxchans = 5;
	}

	if (!me.emaillimit)
	{
		slog(LG_INFO, "conf_check(): no `emaillimit' set in %s; " "defaulting to 10", config_file);
		me.emaillimit = 10;
	}

	if (!me.emailtime)
	{
		slog(LG_INFO, "conf_check(): no `emailtime' set in %s; " "defaulting to 300", config_file);
		me.emailtime = 300;
	}

	if (me.auth != 0 && me.auth != 1)
	{
		slog(LG_INFO, "conf_check(): no `auth' set in %s; " "defaulting to NONE", config_file);
		me.auth = AUTH_NONE;
	}

	/* we know ca_all now */
	chansvs.ca_vop &= ca_all;
	chansvs.ca_hop &= ca_all;
	chansvs.ca_aop &= ca_all;
	chansvs.ca_sop &= ca_all;
	/* chansvs.ca_hop may be equal to chansvs.ca_vop to disable HOP */
	if (!chansvs.ca_vop || !chansvs.ca_hop || !chansvs.ca_aop ||
			!chansvs.ca_sop ||
			chansvs.ca_vop == chansvs.ca_aop ||
			chansvs.ca_vop == chansvs.ca_sop ||
			chansvs.ca_hop == chansvs.ca_aop ||
			chansvs.ca_hop == chansvs.ca_sop ||
			chansvs.ca_aop == chansvs.ca_sop)
	{
		slog(LG_INFO, "conf_check(): invalid xop levels in %s, using defaults", config_file);
		chansvs.ca_vop = CA_VOP_DEF & ca_all;
		chansvs.ca_hop = CA_HOP_DEF & ca_all;
		chansvs.ca_aop = CA_AOP_DEF & ca_all;
		chansvs.ca_sop = CA_SOP_DEF & ca_all;
	}

	if (config_options.flood_msgs && !config_options.flood_time)
		config_options.flood_time = 10;

	if (!config_options.default_clone_limit)
		config_options.default_clone_limit = 6;

	/* recall that commit_interval is in seconds */
	if ((!config_options.commit_interval) || (config_options.commit_interval < 60) || (config_options.commit_interval > 3600))
	{
		slog(LG_INFO, "conf_check(): invalid `commit_interval' set in %s; " "defaulting to 5 minutes", config_file);
		config_options.commit_interval = 300;
	}

	return true;
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
