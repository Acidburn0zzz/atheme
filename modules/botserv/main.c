/*
 * Copyright (c) 2005 Atheme Development Group
 * Rights to this code are documented in doc/LICENSE.
 *
 * This file contains the main() routine.
 *
 * $Id: main.c 7895 2009-01-24 02:40:03Z celestin $
 */

#include "atheme.h"
#include "botserv.h"

DECLARE_MODULE_V1
(
	"botserv/main", false, _modinit, _moddeinit,
	"$Id: main.c 7895 2009-01-24 02:40:03Z celestin $",
	"Rizon Development Group <http://www.atheme.org>"
);

static void bs_join(hook_channel_joinpart_t *hdata);
static void bs_part(hook_channel_joinpart_t *hdata);
static void bs_register(hook_channel_req_t *mc);
static void bs_newchan(channel_t *c);
static void bs_topiccheck(hook_channel_topic_check_t *data);

static void bs_cmd_bot(sourceinfo_t *si, int parc, char *parv[]);
static void bs_cmd_add(sourceinfo_t *si, int parc, char *parv[]);
static void bs_cmd_change(sourceinfo_t *si, int parc, char *parv[]);
static void bs_cmd_delete(sourceinfo_t *si, int parc, char *parv[]);
static void bs_cmd_assign(sourceinfo_t *si, int parc, char *parv[]);
static void bs_cmd_unassign(sourceinfo_t *si, int parc, char *parv[]);
static void bs_cmd_botlist(sourceinfo_t *si, int parc, char *parv[]);
static void on_shutdown(void *unused);

static void botserv_load_database(void);

/* visible for other modules; use the typedef to enforce type checking */
fn_botserv_bot_find botserv_bot_find;
fn_botserv_save_database botserv_save_database;

service_t *botsvs;
list_t bs_cmdtree;
list_t bs_helptree;
list_t bs_conftable;
list_t *cs_cmdtree;

E list_t mychan;

list_t bs_bots;

command_t bs_bot = { "BOT", "Maintains network bot list.", PRIV_USER_ADMIN, 6, bs_cmd_bot };
command_t bs_assign = { "ASSIGN", "Assigns a bot to a channel.", AC_NONE, 2, bs_cmd_assign };
command_t bs_unassign = { "UNASSIGN", "Unassigns a bot from a channel.", AC_NONE, 1, bs_cmd_unassign };
command_t bs_botlist = { "BOTLIST", "Lists available bots.", AC_NONE, 0, bs_cmd_botlist };

/* ******************************************************************** */

static botserv_bot_t *bs_mychan_find_bot(mychan_t *mc)
{
	metadata_t *md;
	botserv_bot_t *bot;

	md = metadata_find(mc, "private:botserv:bot-assigned");
	bot = md != NULL ? botserv_bot_find(md->value) : NULL;
	if (bot != NULL && !user_find_named(bot->nick))
		bot = NULL;
	if (bot == NULL && md != NULL)
	{
		slog(LG_INFO, "bs_mychan_find_bot(): unassigning invalid bot %s from %s",
				md->value, mc->name);
		if (mc->chan != NULL)
			notice(botsvs->nick, mc->name, "Unassigning invalid bot %s",
					md->value);
		metadata_delete(mc, "private:botserv:bot-assigned");
		metadata_delete(mc, "private:botserv:bot-handle-fantasy");
	}
	return bot;
}

/* ******************************************************************** */

static void (*msg_real)(const char *from, const char *target, const char *fmt, ...);

static void
bs_msg(const char *from, const char *target, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	const char *real_source = from;

	va_start(ap, fmt);
	vasprintf(&buf, fmt, ap);
	va_end(ap);

	if (*target == '#' && !strcmp(from, chansvs.nick))
	{
		mychan_t *mc;
		botserv_bot_t *bot = NULL;

		mc = mychan_find(target);
		if (mc == NULL)
			real_source = from;
		else
		{
			bot = bs_mychan_find_bot(mc);
			real_source = bot->nick;
		}
	}

	msg_real(real_source, target, "%s", buf);
	free(buf);
}

static void (*topic_sts_real)(channel_t *c, user_t *source, const char *setter, time_t ts, time_t prevts, const char *topic);

static void
bs_topic_sts(channel_t *c, user_t *source, const char *setter, time_t ts, time_t prevts, const char *topic)
{
	mychan_t *mc;
	botserv_bot_t *bot = NULL;

	if (source == chansvs.me->me && (mc = mychan_find(c->name)) != NULL)
		bot = bs_mychan_find_bot(mc);

	topic_sts_real(c, bot ? bot->me->me : source, setter, ts, prevts, topic);
}

static void
bs_modestack_mode_simple(const char *source, channel_t *channel, int dir, int flags)
{
	mychan_t *mc;
	metadata_t *bs;
	user_t *bot = NULL;

	if (source != NULL && chansvs.nick != NULL &&
			!strcmp(source, chansvs.nick) &&
			(mc = mychan_find(channel->name)) != NULL &&
			(bs = metadata_find(mc, "private:botserv:bot-assigned")) != NULL)
		bot = user_find_named(bs->value);

	modestack_mode_simple_real(bot ? bot->nick : source, channel, dir, flags);
}

static void
bs_modestack_mode_limit(const char *source, channel_t *channel, int dir, unsigned int limit)
{
	mychan_t *mc;
	metadata_t *bs;
	user_t *bot = NULL;

	if (source != NULL && chansvs.nick != NULL &&
			!strcmp(source, chansvs.nick) &&
			(mc = mychan_find(channel->name)) != NULL &&
			(bs = metadata_find(mc, "private:botserv:bot-assigned")) != NULL)
		bot = user_find_named(bs->value);

	modestack_mode_limit_real(bot ? bot->nick : source, channel, dir, limit);
}

static void
bs_modestack_mode_ext(const char *source, channel_t *channel, int dir, int i, const char *value)
{
	mychan_t *mc;
	metadata_t *bs;
	user_t *bot = NULL;

	if (source != NULL && chansvs.nick != NULL &&
			!strcmp(source, chansvs.nick) &&
			(mc = mychan_find(channel->name)) != NULL &&
			(bs = metadata_find(mc, "private:botserv:bot-assigned")) != NULL)
		bot = user_find_named(bs->value);

	modestack_mode_ext_real(bot ? bot->nick : source, channel, dir, i, value);
}

static void
bs_modestack_mode_param(const char *source, channel_t *channel, int dir, char type, const char *value)
{
	mychan_t *mc;
	metadata_t *bs;
	user_t *bot = NULL;

	if (source != NULL && chansvs.nick != NULL &&
			!strcmp(source, chansvs.nick) &&
			(mc = mychan_find(channel->name)) != NULL &&
			(bs = metadata_find(mc, "private:botserv:bot-assigned")) != NULL)
		bot = user_find_named(bs->value);

	modestack_mode_param_real(bot ? bot->nick : source, channel, dir, type, value);
}

static void
bs_try_kick(user_t *source, channel_t *chan, user_t *target, const char *reason)
{
	mychan_t *mc;
	metadata_t *bs;
	user_t *bot = NULL;

	if (source != chansvs.me->me)
		return try_kick_real(source, chan, target, reason);

	if ((mc = mychan_find(chan->name)) != NULL && (bs = metadata_find(mc, "private:botserv:bot-assigned")) != NULL)
		bot = user_find_named(bs->value);

	try_kick_real(bot ? bot : source, chan, target, reason);
}

/* ******************************************************************** */

static void
bs_join_registered(bool all)
{
	mychan_t *mc;
	mowgli_patricia_iteration_state_t state;
	chanuser_t *cu;
	metadata_t *md;
	int cs = 0;

	if ((chansvs.me != NULL) && (chansvs.me->me != NULL))
		cs = 1;

	MOWGLI_PATRICIA_FOREACH(mc, &state, mclist)
	{
		if ((md = metadata_find(mc, "private:botserv:bot-assigned")) == NULL)
			continue;

		if (all)
		{
			join(mc->name, md->value);
			if (mc->chan && cs && (cu = chanuser_find(mc->chan, chansvs.me->me)))
				part(mc->name, chansvs.nick);
			continue;
		}
		else if (mc->chan != NULL && mc->chan->members.count != 0)
		{
			join(mc->name, md->value);
			if (mc->chan && cs && (cu = chanuser_find(mc->chan, chansvs.me->me)))
				part(mc->name, chansvs.nick);
			continue;
		}
	}
}

/* ******************************************************************** */

static void bs_channel_drop(mychan_t *mc)
{
	botserv_bot_t *bot;

	if ((bot = bs_mychan_find_bot(mc)) == NULL)
		return;	

	metadata_delete(mc, "private:botserv:bot-assigned");
	metadata_delete(mc, "private:botserv:bot-handle-fantasy");
	part(mc->name, bot->nick);
}

/* ******************************************************************** */

/* botserv: command handler */
static void botserv(sourceinfo_t *si, int parc, char *parv[])
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
	command_exec_split(si->service, si, cmd, text, &bs_cmdtree);
}

/* botserv: bot handler: channel commands only. */
static void
botserv_channel_handler(sourceinfo_t *si, int parc, char *parv[])
{
	metadata_t *md;
	mychan_t *mc = NULL;
	char orig[BUFSIZE];
	char newargs[BUFSIZE];
	char *cmd;
	char *args;

	/* this should never happen */
	if (parv[parc - 2][0] == '&')
	{
		slog(LG_ERROR, "services(): got parv with local channel: %s", parv[0]);
		return;
	}

	/* is this a fantasy command? respect global fantasy settings. */
	if (chansvs.fantasy == false)
	{
		/* *all* fantasy disabled */
		return;
	}

	mc = mychan_find(parv[parc - 2]);
	if (!mc)
	{
		/* unregistered, NFI how we got this message, but let's leave it alone! */
		slog(LG_DEBUG, "botserv_channel_handler(): received message for %s (unregistered channel?)", parv[parc - 2]);
		return;
	}

	md = metadata_find(mc, "disable_fantasy");
	if (md)
	{
		/* fantasy disabled on this channel. don't message them, just bail. */
		return;
	}

	md = metadata_find(mc, "private:botserv:bot-assigned");
	if (md == NULL)
	{
		/* we received this, but have no record of a bot assigned. WTF */
		slog(LG_DEBUG, "botserv_channel_handler(): received a message for a bot, but %s has no bots assigned.", mc->name);
		return;
	}

	md = metadata_find(mc, "private:botserv:bot-handle-fantasy");
	if (md == NULL || irccasecmp(si->service->me->nick, md->value))
		return;

	/* make a copy of the original for debugging */
	strlcpy(orig, parv[parc - 1], BUFSIZE);

	/* lets go through this to get the command */
	cmd = strtok(parv[parc - 1], " ");

	if (!cmd)
		return;
	if (*cmd == '\001')
	{
		handle_ctcp_common(si, cmd, strtok(NULL, ""));
		return;
	}

	/* take the command through the hash table, handling both !prefix and Bot, ... styles */
	if (strlen(cmd) > 2 && (strchr(chansvs.trigger, *cmd) != NULL && isalpha(*++cmd)))
	{
		/* XXX not really nice to look up the command twice
		* -- jilles */
		if (command_find(cs_cmdtree, cmd) == NULL)
			return;
		if (floodcheck(si->su, si->service->me))
			return;
		/* construct <channel> <args> */
		strlcpy(newargs, parv[parc - 2], sizeof newargs);
		args = strtok(NULL, "");
		if (args != NULL)
		{
			strlcat(newargs, " ", sizeof newargs);
			strlcat(newargs, args, sizeof newargs);
		}
		/* let the command know it's called as fantasy cmd */
		si->c = mc->chan;
		/* fantasy commands are always verbose
		* (a little ugly but this way we can !set verbose)
		*/
		mc->flags |= MC_FORCEVERBOSE;
		command_exec_split(si->service, si, cmd, newargs, cs_cmdtree);
		mc->flags &= ~MC_FORCEVERBOSE;
	}
	else if (!strncasecmp(cmd, si->service->me->nick, strlen(si->service->me->nick)) && (cmd = strtok(NULL, "")) != NULL)
	{
		char *pptr;

		strlcpy(newargs, parv[parc - 2], sizeof newargs);
		if ((pptr = strchr(cmd, ' ')) != NULL)
		{
			*pptr = '\0';

			strlcat(newargs, " ", sizeof newargs);
			strlcat(newargs, ++pptr, sizeof newargs);
		}

		if (command_find(cs_cmdtree, cmd) == NULL)
			return;
		if (floodcheck(si->su, si->service->me))
			return;

		si->c = mc->chan;

		/* fantasy commands are always verbose
		* (a little ugly but this way we can !set verbose)
		*/
		mc->flags |= MC_FORCEVERBOSE;
		command_exec_split(si->service, si, cmd, newargs, cs_cmdtree);
		mc->flags &= ~MC_FORCEVERBOSE;
	}
}

/* ******************************************************************** */

static void botserv_config_ready(void *unused)
{
	botserv_load_database();
	if (me.connected)
		bs_join_registered(!config_options.leave_chans);

	hook_del_config_ready(botserv_config_ready);
}

/* ******************************************************************** */

/* TODO: merge this into backend/flatfile. --nenolod */
void botserv_save_database(void *unused)
{
	FILE *f;
	node_t *n;
	int errno1;

	if (!(f = fopen(DATADIR "/botserv.db.new", "w")))
	{
		errno1 = errno;
		slog(LG_INFO, "botserv_save_database(): cannot create botserv.db.new: %s", strerror(errno1));
		wallops(_("\2DATABASE ERROR\2: botserv_save_database(): cannot create botserv.db.new: %s"), strerror(errno1));
		return;
	}

	/* write database version */
	fprintf(f, "DBV 1\n");

	/* iterate through and write all the metadata */
	LIST_FOREACH(n, bs_bots.head)
	{
		botserv_bot_t *bot = (botserv_bot_t *) n->data;

		fprintf(f, "BOT %s %s %s %d %ld %s\n", bot->nick, bot->user, bot->host, bot->private, (long)bot->registered, bot->real);
	}

	fprintf(f, "COUNT %d\n", bs_bots.count);

	fclose(f);

	/* use an atomic rename */
	if ((rename(DATADIR "/botserv.db.new", DATADIR "/botserv.db")) < 0)
	{
		errno1 = errno;
		slog(LG_INFO, "botserv_save_database(): cannot rename botserv.db.new to botserv.db: %s", strerror(errno1));
		wallops(_("\2DATABASE ERROR\2: botserv_save_database(): cannot rename botserv.db.new to botserv.db: %s"), strerror(errno1));
		return;
	}
}

/* ******************************************************************** */

static void botserv_load_database(void)
{
	FILE *f;
	char *item, dBuf[BUFSIZE];
	unsigned int linecnt;
	int i;

	f = fopen(DATADIR "/botserv.db", "r");
	if (f == NULL)
	{
		if (errno == ENOENT)
		{
			slog(LG_ERROR, "botserv_load_database(): %s does not exist, creating it", DATADIR "/botserv.db");
			return;
		}
		else
		{
			slog(LG_ERROR, "botserv_load_database(): can't open %s for reading: %s", DATADIR "/botserv.db", strerror(errno));
			slog(LG_ERROR, "botserv_load_database(): exiting to avoid data loss");
			exit(1);
		}
	}

	slog(LG_DEBUG, "botserv_load_database(): ----------------------- loading ------------------------");

	while (fgets(dBuf, BUFSIZE, f))
	{
		linecnt++;

		/* check for unimportant lines */
		item = strtok(dBuf, " ");
		strip(item);

		if (*item == '#' || *item == '\n' || *item == '\t' || *item == ' ' || *item == '\0' || *item == '\r' || !*item)
			continue;

		/* database version */
		if (!strcmp("DBV", item))
		{
			i = atoi(strtok(NULL, " "));
			if (i > 1)
			{
				slog(LG_ERROR, "botserv_load_database(): database version is %d; I only understand v1.", i);
				exit(EXIT_FAILURE);
			}
		}

		/* count */
		else if (!strcmp("COUNT", item))
		{
			i = atoi(strtok(NULL, " "));
			if ((unsigned int)i != LIST_LENGTH(&bs_bots))
				slog(LG_ERROR, "botserv_load_database(): inconsistency: database defines %d objects, I only deserialized %d.", i, bs_bots.count);
		}

		/* a botserv bot */
		else if (!strcmp("BOT", item))
		{
			char *nick, *user, *host, *prv, *reg, *real;
			botserv_bot_t *bot;

			nick = strtok(NULL, " ");
			user = strtok(NULL, " ");
			host = strtok(NULL, " ");
			prv = strtok(NULL, " ");
			reg = strtok(NULL, " ");
			real = strtok(NULL, "\n");

			if (nick == NULL || user == NULL || host == NULL || prv == NULL || reg == NULL || real == NULL)
			{
				slog(LG_ERROR, "botserv_load_database(): missing metadata for bot, skipping");
				continue;
			}

			strip(real);
			bot = scalloc(sizeof(botserv_bot_t), 1);
			bot->nick = sstrdup(nick);
			bot->user = sstrdup(user);
			bot->host = sstrdup(host);
			bot->real = sstrdup(real);
			bot->private = atoi(prv);
			bot->registered = atoi(reg);
			bot->me = service_add_static(bot->nick, bot->user, bot->host, bot->real, botserv_channel_handler, cs_cmdtree);
			service_set_chanmsg(bot->me, true);

			node_add(bot, &bot->bnode, &bs_bots);
		}
	}

	fclose(f);
}

/* ******************************************************************** */

botserv_bot_t* botserv_bot_find(char *name)
{
	node_t *n;

	if (name == NULL)
		return NULL;

	LIST_FOREACH(n, bs_bots.head)
	{
		botserv_bot_t *bot = (botserv_bot_t *) n->data;

		if (!irccasecmp(name, bot->nick))
			return bot;
	}

	return NULL;
}

/* ******************************************************************** */

/* BOT CMD nick user host real */
static void bs_cmd_bot(sourceinfo_t *si, int parc, char *parv[])
{
	if (parc < 1 || parv[0] == NULL)
	{
		command_fail(si, fault_needmoreparams, STR_INVALID_PARAMS, "BOT");
		command_fail(si, fault_needmoreparams, _("Syntax: BOT ADD <nick> <user> <host> <real>"));
		command_fail(si, fault_needmoreparams, _("Syntax: BOT CHANGE <oldnick> <newnick> [<user> [<host> [<real>]]]"));
		command_fail(si, fault_needmoreparams, _("Syntax: BOT DEL <nick>"));
		return;
	}

	if (!irccasecmp(parv[0], "ADD"))
	{
		bs_cmd_add(si, parc - 1, parv + 1);
	}
	else if(!irccasecmp(parv[0], "CHANGE"))
	{
		bs_cmd_change(si, parc - 1, parv + 1);
	}
	else if(!irccasecmp(parv[0], "DEL"))
	{
		bs_cmd_delete(si, parc - 1, parv + 1);
	}
	else
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "BOT");
		command_fail(si, fault_badparams, _("Syntax: BOT ADD <nick> <user> <host> <real>"));
		command_fail(si, fault_badparams, _("Syntax: BOT CHANGE <oldnick> <newnick> [<user> [<host> [<real>]]]"));
		command_fail(si, fault_badparams, _("Syntax: BOT DEL <nick>"));
	}
}

/* ******************************************************************** */

static bool valid_misc_field(const char *field, size_t maxlen)
{
	if (strlen(field) > maxlen)
		return false;

	/* Never ever allow @!?* as they have special meaning in all ircds */
	/* Empty, space anywhere and colon at the start break the protocol */
	/* Also disallow ASCII 1-31 */
	if (strchr(field, '@') || strchr(field, '!') || strchr(field, '?') ||
			strchr(field, '*') || strchr(field, ' ') ||
			*field == ':' || *field == '\0' ||
			has_ctrl_chars(field))
		return false;

	return true;
}

/* CHANGE oldnick nick [user [host [real]]] */
static void bs_cmd_change(sourceinfo_t *si, int parc, char *parv[])
{
	botserv_bot_t *bot;
	mowgli_patricia_iteration_state_t state;
	mychan_t *mc;
	metadata_t *md;

	if (parc < 2 || parv[0] == NULL || parv[1] == NULL)
	{
		command_fail(si, fault_needmoreparams, STR_INVALID_PARAMS, "BOT CHANGE");
		command_fail(si, fault_needmoreparams, _("Syntax: BOT CHANGE <oldnick> <newnick> [<user> [<host> [<real>]]]"));
		return;
	}

	bot = botserv_bot_find(parv[0]);
	if (bot == NULL)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not a bot", parv[0]);
		return;
	}

	if (nicksvs.no_nick_ownership ? myuser_find(parv[1]) != NULL : mynick_find(parv[1]) != NULL)
	{
		command_fail(si, fault_alreadyexists, _("\2%s\2 is a registered nick."), parv[1]);
		return;
	}

	if (irccasecmp(parv[0], parv[1]))
	{
		if (botserv_bot_find(parv[1]) || service_find_nick(parv[1]))
		{
			command_fail(si, fault_alreadyexists,
					_("\2%s\2 is already a bot or service."),
					parv[1]);
			return;
		}
	}

	if (parc >= 2 && (!valid_misc_field(parv[1], NICKLEN - 1) ||
				strchr(parv[1], '.') || isdigit(*parv[1])))
	{
		command_fail(si, fault_badparams, _("\2%s\2 is an invalid nickname."), parv[1]);
		return;
	}

	if (parc >= 4 && !check_vhost_validity(si, parv[3]))
		return;

	service_delete(bot->me);
	switch(parc)
	{
		case 5:
			if (strlen(parv[4]) < GECOSLEN)
			{
				free(bot->real);
				bot->real = sstrdup(parv[4]);
			}
			else
				command_fail(si, fault_badparams, _("\2%s\2 is an invalid realname, not changing it"), parv[4]);
		case 4:
			free(bot->host);
			bot->host = sstrdup(parv[3]);
		case 3:
			/* XXX: we really need an is_valid_user(), but this is close enough. --nenolod */
			if (valid_misc_field(parv[2], USERLEN - 1)) {
				free(bot->user);
				bot->user = sstrdup(parv[2]);
			} else
				command_fail(si, fault_badparams, _("\2%s\2 is an invalid username, not changing it."), parv[2]);
		case 2:
			free(bot->nick);
			bot->nick = sstrdup(parv[1]);
			break;
		default:
			command_fail(si, fault_needmoreparams, STR_INVALID_PARAMS, "BOT CHANGE");
			command_fail(si, fault_needmoreparams, _("Syntax: BOT CHANGE <oldnick> <newnick> [<user> [<host> [<real>]]]"));
			return;
	}
	bot->registered = CURRTIME;
	bot->me = service_add_static(bot->nick, bot->user, bot->host, bot->real, botserv_channel_handler, cs_cmdtree);
	service_set_chanmsg(bot->me, true);

	/* join it back and also update the metadata */
	MOWGLI_PATRICIA_FOREACH(mc, &state, mclist)
	{
		if ((md = metadata_find(mc, "private:botserv:bot-assigned")) == NULL)
			continue;

		if (!irccasecmp(md->value, parv[0]))
		{
			metadata_add(mc, "private:botserv:bot-assigned", parv[1]);
			metadata_add(mc, "private:botserv:bot-handle-fantasy", parv[1]);
			if (!config_options.leave_chans || (mc->chan != NULL && LIST_LENGTH(&mc->chan->members) > 0))
				join(mc->name, parv[1]);
		}
	}

	botserv_save_database(NULL);
	command_success_nodata(si, "\2%s\2 (\2%s\2@\2%s\2) [\2%s\2] changed.", bot->nick, bot->user, bot->host, bot->real);
}

/* ******************************************************************** */

/* ADD nick user host real */
static void bs_cmd_add(sourceinfo_t *si, int parc, char *parv[])
{
	botserv_bot_t *bot;
	char buf[BUFSIZE];

	if (parc < 4)
	{
		command_fail(si, fault_needmoreparams, STR_INVALID_PARAMS, "BOT ADD");
		command_fail(si, fault_needmoreparams, _("Syntax: BOT ADD <nick> <user> <host> <real>"));
		return;
	}

	if (botserv_bot_find(parv[0]) || service_find(parv[0]) || service_find_nick(parv[0]))
	{
		command_fail(si, fault_alreadyexists,
				_("\2%s\2 is already a bot or service."),
				parv[0]);
		return;
	}

	if (nicksvs.no_nick_ownership ? myuser_find(parv[0]) != NULL : mynick_find(parv[0]) != NULL)
	{
		command_fail(si, fault_alreadyexists, _("\2%s\2 is a registered nick."), parv[0]);
		return;
	}

	if (parc > 4 && parv[4] != NULL)
		snprintf(buf, sizeof(buf), "%s %s", parv[3], parv[4]);
	else
		snprintf(buf, sizeof(buf), "%s", parv[3]);

	if (!valid_misc_field(parv[0], NICKLEN - 1) || strchr(parv[0], '.') ||
			isdigit(*parv[0]))
	{
		command_fail(si, fault_badparams, _("\2%s\2 is an invalid nickname."), parv[0]);
		return;
	}

	if (!valid_misc_field(parv[1], USERLEN - 1))
	{
		command_fail(si, fault_badparams, _("\2%s\2 is an invalid username."), parv[1]);
		return;
	}

	if (!check_vhost_validity(si, parv[2]))
		return;

	if (strlen(parv[3]) >= GECOSLEN)
	{
		command_fail(si, fault_badparams, _("\2%s\2 is an invalid realname."), parv[3]);
		return;
	}

	bot = scalloc(sizeof(botserv_bot_t), 1);
	bot->nick = sstrdup(parv[0]);
	bot->user = sstrdup(parv[1]);
	bot->host = sstrdup(parv[2]);
	bot->real = sstrdup(buf);
	bot->private = false;
	bot->registered = CURRTIME;
	bot->me = service_add_static(bot->nick, bot->user, bot->host, bot->real, botserv_channel_handler, cs_cmdtree);
	service_set_chanmsg(bot->me, true);
	node_add(bot, &bot->bnode, &bs_bots);

	botserv_save_database(NULL);
	command_success_nodata(si, "\2%s\2 (\2%s\2@\2%s\2) [\2%s\2] created.", bot->nick, bot->user, bot->host, bot->real);
}

/* ******************************************************************** */

/* DELETE nick */
static void bs_cmd_delete(sourceinfo_t *si, int parc, char *parv[])
{
	botserv_bot_t *bot = botserv_bot_find(parv[0]);
	mowgli_patricia_iteration_state_t state;
	mychan_t *mc;
	metadata_t *md;

	if (parc < 1)
	{
		command_fail(si, fault_needmoreparams, STR_INVALID_PARAMS, "BOT DEL");
		command_fail(si, fault_needmoreparams, _("Syntax: BOT DEL <nick>"));
		return;
	}

	if (bot == NULL)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not a bot", parv[0]);
		return;
	}

	MOWGLI_PATRICIA_FOREACH(mc, &state, mclist)
	{
		if ((md = metadata_find(mc, "private:botserv:bot-assigned")) == NULL)
			continue;

		if (!irccasecmp(md->value, bot->nick))
		{
			if (mc->flags & MC_GUARD &&
					(!config_options.leave_chans ||
					 (mc->chan != NULL &&
					  LIST_LENGTH(&mc->chan->members) > 1)))
				join(mc->name, chansvs.nick);

			metadata_delete(mc, "private:botserv:bot-assigned");
			metadata_delete(mc, "private:botserv:bot-handle-fantasy");
		}
	}

	node_del(&bot->bnode, &bs_bots);
	service_delete(bot->me);
	free(bot->nick);
	free(bot->user);
	free(bot->real);
	free(bot->host);
	free(bot);

	botserv_save_database(NULL);
	command_success_nodata(si, "Bot \2%s\2 has been deleted.", parv[0]);
}

/* ******************************************************************** */

/* LIST */
static void bs_cmd_botlist(sourceinfo_t *si, int parc, char *parv[])
{
	int i = 0;
	node_t *n;

	command_success_nodata(si, "Listing of bots available on \2%s\2:", me.netname);

	LIST_FOREACH(n, bs_bots.head)
	{
		botserv_bot_t *bot = (botserv_bot_t *) n->data;

		if (!bot->private)
			command_success_nodata(si, "\2%d:\2 %s (%s@%s) [%s]", ++i, bot->nick, bot->user, bot->host, bot->real);
	}

	command_success_nodata(si, "\2%d\2 bots available.", i);
	if (si->su != NULL && has_priv(si, PRIV_CHAN_ADMIN))
	{
		i = 0;
		command_success_nodata(si, "Listing of private bots available on \2%s\2:", me.netname);
		LIST_FOREACH(n, bs_bots.head)
		{
			botserv_bot_t *bot = (botserv_bot_t *) n->data;

			if (bot->private)
				command_success_nodata(si, "\2%d:\2 %s (%s@%s) [%s]", ++i, bot->nick, bot->user, bot->host, bot->real);
		}
		command_success_nodata(si, "\2%d\2 private bots available.", i);
	}
	command_success_nodata(si, "Use \2/msg %s ASSIGN #chan botnick\2 to assign one to your channel.", si->service->me->nick);
}

/* ******************************************************************** */

/* ASSIGN #channel nick */
static void bs_cmd_assign(sourceinfo_t *si, int parc, char *parv[])
{
	mychan_t *mc = mychan_find(parv[0]);
	metadata_t *md;
	botserv_bot_t *bot;

	if (!parv[0] || !parv[1])
	{
		command_fail(si, fault_needmoreparams, STR_INVALID_PARAMS, "ASSIGN");
		command_fail(si, fault_needmoreparams, _("Syntax: ASSIGN <#channel> <nick>"));
		return;
	}

	if (mc == NULL)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not registered.", parv[0]);
		return;
	}

	if (metadata_find(mc, "private:botserv:no-bot") != NULL)
	{
		command_fail(si, fault_noprivs, "You cannot assign bots to \2%s\2.", mc->name);
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_SET))
	{
		command_fail(si, fault_noprivs, "You are not authorised to assign bots on \2%s\2.", mc->name);
		return;
	}

	md = metadata_find(mc, "private:botserv:bot-assigned");

	bot = botserv_bot_find(parv[1]);
	if (bot == NULL)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not a bot", parv[1]);
		return;
	}

	if (bot->private && !has_priv(si, PRIV_CHAN_ADMIN))
	{
		command_fail(si, fault_noprivs, "You are not authorised to assign the bot \2%s\2 to a channel.", bot->nick);
		return;
	}

	if (md != NULL && !irccasecmp(md->value, parv[1]))
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is already assigned to \2%s\2.", bot->nick, parv[0]);
		return;
	}

	if (md == NULL || irccasecmp(md->value, parv[1]))
	{
		join(mc->name, parv[1]);
		if (md != NULL)
			part(mc->name, md->value);
	}

	part(mc->name, chansvs.nick);
	metadata_add(mc, "private:botserv:bot-assigned", parv[1]);
	metadata_add(mc, "private:botserv:bot-handle-fantasy", parv[1]);

	command_success_nodata(si, "Assigned the bot \2%s\2 to \2%s\2.", parv[1], parv[0]);
}

/* ******************************************************************** */

/* UNASSIGN #channel */
static void bs_cmd_unassign(sourceinfo_t *si, int parc, char *parv[])
{
	mychan_t *mc = mychan_find(parv[0]);
	metadata_t *md;

	if (!parv[0])
	{
		command_fail(si, fault_needmoreparams, STR_INVALID_PARAMS, "UNASSIGN");
		command_fail(si, fault_needmoreparams, _("Syntax: UNASSIGN <#channel>"));
		return;
	}

	if (mc == NULL)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not registered.", parv[0]);
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_SET))
	{
		command_fail(si, fault_noprivs, "You are not authorised to unassign a bot on \2%s\2.", mc->name);
		return;
	}

	if ((md = metadata_find(mc, "private:botserv:bot-assigned")) == NULL)
	{
		command_fail(si, fault_nosuch_key, _("\2%s\2 does not have a bot assigned."), mc->name);
		return;
	}

	if (mc->flags & MC_GUARD && (!config_options.leave_chans ||
				(mc->chan != NULL &&
				 LIST_LENGTH(&mc->chan->members) > 1)))
		join(mc->name, chansvs.nick);
	part(mc->name, md->value);
	metadata_delete(mc, "private:botserv:bot-assigned");
	metadata_delete(mc, "private:botserv:bot-handle-fantasy");
	command_success_nodata(si, "Unassigned the bot from \2%s\2.", parv[0]);
}

/* ******************************************************************** */

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(cs_cmdtree, "chanserv/main", "cs_cmdtree");
	hook_add_event("config_ready");
	hook_add_config_ready(botserv_config_ready);

	hook_add_event("channel_drop");
	hook_add_channel_drop(bs_channel_drop);

	hook_add_event("shutdown");
	hook_add_shutdown(on_shutdown);

	botsvs = service_add("botserv", botserv, &bs_cmdtree, &bs_conftable);

	command_add(&bs_bot, &bs_cmdtree);
	command_add(&bs_assign, &bs_cmdtree);
	command_add(&bs_unassign, &bs_cmdtree);
	command_add(&bs_botlist, &bs_cmdtree);
	help_addentry(&bs_helptree, "BOT", "help/botserv/bot", NULL);
	help_addentry(&bs_helptree, "ASSIGN", "help/botserv/assign", NULL);
	help_addentry(&bs_helptree, "UNASSIGN", "help/botserv/unassign", NULL);
	help_addentry(&bs_helptree, "BOTLIST", "help/botserv/botlist", NULL);
	hook_add_event("channel_join");
	hook_add_event("channel_part");
	hook_add_event("channel_register");
	hook_add_event("channel_add");
	hook_add_event("channel_can_change_topic");
	hook_add_channel_join(bs_join);
	hook_add_channel_part(bs_part);
	hook_add_channel_register(bs_register);
	hook_add_channel_add(bs_newchan);
	hook_add_channel_can_change_topic(bs_topiccheck);

	modestack_mode_simple = bs_modestack_mode_simple;
	modestack_mode_limit  = bs_modestack_mode_limit;
	modestack_mode_ext    = bs_modestack_mode_ext;
	modestack_mode_param  = bs_modestack_mode_param;
	try_kick              = bs_try_kick;
	topic_sts_real        = topic_sts;
	topic_sts             = bs_topic_sts;
	msg_real              = msg;
	msg                   = bs_msg;
}

void _moddeinit(void)
{
	node_t *n, *tn;

	if (botsvs)
	{
		service_delete(botsvs);
		botsvs = NULL;
	}
	LIST_FOREACH_SAFE(n, tn, bs_bots.head)
	{
		botserv_bot_t *bot = (botserv_bot_t *) n->data;

		node_del(&bot->bnode, &bs_bots);
		service_delete(bot->me);
		free(bot->nick);
		free(bot->user);
		free(bot->real);
		free(bot->host);
		free(bot);
	}
	command_delete(&bs_bot, &bs_cmdtree);
	command_delete(&bs_assign, &bs_cmdtree);
	command_delete(&bs_unassign, &bs_cmdtree);
	command_delete(&bs_botlist, &bs_cmdtree);
	help_delentry(&bs_helptree, "BOT");
	help_delentry(&bs_helptree, "ASSIGN");
	help_delentry(&bs_helptree, "UNASSIGN");
	help_delentry(&bs_helptree, "BOTLIST");
	hook_del_channel_join(bs_join);
	hook_del_channel_part(bs_part);
	hook_del_channel_register(bs_register);
	hook_del_channel_add(bs_newchan);
	hook_del_channel_can_change_topic(bs_topiccheck);
	hook_del_channel_drop(bs_channel_drop);
	hook_del_shutdown(on_shutdown);
	hook_del_config_ready(botserv_config_ready);

	modestack_mode_simple = modestack_mode_simple_real;
	modestack_mode_limit  = modestack_mode_limit_real;
	modestack_mode_ext    = modestack_mode_ext_real;
	modestack_mode_param  = modestack_mode_param_real;
	try_kick              = try_kick_real;
	topic_sts             = topic_sts_real;
	msg                   = msg_real;
}

/* ******************************************************************** */

static void on_shutdown(void *unused)
{
	node_t *n;

	LIST_FOREACH(n, bs_bots.head)
	{
		botserv_bot_t *bot = (botserv_bot_t *) n->data;
		quit_sts(bot->me->me, "shutting down");
	}
}

static void bs_join(hook_channel_joinpart_t *hdata)
{
	chanuser_t *cu = hdata->cu;
	user_t *u;
	channel_t *chan;
	mychan_t *mc;
	unsigned int flags;
	bool noop;
	bool secure;
	metadata_t *md;
	chanacs_t *ca2;
	char akickreason[120] = "User is banned from this channel", *p;
	botserv_bot_t *bot;

	if (cu == NULL || is_internal_client(cu->user))
		return;
	u = cu->user;
	chan = cu->chan;

	/* first check if this is a registered channel at all */
	mc = mychan_find(chan->name);
	if (mc == NULL)
		return;

	/* chanserv's function handles those */
	if (metadata_find(mc, "private:botserv:bot-assigned") == NULL)
		return;

	bot = bs_mychan_find_bot(mc);
	if (bot == NULL)
	{
		if (chan->nummembers == 1 && mc->flags & MC_GUARD)
			join(chan->name, chansvs.nick);
	}
	else
	{
		if (chan->nummembers == 1)
			join(chan->name, bot->nick);
	}

	flags = chanacs_user_flags(mc, u);
	noop = mc->flags & MC_NOOP || (u->myuser != NULL &&
	u->myuser->flags & MU_NOOP);
	/* attempt to deop people recreating channels, if the more
	* sophisticated mechanism is disabled */
	secure = mc->flags & MC_SECURE || (!chansvs.changets &&
	chan->nummembers == 1 && chan->ts > CURRTIME - 300);

	/* auto stuff */
	if ((mc->flags & MC_RESTRICTED) && !(flags & CA_ALLPRIVS) && !has_priv_user(u, PRIV_JOIN_STAFFONLY))
	{
		/* Stay on channel if this would empty it -- jilles */
		if (chan->nummembers <= 2)
		{
			mc->flags |= MC_INHABIT;
			if (!(mc->flags & MC_GUARD))
			{
				if (bot)
					join(chan->name, bot->nick);
				else
					join(chan->name, chansvs.nick);
			}
		}
		if (mc->mlock_on & CMODE_INVITE || chan->modes & CMODE_INVITE)
		{
			if (!(chan->modes & CMODE_INVITE))
				check_modes(mc, true);
			if (bot)
				remove_banlike(bot->me->me, chan, ircd->invex_mchar, u);
			else
				remove_banlike(chansvs.me->me, chan, ircd->invex_mchar, u);
			modestack_flush_channel(chan);
		}
		else
		{
			if (bot)
			{
				ban(bot->me->me, chan, u);
				remove_ban_exceptions(bot->me->me, chan, u);
			}
			else
			{
				ban(chansvs.me->me, chan, u);
				remove_ban_exceptions(chansvs.me->me, chan, u);
			}
		}
		if (bot)
		{
			try_kick(bot->me->me, chan, u, "You are not authorized to be on this channel");
		}
		else
		{
			try_kick(chansvs.me->me, chan, u, "You are not authorized to be on this channel");
		}
		hdata->cu = NULL;
		return;
	}

	if (flags & CA_AKICK && !(flags & CA_REMOVE))
	{
		/* Stay on channel if this would empty it -- jilles */
		if (chan->nummembers <= 2)
		{
			mc->flags |= MC_INHABIT;
			if (!(mc->flags & MC_GUARD))
			{
				if (bot)
					join(chan->name, bot->nick);
				else
					join(chan->name, chansvs.nick);
			}
		}
		/* use a user-given ban mask if possible -- jilles */
		ca2 = chanacs_find_host_by_user(mc, u, CA_AKICK);
		if (ca2 != NULL)
		{
			if (chanban_find(chan, ca2->host, 'b') == NULL)
			{
				char str[512];

				chanban_add(chan, ca2->host, 'b');
				snprintf(str, sizeof str, "+b %s", ca2->host);
				/* ban immediately */
				if (bot)
					mode_sts(bot->nick, chan, str);
				else
					mode_sts(chansvs.nick, chan, str);
			}
		}
		else
		{
			/* XXX this could be done more efficiently */
			ca2 = chanacs_find(mc, u->myuser, CA_AKICK);
			if (bot)
				ban(bot->me->me, chan, u);
			else
				ban(chansvs.me->me, chan, u);
		}
		if (bot)
			remove_ban_exceptions(bot->me->me, chan, u);
		else
			remove_ban_exceptions(chansvs.me->me, chan, u);
		if (ca2 != NULL)
		{
			md = metadata_find(ca2, "reason");
			if (md != NULL && *md->value != '|')
			{
				snprintf(akickreason, sizeof akickreason,
				"Banned: %s", md->value);
				p = strchr(akickreason, '|');
				if (p != NULL)
				*p = '\0';
				else
				p = akickreason + strlen(akickreason);
				/* strip trailing spaces, so as not to
				* disclose the existence of an oper reason */
				p--;
				while (p > akickreason && *p == ' ')
				p--;
				p[1] = '\0';
			}
		}
		if (bot)
			try_kick(bot->me->me, chan, u, akickreason);
		else
			try_kick(chansvs.me->me, chan, u, akickreason);
		hdata->cu = NULL;
		return;
	}

	/* Kick out users who may be recreating channels mlocked +i.
	* Users with +i flag are allowed to join, as are users matching
	* an invite exception (the latter only works if the channel already
	* exists because members are sent before invite exceptions).
	* Because most networks do not use kick_on_split_riding or
	* no_create_on_split, do not trust users coming back from splits.
	* Unfortunately, this kicks users who have been invited by a channel
	* operator, after a split or services restart.
	*/
	if (mc->mlock_on & CMODE_INVITE && !(flags & CA_INVITE) &&
	(!(u->server->flags & SF_EOB) || (chan->nummembers <= 2 && (chan->nummembers <= 1 || chanuser_find(chan, chansvs.me->me) || (bot && chanuser_find(chan, bot->me->me))))) &&
	(!ircd->invex_mchar || !next_matching_ban(chan, u, ircd->invex_mchar, chan->bans.head)))
	{
		if (chan->nummembers <= 2)
		{
			mc->flags |= MC_INHABIT;
			if (!(mc->flags & MC_GUARD))
			{
				if (bot)
					join(chan->name, bot->nick);
				else
					join(chan->name, chansvs.nick);
			}
		}
		if (!(chan->modes & CMODE_INVITE))
		check_modes(mc, true);
		modestack_flush_channel(chan);
		if (bot)
			try_kick(bot->me->me, chan, u, "Invite only channel");
		else
			try_kick(chansvs.me->me, chan, u, "Invite only channel");
		hdata->cu = NULL;
		return;
	}

	/* A user joined and was not kicked; we do not need
	* to stay on the channel artificially. */
	if (mc->flags & MC_INHABIT)
	{
		mc->flags &= ~MC_INHABIT;
		if (!(mc->flags & MC_GUARD) && (!config_options.chan || irccasecmp(chan->name, config_options.chan)) && chanuser_find(chan, chansvs.me->me))
			part(chan->name, chansvs.nick);
	}

	if (ircd->uses_owner)
	{
		if (flags & CA_USEOWNER)
		{
			if (flags & CA_AUTOOP && !(noop || cu->modes & ircd->owner_mode))
			{
				if (bot)
					modestack_mode_param(bot->nick, chan, MTYPE_ADD, ircd->owner_mchar[1], CLIENT_NAME(u));
				else
					modestack_mode_param(chansvs.nick, chan, MTYPE_ADD, ircd->owner_mchar[1], CLIENT_NAME(u));
				cu->modes |= ircd->owner_mode;
			}
		}
		else if (secure && (cu->modes & ircd->owner_mode))
		{
			if (bot)
				modestack_mode_param(bot->nick, chan, MTYPE_DEL, ircd->owner_mchar[1], CLIENT_NAME(u));
			else
				modestack_mode_param(chansvs.nick, chan, MTYPE_DEL, ircd->owner_mchar[1], CLIENT_NAME(u));
			cu->modes &= ~ircd->owner_mode;
		}
	}

	/* XXX still uses should_protect() */
	if (ircd->uses_protect)
	{
		if (flags & CA_USEPROTECT)
		{
			if (flags & CA_AUTOOP && !(noop || cu->modes & ircd->protect_mode))
			{
				if (bot)
					modestack_mode_param(bot->nick, chan, MTYPE_ADD, ircd->protect_mchar[1], CLIENT_NAME(u));
				else
					modestack_mode_param(chansvs.nick, chan, MTYPE_ADD, ircd->protect_mchar[1], CLIENT_NAME(u));
				cu->modes |= ircd->protect_mode;
			}
		}
		else if (secure && (cu->modes & ircd->protect_mode))
		{
			if (bot)
				modestack_mode_param(bot->nick, chan, MTYPE_DEL, ircd->protect_mchar[1], CLIENT_NAME(u));
			else
				modestack_mode_param(chansvs.nick, chan, MTYPE_DEL, ircd->protect_mchar[1], CLIENT_NAME(u));
			cu->modes &= ~ircd->protect_mode;
		}
	}

	if (flags & CA_AUTOOP)
	{
		if (!(noop || cu->modes & CSTATUS_OP))
		{
			if (bot)
				modestack_mode_param(bot->nick, chan, MTYPE_ADD, 'o', CLIENT_NAME(u));
			else
				modestack_mode_param(chansvs.nick, chan, MTYPE_ADD, 'o', CLIENT_NAME(u));
			cu->modes |= CSTATUS_OP;
		}
	}
	else if (secure && (cu->modes & CSTATUS_OP) && !(flags & CA_OP))
	{
		if (bot)
			modestack_mode_param(bot->nick, chan, MTYPE_DEL, 'o', CLIENT_NAME(u));
		else
			modestack_mode_param(chansvs.nick, chan, MTYPE_DEL, 'o', CLIENT_NAME(u));
		cu->modes &= ~CSTATUS_OP;
	}

	if (ircd->uses_halfops)
	{
		if (flags & CA_AUTOHALFOP)
		{
			if (!(noop || cu->modes & (CSTATUS_OP | ircd->halfops_mode)))
			{
				if (bot)
					modestack_mode_param(bot->nick, chan, MTYPE_ADD, 'h', CLIENT_NAME(u));
				else
					modestack_mode_param(chansvs.nick, chan, MTYPE_ADD, 'h', CLIENT_NAME(u));
				cu->modes |= ircd->halfops_mode;
			}
		}
		else if (secure && (cu->modes & ircd->halfops_mode) && !(flags & CA_HALFOP))
		{
			if (bot)
				modestack_mode_param(bot->nick, chan, MTYPE_DEL, 'h', CLIENT_NAME(u));
			else
				modestack_mode_param(chansvs.nick, chan, MTYPE_DEL, 'h', CLIENT_NAME(u));
		cu->modes &= ~ircd->halfops_mode;
		}
	}

	if (flags & CA_AUTOVOICE)
	{
		if (!(noop || cu->modes & (CSTATUS_OP | ircd->halfops_mode | CSTATUS_VOICE)))
		{
			if (bot)
				modestack_mode_param(bot->nick, chan, MTYPE_ADD, 'v', CLIENT_NAME(u));
			else
				modestack_mode_param(chansvs.nick, chan, MTYPE_ADD, 'v', CLIENT_NAME(u));
			cu->modes |= CSTATUS_VOICE;
		}
	}

	if (u->server->flags & SF_EOB && (md = metadata_find(mc, "private:entrymsg")))
	{
		if (bot)
			notice(bot->nick, cu->user->nick, "[%s] %s", mc->name, md->value);
	}

	if (flags & CA_USEDUPDATE)
		mc->used = CURRTIME;
}

/* ******************************************************************** */

static void
bs_part(hook_channel_joinpart_t *hdata)
{
	chanuser_t *cu;
	mychan_t *mc;
	botserv_bot_t *bot;

	cu = hdata->cu;
	if (cu == NULL)
		return;

	mc = mychan_find(cu->chan->name);
	if (mc == NULL)
		return;

	/* chanserv's function handles those */
	if (metadata_find(mc, "private:botserv:bot-assigned") == NULL)
		return;

	bot = bs_mychan_find_bot(mc);
	if (CURRTIME - mc->used >= 3600)
		if (chanacs_user_flags(mc, cu->user) & CA_USEDUPDATE)
			mc->used = CURRTIME;
	/*
	* When channel_part is fired, we haven't yet removed the
	* user from the room. So, the channel will have two members
	* if ChanServ is joining channels: the triggering user and
	* itself.
	*
	* Do not part if we're enforcing an akick/close in an otherwise
	* empty channel (MC_INHABIT). -- jilles
	*/
	if ((mc->flags & MC_GUARD)
			&& config_options.leave_chans
			&& !(mc->flags & MC_INHABIT)
			&& (cu->chan->nummembers <= 2)
			&& !is_internal_client(cu->user))
	{
		if (bot)
			part(cu->chan->name, bot->nick);
		else
			part(cu->chan->name, chansvs.nick);
	}
}

static void bs_register(hook_channel_req_t *hdata)
{
	mychan_t *mc;
	metadata_t *bs;
	botserv_bot_t *bot;

	mc = hdata->mc;
	if ((bs = metadata_find(mc, "private:botserv:bot-assigned")) == NULL)
		bot = NULL;
	else
		bot = botserv_bot_find(bs->value);
	if (mc->chan)
	{
		if (mc->flags & MC_GUARD)
		{
			if (bot)
				join(mc->name, bot->nick);
			else
				join(mc->name, chansvs.nick);
		}

		check_modes(mc, true);
	}
}

/* Called when a topic change is received from the network, before altering
 * our internal structures */
static void bs_topiccheck(hook_channel_topic_check_t *data)
{
	mychan_t *mc;
	unsigned int accessfl = 0;
	metadata_t *bs;
	botserv_bot_t *bot;

	mc = mychan_find(data->c->name);
	if (mc == NULL)
		return;

	if ((bs = metadata_find(mc, "private:botserv:bot-assigned")) == NULL)
		bot = NULL;
	else
		bot = botserv_bot_find(bs->value);
	if ((mc->flags & (MC_KEEPTOPIC | MC_TOPICLOCK)) == (MC_KEEPTOPIC | MC_TOPICLOCK))
	{
		if (data->u == NULL || !((accessfl = chanacs_user_flags(mc, data->u)) & CA_TOPIC))
		{
			/* topic burst or unauthorized user, revert it */
			data->approved = 1;
			slog(LG_DEBUG, "cs_topiccheck(): reverting topic change on channel %s by %s",
					data->c->name,
					data->u != NULL ? data->u->nick : "<server>");

			if (data->u != NULL && !(mc->mlock_off & CMODE_TOPIC))
			{
				/* they do not have access to be opped either,
				 * deop them and set +t */
				/* note: channel_mode() takes nicks, not UIDs
				 * when used with a non-NULL source */
				if (ircd->uses_halfops && !(accessfl & (CA_OP | CA_AUTOOP | CA_HALFOP | CA_AUTOHALFOP)))
					channel_mode_va((bot)?bot->me->me:chansvs.me->me, data->c,
							3, "+t-oh", data->u->nick, data->u->nick);
				else if (!ircd->uses_halfops && !(accessfl & (CA_OP | CA_AUTOOP)))
					channel_mode_va((bot)?bot->me->me:chansvs.me->me, data->c,
							2, "+t-o", data->u->nick);
			}
		}
	}
}

/* Called on creation of a channel */
static void bs_newchan(channel_t *c)
{
	mychan_t *mc;
	chanuser_t *cu;
	metadata_t *md, *bs;
	char *setter;
	char *text;
	time_t channelts = 0;
	time_t topicts;
	char str[21];
	botserv_bot_t *bot;

	if (!(mc = mychan_find(c->name)))
		return;

	if ((bs = metadata_find(mc, "private:botserv:bot-assigned")) == NULL)
		bot = NULL;
	else
		bot = botserv_bot_find(bs->value);

	/* schedule a mode lock check when we know the current modes
	 * -- jilles */
	mc->flags |= MC_MLOCK_CHECK;

	md = metadata_find(mc, "private:channelts");
	if (md != NULL)
		channelts = atol(md->value);
	if (channelts == 0)
		channelts = mc->registered;

	if (c->ts > channelts && channelts > 0)
		mc->flags |= MC_RECREATED;
	else
		mc->flags &= ~MC_RECREATED;

	if (chansvs.changets && c->ts > channelts && channelts > 0)
	{
		/* Stop the splitrider -- jilles */
		c->ts = channelts;
		clear_simple_modes(c);
		c->modes |= CMODE_NOEXT | CMODE_TOPIC;
		check_modes(mc, false);
		/* No ops to clear */
		chan_lowerts(c, (bot)?bot->me->me:chansvs.me->me);
		cu = chanuser_add(c, CLIENT_NAME((bot)?bot->me->me:chansvs.me->me));
		cu->modes |= CSTATUS_OP;
		/* make sure it parts again sometime (empty SJOIN etc) */
		mc->flags |= MC_INHABIT;
	}
	else if (c->ts != channelts)
	{
		snprintf(str, sizeof str, "%lu", (unsigned long)c->ts);
		metadata_add(mc, "private:channelts", str);
	}
	else if (!(MC_TOPICLOCK & mc->flags) && LIST_LENGTH(&c->members) == 0)
		/* Same channel, let's assume ircd has kept topic.
		 * However, if topiclock is enabled, we must change it back
		 * regardless.
		 * Also, if there is someone in this channel already, it is
		 * being created by a service and we must restore.
		 * -- jilles */
		return;

	if (!(MC_KEEPTOPIC & mc->flags))
		return;

	md = metadata_find(mc, "private:topic:setter");
	if (md == NULL)
		return;
	setter = md->value;

	md = metadata_find(mc, "private:topic:text");
	if (md == NULL)
		return;
	text = md->value;

	md = metadata_find(mc, "private:topic:ts");
	if (md == NULL)
		return;
	topicts = atol(md->value);

	handle_topic(c, setter, topicts, text);
	topic_sts(c, bot ? bot->me->me : chansvs.me->me, setter, topicts, 0, text);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
