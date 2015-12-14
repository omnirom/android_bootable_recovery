/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB
*/

#include "fuse_i.h"
#include "fuse_misc.h"
#include "fuse_common_compat.h"
#include "fuse_lowlevel_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

struct fuse_chan {
	struct fuse_chan_ops op;

	struct fuse_session *se;

	int fd;

	size_t bufsize;

	void *data;

	int compat;
};

struct fuse_session *fuse_session_new(struct fuse_session_ops *op, void *data)
{
	struct fuse_session *se = (struct fuse_session *) malloc(sizeof(*se));
	if (se == NULL) {
		fprintf(stderr, "fuse: failed to allocate session\n");
		return NULL;
	}

	memset(se, 0, sizeof(*se));
	se->op = *op;
	se->data = data;

	return se;
}

void fuse_session_add_chan(struct fuse_session *se, struct fuse_chan *ch)
{
	assert(se->ch == NULL);
	assert(ch->se == NULL);
	se->ch = ch;
	ch->se = se;
}

void fuse_session_remove_chan(struct fuse_chan *ch)
{
	struct fuse_session *se = ch->se;
	if (se) {
		assert(se->ch == ch);
		se->ch = NULL;
		ch->se = NULL;
	}
}

struct fuse_chan *fuse_session_next_chan(struct fuse_session *se,
					 struct fuse_chan *ch)
{
	assert(ch == NULL || ch == se->ch);
	if (ch == NULL)
		return se->ch;
	else
		return NULL;
}

void fuse_session_process(struct fuse_session *se, const char *buf, size_t len,
			  struct fuse_chan *ch)
{
	se->op.process(se->data, buf, len, ch);
}

void fuse_session_process_buf(struct fuse_session *se,
			      const struct fuse_buf *buf, struct fuse_chan *ch)
{
	if (se->process_buf) {
		se->process_buf(se->data, buf, ch);
	} else {
		assert(!(buf->flags & FUSE_BUF_IS_FD));
		fuse_session_process(se->data, buf->mem, buf->size, ch);
	}
}

int fuse_session_receive_buf(struct fuse_session *se, struct fuse_buf *buf,
			     struct fuse_chan **chp)
{
	int res;

	if (se->receive_buf) {
		res = se->receive_buf(se, buf, chp);
	} else {
		res = fuse_chan_recv(chp, buf->mem, buf->size);
		if (res > 0)
			buf->size = res;
	}

	return res;
}


void fuse_session_destroy(struct fuse_session *se)
{
	if (se->op.destroy)
		se->op.destroy(se->data);
	if (se->ch != NULL)
		fuse_chan_destroy(se->ch);
	free(se);
}

void fuse_session_exit(struct fuse_session *se)
{
	if (se->op.exit)
		se->op.exit(se->data, 1);
	se->exited = 1;
}

void fuse_session_reset(struct fuse_session *se)
{
	if (se->op.exit)
		se->op.exit(se->data, 0);
	se->exited = 0;
}

int fuse_session_exited(struct fuse_session *se)
{
	if (se->op.exited)
		return se->op.exited(se->data);
	else
		return se->exited;
}

void *fuse_session_data(struct fuse_session *se)
{
	return se->data;
}

static struct fuse_chan *fuse_chan_new_common(struct fuse_chan_ops *op, int fd,
					      size_t bufsize, void *data,
					      int compat)
{
	struct fuse_chan *ch = (struct fuse_chan *) malloc(sizeof(*ch));
	if (ch == NULL) {
		fprintf(stderr, "fuse: failed to allocate channel\n");
		return NULL;
	}

	memset(ch, 0, sizeof(*ch));
	ch->op = *op;
	ch->fd = fd;
	ch->bufsize = bufsize;
	ch->data = data;
	ch->compat = compat;

	return ch;
}

struct fuse_chan *fuse_chan_new(struct fuse_chan_ops *op, int fd,
				size_t bufsize, void *data)
{
	return fuse_chan_new_common(op, fd, bufsize, data, 0);
}

struct fuse_chan *fuse_chan_new_compat24(struct fuse_chan_ops_compat24 *op,
					 int fd, size_t bufsize, void *data)
{
	return fuse_chan_new_common((struct fuse_chan_ops *) op, fd, bufsize,
				    data, 24);
}

int fuse_chan_fd(struct fuse_chan *ch)
{
	return ch->fd;
}

int fuse_chan_clearfd(struct fuse_chan *ch)
{
	if (ch == NULL)
		return -1;

	int fd = ch->fd;
	ch->fd = -1;
	return fd;
}

size_t fuse_chan_bufsize(struct fuse_chan *ch)
{
	return ch->bufsize;
}

void *fuse_chan_data(struct fuse_chan *ch)
{
	return ch->data;
}

struct fuse_session *fuse_chan_session(struct fuse_chan *ch)
{
	return ch->se;
}

int fuse_chan_recv(struct fuse_chan **chp, char *buf, size_t size)
{
	struct fuse_chan *ch = *chp;
	if (ch->compat)
		return ((struct fuse_chan_ops_compat24 *) &ch->op)
			->receive(ch, buf, size);
	else
		return ch->op.receive(chp, buf, size);
}

int fuse_chan_receive(struct fuse_chan *ch, char *buf, size_t size)
{
	int res;

	res = fuse_chan_recv(&ch, buf, size);
	return res >= 0 ? res : (res != -EINTR && res != -EAGAIN) ? -1 : 0;
}

int fuse_chan_send(struct fuse_chan *ch, const struct iovec iov[], size_t count)
{
	return ch->op.send(ch, iov, count);
}

void fuse_chan_destroy(struct fuse_chan *ch)
{
	fuse_session_remove_chan(ch);
	if (ch->op.destroy)
		ch->op.destroy(ch);
	free(ch);
}

#ifndef __FreeBSD__
FUSE_SYMVER(".symver fuse_chan_new_compat24,fuse_chan_new@FUSE_2.4");
#endif
