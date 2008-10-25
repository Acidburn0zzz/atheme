/*
 * atheme-services: A collection of minimalist IRC services
 * object.c: Object management.
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

static BlockHeap *metadata_heap;	/* HEAP_CHANUSER */

void init_metadata(void)
{
	metadata_heap = BlockHeapCreate(sizeof(metadata_t), HEAP_CHANUSER);
	if (metadata_heap == NULL)
	{
		slog(LG_ERROR, "init_metadata(): block allocator failure.");
		exit(EXIT_FAILURE);
	}
}

/*
 * object_init
 *
 * Populates the object manager part of an object.
 *
 * Inputs:
 *      - pointer to object manager area
 *      - (optional) name of object
 *      - (optional) custom destructor; if this is specified it must free
 *        the metadata itself
 *
 * Outputs:
 *      - none
 *
 * Side Effects:
 *      - none
 */
void object_init(object_t *obj, const char *name, destructor_t des)
{
	return_if_fail(obj != NULL);

	if (name != NULL)
		obj->name = sstrdup(name);

	obj->destructor = des;
#ifdef USE_OBJECT_REF
	obj->refcount = 1;
#endif
}

#ifdef USE_OBJECT_REF
/*
 * object_ref
 *
 * Increment the reference counter on an object.
 *
 * Inputs:
 *      - the object to refcount
 *
 * Outputs:
 *      - none
 *
 * Side Effects:
 *      - none
 */
void * object_ref(void *object)
{
	return_val_if_fail(object != NULL, NULL);

	object(object)->refcount++;

	return object;
}
#endif

/*
 * object_unref
 *
 * Decrement the reference counter on an object.
 *
 * Inputs:
 *      - the object to refcount
 *
 * Outputs:
 *      - none
 *
 * Side Effects:
 *      - if the refcount is 0, the object is destroyed.
 */
void object_unref(void *object)
{
	object_t *obj;

	return_if_fail(object != NULL);
	obj = object(object);

#ifdef USE_OBJECT_REF
	obj->refcount--;

	if (obj->refcount <= 0)
#endif
	{
		if (obj->name != NULL)
			free(obj->name);

		if (obj->destructor != NULL)
			obj->destructor(obj);
		else
		{
			metadata_delete_all(obj);
			free(obj);
		}
	}
}

metadata_t *metadata_add(void *target, const char *name, const char *value)
{
	object_t *obj;
	metadata_t *md;

	return_val_if_fail(name != NULL, NULL);
	return_val_if_fail(value != NULL, NULL);

	obj = object(target);

	if ((md = metadata_find(target, name)))
		metadata_delete(target, name);

	md = BlockHeapAlloc(metadata_heap);

	md->name = strshare_get(name);
	md->value = sstrdup(value);

	node_add(md, &md->node, &obj->metadata);

	if (!strncmp("private:", md->name, 8))
		md->private = TRUE;

	/* XXX only call the hook for users */
	if (obj->destructor == (destructor_t)myuser_delete)
	{
	}

	return md;
}

void metadata_delete(void *target, const char *name)
{
	object_t *obj;
	metadata_t *md = metadata_find(target, name);

	if (!md)
		return;

	obj = object(target);

	node_del(&md->node, &obj->metadata);

	strshare_unref(md->name);
	free(md->value);

	BlockHeapFree(metadata_heap, md);
}

metadata_t *metadata_find(void *target, const char *name)
{
	object_t *obj;
	node_t *n;
	metadata_t *md;

	return_val_if_fail(name != NULL, NULL);

	obj = object(target);

	LIST_FOREACH(n, obj->metadata.head)
	{
		md = n->data;

		if (!strcasecmp(md->name, name))
			return md;
	}

	return NULL;
}

void metadata_delete_all(void *target)
{
	object_t *obj;
	node_t *n, *tn;
	metadata_t *md;

	obj = object(target);
	LIST_FOREACH_SAFE(n, tn, obj->metadata.head)
	{
		md = n->data;
		metadata_delete(obj, md->name);
	}
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
