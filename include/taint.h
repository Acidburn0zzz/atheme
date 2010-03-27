/*
 * Copyright (c) 2010 William Pitcock <nenolod@atheme.org>.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Management of tainted running configuration reasons and status.
 */

#ifndef __TAINT_H__
#define __TAINT_H__

typedef struct {
	char cond[BUFSIZE];
	char file[BUFSIZE];
	int line;
	char buf[BUFSIZE];
	node_t node;
} taint_reason_t;

E list_t taint_list;

#define IS_TAINTED	LIST_LENGTH(&taint_list)
#define TAINT_ON(cond, reason) \
	if ((cond))						\
	{							\
		taint_reason_t *tr;				\
		tr = scalloc(sizeof(taint_reason_t), 1);	\
		strlcpy(tr->cond, ##cond, BUFSIZE);		\
		strlcpy(tr->file, __FILE__, BUFSIZE);		\
		tr->line = __LINE__;				\
		strlcpy(tr->buf, (reason), BUFSIZE);		\
		node_add(&taint_list, &tr->node, tr);		\
		slog(LG_ERROR, "TAINTED: %s", (reason));	\
		if (!config_options.allow_taint)		\
		{						\
			slog(LG_ERROR, "exiting due to taint");	\
			exit(EXIT_FAILURE);			\
		}						\
	}

#endif
