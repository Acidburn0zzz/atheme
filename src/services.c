/*
 * atheme-services: A collection of minimalist IRC services
 * services.c: Routines commonly used by various services.
 *
 * Copyright (c) 2005-2007 Atheme Project (http://www.atheme.org)
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
#include "pmodule.h"

extern mowgli_patricia_t *services;
int authservice_loaded = 0;
int use_myuser_access = 0;
int use_svsignore = 0;
int use_privmsg = 0;
int use_account_private = 0;
int use_channel_private = 0;

#define MAX_BUF 256

/* ban wrapper for cmode, returns number of bans added (0 or 1) */
int ban(user_t *sender, channel_t *c, user_t *user)
{
	char mask[MAX_BUF];
	char modemask[MAX_BUF];
	chanban_t *cb;

	if (!c)
		return 0;

	snprintf(mask, MAX_BUF, "*!*@%s", user->vhost);
	mask[MAX_BUF - 1] = '\0';

	snprintf(modemask, MAX_BUF, "+b %s", mask);
	modemask[MAX_BUF - 1] = '\0';

	cb = chanban_find(c, mask, 'b');

	if (cb != NULL)
		return 0;

	chanban_add(c, mask, 'b');

	mode_sts(sender->nick, c, modemask);
	return 1;
}

/* returns number of modes removed -- jilles */
int remove_banlike(user_t *source, channel_t *chan, int type, user_t *target)
{
	char change[MAX_BUF];
	int count = 0;
	node_t *n, *tn;
	chanban_t *cb;

	if (type == 0)
		return 0;
	if (source == NULL || chan == NULL || target == NULL)
		return 0;

	for (n = next_matching_ban(chan, target, type, chan->bans.head); n != NULL; n = next_matching_ban(chan, target, type, tn))
	{
		tn = n->next;
		cb = n->data;

		snprintf(change, sizeof change, "-%c %s", cb->type, cb->mask);
		mode_sts(source->nick, chan, change);
		chanban_delete(cb);
		count++;
	}
	return count;
}

/* returns number of exceptions removed -- jilles */
int remove_ban_exceptions(user_t *source, channel_t *chan, user_t *target)
{
	return remove_banlike(source, chan, ircd->except_mchar, target);
}

void try_kick(user_t *source, channel_t *chan, user_t *target, const char *reason)
{
	return_if_fail(source != NULL);
	return_if_fail(chan != NULL);
	return_if_fail(target != NULL);
	return_if_fail(reason != NULL);

	if (target->flags & UF_IMMUNE)
	{
		wallops("Not kicking immune user %s!%s@%s from %s (%s: %s)",
				target->nick, target->user, target->vhost,
				chan->name, source ? source->nick : me.name,
				reason);
		notice(source->nick, chan->name,
				"Not kicking immune user %s (%s)",
				target->nick, reason);
		return;
	}
	kick(source->nick, chan->name, target->nick, reason);
}

/* sends a KILL message for a user and removes the user from the userlist
 * source should be a service user or NULL for a server kill
 */
void kill_user(user_t *source, user_t *victim, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZE];

	return_if_fail(victim != NULL);

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZE, fmt, ap);
	va_end(ap);

	if (victim->flags & UF_ENFORCER)
		quit_sts(victim, buf);
	else if (victim->server == me.me)
	{
		slog(LG_INFO, "kill_user(): not killing service %s (source %s, comment %s)",
				victim->nick,
				source != NULL ? source->nick : me.name,
				buf);
		return;
	}
	else
		kill_id_sts(source, CLIENT_NAME(victim), buf);
	user_delete(victim);
}

void introduce_enforcer(const char *nick)
{
	user_t *u;

	/* TS 1 to win nick collisions */
	u = user_add(nick, "enforcer", me.name, NULL, NULL,
			ircd->uses_uid ? uid_get() : NULL,
			"Held for nickname owner", me.me, 1);
	return_if_fail(u != NULL);
	u->flags |= UF_INVIS | UF_ENFORCER;
	introduce_nick(u);
}

/* join a channel, creating it if necessary */
void join(char *chan, char *nick)
{
	channel_t *c;
	user_t *u;
	chanuser_t *cu;
	boolean_t isnew = FALSE;
	mychan_t *mc;
	metadata_t *md;
	time_t ts;

	u = user_find_named(nick);
	if (!u)
		return;
	c = channel_find(chan);
	if (c == NULL)
	{
		mc = mychan_find(chan);
		if (chansvs.changets && mc != NULL)
		{
			/* Use the previous TS if known, registration
			 * time otherwise, but never ever create a channel
			 * with TS 0 -- jilles */
			ts = mc->registered;
			md = metadata_find(mc, "private:channelts");
			if (md != NULL)
				ts = atol(md->value);
			if (ts == 0)
				ts = CURRTIME;
		}
		else
			ts = CURRTIME;
		c = channel_add(chan, ts, me.me);
		c->modes |= CMODE_NOEXT | CMODE_TOPIC;
		if (mc != NULL)
			check_modes(mc, FALSE);
		isnew = TRUE;
	}
	else if ((cu = chanuser_find(c, u)))
	{
		slog(LG_DEBUG, "join(): i'm already in `%s'", c->name);
		return;
	}
	join_sts(c, u, isnew, channel_modes(c, TRUE));
	cu = chanuser_add(c, CLIENT_NAME(u));
	cu->modes |= CMODE_OP;
	if (isnew)
	{
		hook_call_event("channel_add", c);
		if (config_options.chan != NULL && !irccasecmp(config_options.chan, c->name))
			joinall(config_options.chan);
	}
}

/* part a channel */
void part(char *chan, char *nick)
{
	channel_t *c = channel_find(chan);
	user_t *u = user_find_named(nick);

	if (!u || !c)
		return;
	if (!chanuser_find(c, u))
		return;
	if (me.connected)
		part_sts(c, u);
	chanuser_delete(c, u);
}

void services_init(void)
{
	service_t *svs;
	mowgli_patricia_iteration_state_t state;

	MOWGLI_PATRICIA_FOREACH(svs, &state, services)
	{
		if (ircd->uses_uid && svs->me->uid[0] == '\0')
			user_changeuid(svs->me, uid_get());
		else if (!ircd->uses_uid && svs->me->uid[0] != '\0')
			user_changeuid(svs->me, NULL);
		if (!ircd->uses_uid)
			kill_id_sts(NULL, svs->name, "Attempt to use service nick");
		introduce_nick(svs->me);
	}
}

void joinall(char *name)
{
	service_t *svs;
	mowgli_patricia_iteration_state_t state;

	if (name == NULL)
		return;

	MOWGLI_PATRICIA_FOREACH(svs, &state, services)
	{
		join(name, svs->name);
	}
}

void partall(char *name)
{
	mowgli_patricia_iteration_state_t state;
	service_t *svs;
	mychan_t *mc;

	if (name == NULL)
		return;
	mc = mychan_find(name);
	MOWGLI_PATRICIA_FOREACH(svs, &state, services)
	{
		if (svs == chansvs.me && mc != NULL && config_options.join_chans)
			continue;
		/* Do not cache this channel_find(), the
		 * channel may disappear under our feet
		 * -- jilles */
		if (chanuser_find(channel_find(name), svs->me))
			part(name, svs->name);
	}
}

/* reintroduce a service e.g. after it's been killed -- jilles */
void reintroduce_user(user_t *u)
{
	node_t *n;
	channel_t *c;
	service_t *svs;

	svs = find_service(u->nick);
	if (svs == NULL)
	{
		slog(LG_DEBUG, "tried to reintroduce_user non-service %s", u->nick);
		return;
	}
	/* Reintroduce with a new UID.  This avoids problems distinguishing
	 * commands targeted at the old and new user.
	 */
	if (*u->uid)
	{
		user_changeuid(u, uid_get());
	}
	else
	{
		/* Ensure it is really gone before introducing the new one.
		 * This also helps with nick collisions.
		 * With UID this is not necessary as we will
		 * reintroduce with a new UID, and nick collisions
		 * are unambiguous.
		 */
		if (!ircd->uses_uid)
			kill_id_sts(NULL, u->nick, "Service nick");
	}
	introduce_nick(u);
	LIST_FOREACH(n, u->channels.head)
	{
		c = ((chanuser_t *)n->data)->chan;
		if (LIST_LENGTH(&c->members) > 1 || c->modes & ircd->perm_mode)
			join_sts(c, u, 0, channel_modes(c, TRUE));
		else
		{
			/* channel will have been destroyed... */
			/* XXX resend the bans instead of destroying them? */
			chanban_clear(c);
			join_sts(c, u, 1, channel_modes(c, TRUE));
			if (c->topic != NULL)
				topic_sts(c, c->topic_setter, c->topicts, 0, c->topic);
		}
	}
}

void verbose(mychan_t *mychan, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZE];

	if (mychan->chan == NULL)
		return;

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZE, fmt, ap);
	va_end(ap);

	if ((MC_VERBOSE | MC_FORCEVERBOSE) & mychan->flags)
		notice(chansvs.nick, mychan->name, "%s", buf);
	else if (MC_VERBOSE_OPS & mychan->flags)
		wallchops(chansvs.me->me, mychan->chan, buf);
}

void snoop(const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZE];

	if (!config_options.chan)
		return;

	if (me.bursting)
		return;

	if (!channel_find(config_options.chan))
		return;

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZE, fmt, ap);
	va_end(ap);

	msg(opersvs.nick, config_options.chan, "%s", buf);
}

/* protocol wrapper for nickchange/nick burst */
void handle_nickchange(user_t *u)
{
	if (u == NULL)
		return;

	if (runflags & RF_LIVE && log_debug_enabled())
		notice(globsvs.me != NULL ? globsvs.nick : me.name, u->nick, "Services are presently running in debug mode, attached to a console. You should take extra caution when utilizing your services passwords.");

	hook_call_event("nick_check", u);
}

/* User u is bursted as being logged in to login (if not NULL) or as
 * being identified to their current nick (if login is NULL)
 * Update the administration or log them out on ircd
 * How to use this in protocol modules:
 * 1. if login info is bursted in a command that always occurs, call
 *    this if the user is logged in, before handle_nickchange()
 * 2. if it is bursted in a command that doesn't always occur, use
 *    netwide EOB as in the ratbox module; call this if the user is logged
 *    in; for all users, postpone handle_nickchange() until the user's
 *    server confirms EOB
 * -- jilles
 */
void handle_burstlogin(user_t *u, char *login)
{
	mynick_t *mn;
	myuser_t *mu;
	node_t *n;

	if (login != NULL)
		/* don't allow alias nicks here -- jilles */
		mu = myuser_find(login);
	else
	{
		mn = mynick_find(u->nick);
		mu = mn != NULL ? mn->owner : NULL;
		login = mu != NULL ? mu->name : u->nick;
	}
	if (mu == NULL)
	{
		/* account dropped during split...
		 * if we have an authentication service, log them out */
		if (authservice_loaded || backend_loaded)
		{
			slog(LG_DEBUG, "handle_burstlogin(): got nonexistent login %s for user %s", login, u->nick);
			if (authservice_loaded)
			{
				notice(nicksvs.nick ? nicksvs.nick : me.name, u->nick, _("Account %s dropped, forcing logout"), login);
				ircd_on_logout(u->nick, login, NULL);
			}
			return;
		}
		/* we're running without a persistent db, create it */
		mu = myuser_add(login, "*", "noemail", MU_CRYPTPASS);
		metadata_add(mu, "fake", "1");
	}
	if (u->myuser != NULL)	/* already logged in, hmm */
		return;
	if (mu->flags & MU_NOBURSTLOGIN && authservice_loaded)
	{
		/* no splits for this account, this bursted login cannot
		 * be legit...
		 * if we have an authentication service, log them out */
		slog(LG_INFO, "handle_burstlogin(): got illegit login %s for user %s", login, u->nick);
		notice(nicksvs.nick ? nicksvs.nick : me.name, u->nick, _("Login to account %s seems invalid, forcing logout"), login);
		ircd_on_logout(u->nick, login, NULL);
		return;
	}
	u->myuser = mu;
	n = node_create();
	node_add(u, n, &mu->logins);
	slog(LG_DEBUG, "handle_burstlogin(): automatically identified %s as %s", u->nick, login);
}

void handle_setlogin(sourceinfo_t *si, user_t *u, char *login)
{
	mynick_t *mn;
	myuser_t *mu;
	node_t *n;

	if (login != NULL)
		/* don't allow alias nicks here -- jilles */
		mu = myuser_find(login);
	else
	{
		mn = mynick_find(u->nick);
		mu = mn != NULL ? mn->owner : NULL;
		login = mu != NULL ? mu->name : u->nick;
	}

	if (authservice_loaded)
	{
		wallops("Ignoring attempt from %s to set login name for %s to %s",
				get_oper_name(si), u->nick, login);
		return;
	}

	if (u->myuser != NULL)
	{
		n = node_find(u, &u->myuser->logins);
		if (n != NULL)
		{
			node_del(n, &u->myuser->logins);
			node_free(n);
		}
		u->myuser = NULL;
	}
	if (mu == NULL)
	{
		if (backend_loaded)
		{
			slog(LG_DEBUG, "handle_setlogin(): got nonexistent login %s for user %s", login, u->nick);
			return;
		}
		/* we're running without a persistent db, create it */
		mu = myuser_add(login, "*", "noemail", MU_CRYPTPASS);
		metadata_add(mu, "fake", "1");
	}
	u->myuser = mu;
	n = node_create();
	node_add(u, n, &mu->logins);
	slog(LG_DEBUG, "handle_setlogin(): %s set %s logged in as %s",
			get_oper_name(si), u->nick, login);
}

void handle_clearlogin(sourceinfo_t *si, user_t *u)
{
	node_t *n;

	if (authservice_loaded)
	{
		wallops("Ignoring attempt from %s to clear login name for %s",
				get_oper_name(si), u->nick);
		return;
	}

	if (u->myuser == NULL)
		return;

	slog(LG_DEBUG, "handle_clearlogin(): %s cleared login for %s (%s)",
			get_oper_name(si), u->nick, u->myuser->name);
	n = node_find(u, &u->myuser->logins);
	if (n != NULL)
	{
		node_del(n, &u->myuser->logins);
		node_free(n);
	}
	u->myuser = NULL;
}

/* this could be done with more finesse, but hey! */
void notice(const char *from, const char *to, const char *fmt, ...)
{
	va_list args;
	char buf[BUFSIZE];
	const char *str = translation_get(fmt);
	user_t *u;
	channel_t *c;

	va_start(args, fmt);
	vsnprintf(buf, BUFSIZE, str, args);
	va_end(args);

	if (*to == '#')
	{
		c = channel_find(to);
		if (c != NULL)
			notice_channel_sts(user_find_named(from), c, buf);
	}
	else
	{
		u = user_find_named(to);
		if (u != NULL)
		{
			if (u->myuser != NULL && u->myuser->flags & MU_USE_PRIVMSG)
				msg(from, to, "%s", buf);
			else
				notice_user_sts(user_find_named(from), u, buf);
		}
	}
}

/*
 * change_notify()
 *
 * Sends a change notification to a user affected by that change, provided
 * that he has not disabled the messages (MU_QUIETCHG is not set).
 *
 * Inputs:
 *       - string representing source (for compatibility with notice())
 *       - user_t object to send the notice to
 *       - printf-style string containing the data to send and any args
 *
 * Outputs:
 *       - nothing
 *
 * Side Effects:
 *       - a notice is sent to a user if MU_QUIETCHG is not set.
 */
void change_notify(const char *from, user_t *to, const char *fmt, ...)
{
	va_list args;
	char buf[BUFSIZE];

	va_start(args, fmt);
	vsnprintf(buf, BUFSIZE, fmt, args);
	va_end(args);

	if (is_internal_client(to))
		return;
	if (to->myuser != NULL && to->myuser->flags & MU_QUIETCHG)
		return;

	notice_user_sts(user_find_named(from), to, buf);
}

/*
 * bad_password()
 *
 * Registers an attempt to authenticate with an incorrect password.
 *
 * Inputs:
 *       - sourceinfo_t representing what sent the bad password
 *       - myuser_t object attempt was against
 *
 * Outputs:
 *       - whether the user was killed off the network
 *
 * Side Effects:
 *       - flood counter is incremented for user
 *       - attempt is registered in metadata
 *       - opers warned if necessary
 *
 * Note:
 *       - kills are currently not done
 */
boolean_t bad_password(sourceinfo_t *si, myuser_t *mu)
{
	const char *mask;
	struct tm tm;
	char numeric[21], strfbuf[32];
	int count;
	metadata_t *md_failnum;

	/* If the user is already logged in, no paranoia is needed,
	 * as they could /ns set password anyway.
	 */
	if (si->smu == mu)
		return FALSE;

	command_add_flood(si, FLOOD_MODERATE);

	mask = get_source_mask(si);

	md_failnum = metadata_find(mu, "private:loginfail:failnum");
	count = md_failnum ? atoi(md_failnum->value) : 0;
	count++;
	snprintf(numeric, sizeof numeric, "%d", count);
	md_failnum = metadata_add(mu, "private:loginfail:failnum", numeric);
	metadata_add(mu, "private:loginfail:lastfailaddr", mask);
	snprintf(numeric, sizeof numeric, "%lu", (unsigned long)CURRTIME);
	metadata_add(mu, "private:loginfail:lastfailtime", numeric);

	if (is_soper(mu))
		snoop("SOPER:AF: \2%s\2 as \2%s\2", get_source_name(si), mu->name);

	if (atoi(md_failnum->value) == 10)
	{
		time_t ts = CURRTIME;
		tm = *localtime(&ts);
		strftime(strfbuf, sizeof(strfbuf) - 1, "%b %d %H:%M:%S %Y", &tm);

		wallops("Warning: Numerous failed login attempts to \2%s\2. Last attempt received from \2%s\2 on %s.", mu->name, mask, strfbuf);
	}

	return FALSE;
}

void command_fail(sourceinfo_t *si, faultcode_t code, const char *fmt, ...)
{
	va_list args;
	char buf[BUFSIZE];
	const char *str = translation_get(fmt);

	va_start(args, fmt);
	vsnprintf(buf, sizeof buf, str, args);
	va_end(args);

	if (si->su == NULL)
	{
		if (si->v != NULL && si->v->cmd_fail)
			si->v->cmd_fail(si, code, buf);
		return;
	}

	if (use_privmsg && si->smu != NULL && si->smu->flags & MU_USE_PRIVMSG)
		msg(si->service->name, si->su->nick, "%s", buf);
	else
		notice_user_sts(si->service->me, si->su, buf);
}

void command_success_nodata(sourceinfo_t *si, const char *fmt, ...)
{
	va_list args;
	char buf[BUFSIZE];
	const char *str = translation_get(fmt);
	char *p, *q;
	char space[] = " ";

	if (si->output_limit && si->output_count > si->output_limit)
		return;
	si->output_count++;

	va_start(args, fmt);
	vsnprintf(buf, BUFSIZE, str, args);
	va_end(args);

	if (si->su == NULL)
	{
		if (si->v != NULL && si->v->cmd_fail)
			si->v->cmd_success_nodata(si, buf);
		return;
	}

	if (si->output_limit && si->output_count > si->output_limit)
	{
		notice(si->service->name, si->su->nick, _("Output limit (%u) exceeded, halting output"), si->output_limit);
		return;
	}

	p = buf;
	do
	{
		q = strchr(p, '\n');
		if (q != NULL)
		{
			*q++ = '\0';
			if (*q == '\0')
				return; /* ending with \n */
		}
		if (*p == '\0')
			p = space; /* replace empty lines with a space */
		if (use_privmsg && si->smu != NULL && si->smu->flags & MU_USE_PRIVMSG)
			msg(si->service->name, si->su->nick, "%s", p);
		else
			notice_user_sts(si->service->me, si->su, p);
		p = q;
	} while (p != NULL);
}

void command_success_string(sourceinfo_t *si, const char *result, const char *fmt, ...)
{
	va_list args;
	char buf[BUFSIZE];
	const char *str = translation_get(fmt);

	va_start(args, fmt);
	vsnprintf(buf, BUFSIZE, str, args);
	va_end(args);

	if (si->su == NULL)
	{
		if (si->v != NULL && si->v->cmd_fail)
			si->v->cmd_success_string(si, result, buf);
		return;
	}

	if (use_privmsg && si->smu != NULL && si->smu->flags & MU_USE_PRIVMSG)
		msg(si->service->name, si->su->nick, "%s", buf);
	else
		notice_user_sts(si->service->me, si->su, buf);
}

static void command_table_cb(const char *line, void *data)
{
	command_success_nodata(data, "%s", line);
}

void command_success_table(sourceinfo_t *si, table_t *table)
{
	table_render(table, command_table_cb, si);
}

const char *get_source_name(sourceinfo_t *si)
{
	static char result[NICKLEN+NICKLEN+10];

	if (si->su != NULL)
	{
		if (si->smu && !irccasecmp(si->su->nick, si->smu->name))
			snprintf(result, sizeof result, "%s", si->su->nick);
		else
			snprintf(result, sizeof result, "%s (%s)", si->su->nick,
					si->smu ? si->smu->name : "");
	}
	else if (si->s != NULL)
		snprintf(result, sizeof result, "%s", si->s->name);
	else
	{
		snprintf(result, sizeof result, "<%s>%s", si->v->description,
				si->smu ? si->smu->name : "");
	}
	return result;
}

const char *get_source_mask(sourceinfo_t *si)
{
	static char result[NICKLEN+USERLEN+HOSTLEN+10];

	if (si->su != NULL)
	{
		snprintf(result, sizeof result, "%s!%s@%s", si->su->nick,
				si->su->user, si->su->vhost);
	}
	else if (si->s != NULL)
		snprintf(result, sizeof result, "%s", si->s->name);
	else
	{
		snprintf(result, sizeof result, "<%s>%s", si->v->description,
				si->smu ? si->smu->name : "");
	}
	return result;
}

const char *get_oper_name(sourceinfo_t *si)
{
	static char result[NICKLEN+USERLEN+HOSTLEN+NICKLEN+10];

	if (si->su != NULL)
	{
		if (si->smu == NULL)
			snprintf(result, sizeof result, "%s!%s@%s{%s}", si->su->nick,
					si->su->user, si->su->vhost,
					si->su->server->name);
		else if (!irccasecmp(si->su->nick, si->smu->name))
			snprintf(result, sizeof result, "%s", si->su->nick);
		else
			snprintf(result, sizeof result, "%s (%s)", si->su->nick,
					si->smu ? si->smu->name : "");
	}
	else if (si->s != NULL)
		snprintf(result, sizeof result, "%s", si->s->name);
	else
	{
		snprintf(result, sizeof result, "<%s>%s", si->v->description,
				si->smu ? si->smu->name : "");
	}
	return result;
}

const char *get_storage_oper_name(sourceinfo_t *si)
{
	static char result[NICKLEN+USERLEN+HOSTLEN+NICKLEN+10];

	if (si->smu != NULL)
		snprintf(result, sizeof result, "%s", si->smu->name);
	else if (si->su != NULL)
	{
		snprintf(result, sizeof result, "%s!%s@%s{%s}", si->su->nick,
				si->su->user, si->su->vhost,
				si->su->server->name);
	}
	else if (si->s != NULL)
		snprintf(result, sizeof result, "%s", si->s->name);
	else
		snprintf(result, sizeof result, "<%s>", si->v->description);
	return result;
}

void wallops(const char *fmt, ...)
{
	va_list args;
	char buf[BUFSIZE];

	if (config_options.silent)
		return;

	va_start(args, fmt);
	vsnprintf(buf, BUFSIZE, translation_get(fmt), args);
	va_end(args);

	if (me.me != NULL && me.connected)
		wallops_sts(buf);
	else
		slog(LG_ERROR, "wallops(): unable to send: %s", buf);
}

void verbose_wallops(const char *fmt, ...)
{
	va_list args;
	char buf[BUFSIZE];

	if (config_options.silent || !config_options.verbose_wallops)
		return;

	va_start(args, fmt);
	vsnprintf(buf, BUFSIZE, translation_get(fmt), args);
	va_end(args);

	if (me.me != NULL && me.connected)
		wallops_sts(buf);
	else
		slog(LG_ERROR, "verbose_wallops(): unable to send: %s", buf);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
