/*
  This file is part of libmicrohttpd
  Copyright (C) 2007-2017 Daniel Pittman and Christian Grothoff
  Copyright (C) 2017 Maru Berezin

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

/**
 * @file microhttpd/connection_http2.c
 * @brief Methods for managing HTTP/2 connections
 * @author maru (Maru Berezin)
 */

#include "connection_http2.h"
#include "mhd_mono_clock.h"
#include "connection.h"

#ifdef HTTP2_SUPPORT

#define ENTER(format, args...) fprintf(stderr, "\e[31;1m[%s]\e[0m " format "\n", __FUNCTION__, ##args)

#define errx(exitcode, format, args...)                                        \
  {                                                                            \
    warnx(format, ##args);                                                     \
    exit(exitcode);                                                            \
  }
#define warn(format, args...) warnx(format ": %s", ##args, strerror(errno))
#define warnx(format, args...) fprintf(stderr, format "\n", ##args)

#define ARRLEN(x) (sizeof(x) / sizeof(x[0]))

#define MAKE_NV(NAME, VALUE)                                                   \
  {                                                                            \
    (uint8_t *)NAME, (uint8_t *)VALUE, sizeof(NAME) - 1, sizeof(VALUE) - 1,    \
        NGHTTP2_NV_FLAG_NONE                                                   \
  }

/**
 *
 *
 * @param h2
 * @param stream_data
 */
static void
add_stream (struct http2_conn *h2,
            struct http2_stream_data *stream_data)
{
  stream_data->next = h2->head.next;
  h2->head.next = stream_data;
  stream_data->prev = &h2->head;
  if (stream_data->next) {
    stream_data->next->prev = stream_data;
  }
}


/**
 *
 *
 * @param h2
 * @param stream_data
 */
static void
remove_stream (struct http2_conn *h2,
               struct http2_stream_data *stream_data)
{
  (void)h2;

  stream_data->prev->next = stream_data->next;
  if (stream_data->next) {
    stream_data->next->prev = stream_data->prev;
  }
}


/**
 *
 *
 * @param h2
 * @param stream_data
 */
static struct http2_stream_data*
create_http2_stream_data (struct http2_conn *h2, int32_t stream_id)
{
  struct http2_stream_data *stream_data;
  stream_data = malloc ( sizeof (struct http2_stream_data));
  if (NULL == stream_data)
    {
      return NULL;
    }
  memset (stream_data, 0, sizeof (struct http2_stream_data));
  stream_data->stream_id = stream_id;

  add_stream (h2, stream_data);
  return stream_data;
}

/**
 *
 *
 * @param stream_data
 */
static void
delete_http2_stream_data (struct http2_stream_data *stream_data)
{
  free (stream_data);
}

static ssize_t str_read_callback(nghttp2_session *session,
                                 int32_t stream_id, uint8_t *buf,
                                 size_t length, uint32_t *data_flags,
                                 nghttp2_data_source *source,
                                 void *user_data) {
  ssize_t len = strlen(source->ptr);
  memcpy(buf, source->ptr, len);
  *data_flags |= NGHTTP2_DATA_FLAG_EOF;
  return len;
}

static int
send_response(nghttp2_session *session, int32_t stream_id,
                         nghttp2_nv *nva, size_t nvlen, void *ptr)
{
  int rv;
  nghttp2_data_provider data_prd;
  data_prd.source.ptr = ptr;
  data_prd.read_callback = str_read_callback;
  ENTER();

  rv = nghttp2_submit_response(session, stream_id, nva, nvlen, &data_prd);
  if (rv != 0) {
    warnx("Fatal error: %s", nghttp2_strerror(rv));
    return -1;
  }
  return 0;
}


static ssize_t
send_callback(nghttp2_session *session, const uint8_t *data,
                             size_t length, int flags, void *user_data)
{
  struct MHD_Connection *connection = (struct MHD_Connection *) user_data;
  (void)session;
  (void)flags;
  ENTER();

  connection->send_cls (connection, data, length);
  return (ssize_t)length;
}

static int
on_request_recv(nghttp2_session *session,
                           struct http2_conn *h2,
                           struct http2_stream_data *stream_data)
{
  int fd;
  nghttp2_nv hdrs[] = {MAKE_NV(":status", "200")};
  char *rel_path;
  ENTER();

  // if (!stream_data->request_path) {
  //   if (error_reply(session, stream_data) != 0) {
  //     return NGHTTP2_ERR_CALLBACK_FAILURE;
  //   }
  //   return 0;
  // }
  // fprintf(stderr, "%s GET %s\n", h2->client_addr,
  //         stream_data->request_path);
  // if (!check_path(stream_data->request_path)) {
  //   if (error_reply(session, stream_data) != 0) {
  //     return NGHTTP2_ERR_CALLBACK_FAILURE;
  //   }
  //   return 0;
  // }
  // for (rel_path = stream_data->request_path; *rel_path == '/'; ++rel_path)
  //   ;
  // fd = open(rel_path, O_RDONLY);
  // if (fd == -1) {
  //   if (error_reply(session, stream_data) != 0) {
  //     return NGHTTP2_ERR_CALLBACK_FAILURE;
  //   }
  //   return 0;
  // }
  // stream_data->fd = fd;
  //
  char *page = "<html><head><title>libmicrohttpd demo</title></head><body>libmicrohttpd demo</body></html>\n";
  if (send_response(session, stream_data->stream_id, hdrs, ARRLEN(hdrs), page) !=
      0) {
    close(fd);
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
  return 0;
}

#define FRAME_TYPE(x) (x==0?"DATA":(x==1?"HEADERS":(x==2?"PRIORITY":(x==4?"SETTINGS":"-"))))

static int
on_frame_recv_callback(nghttp2_session *session,
                                  const nghttp2_frame *frame, void *user_data)
{
  struct http2_conn *h2 = (struct http2_conn *) user_data;
  struct http2_stream_data *stream_data;
  ENTER("frame->hd.type %s", FRAME_TYPE(frame->hd.type));
  switch (frame->hd.type) {
  case NGHTTP2_DATA:
  case NGHTTP2_HEADERS:
    /* Check that the client request has finished */
    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
      stream_data =
          nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
      /* For DATA and HEADERS frame, this callback may be called after
         on_stream_close_callback. Check that stream still alive. */
      if (!stream_data) {
        return 0;
      }
      return on_request_recv(session, h2, stream_data);
    }
    break;
  default:
    break;
  }
  return 0;
}

static int on_begin_headers_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     void *user_data) {
  struct http2_conn *h2 = (struct http2_conn *)user_data;
  struct http2_stream_data *stream_data;
  ENTER();

  if (frame->hd.type != NGHTTP2_HEADERS ||
      frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
    return 0;
  }
  stream_data = create_http2_stream_data(h2, frame->hd.stream_id);
  nghttp2_session_set_stream_user_data(session, frame->hd.stream_id,
                                       stream_data);
  return 0;
}

/* nghttp2_on_header_callback: Called when nghttp2 library emits
   single header name/value pair. */
static int on_header_callback(nghttp2_session *session,
                              const nghttp2_frame *frame, const uint8_t *name,
                              size_t namelen, const uint8_t *value,
                              size_t valuelen, uint8_t flags, void *user_data) {
  struct http2_stream_data *stream_data;
  const char PATH[] = ":path";
  (void)flags;
  (void)user_data;
  ENTER();

  switch (frame->hd.type) {
  case NGHTTP2_HEADERS:
    if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
      break;
    }
    stream_data =
        nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
    // if (!stream_data || stream_data->request_path) {
    //   break;
    // }
    // if (namelen == sizeof(PATH) - 1 && memcmp(PATH, name, namelen) == 0) {
    //   size_t j;
    //   for (j = 0; j < valuelen && value[j] != '?'; ++j)
    //     ;
    //   stream_data->request_path = percent_decode(value, j);
    // }
    break;
  }
  return 0;
}

static int
on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                    uint32_t error_code, void *user_data)
{
  struct http2_conn *h2 = (struct http2_conn *)user_data;
  struct http2_stream_data *stream_data;
  (void)error_code;
  ENTER();

  stream_data = nghttp2_session_get_stream_user_data(session, stream_id);
  if (!stream_data) {
    return 0;
  }
  remove_stream(h2, stream_data);
  delete_http2_stream_data(stream_data);
  return 0;
}


/**
 *
 *
 * @param h2
 */
static int
http2_init_session (struct MHD_Connection *connection)
{
  int rv;
  struct http2_conn *h2 = connection->h2;
  nghttp2_session_callbacks *callbacks;

  rv = nghttp2_session_callbacks_new (&callbacks);
  if (rv != 0)
  {
    return rv;
  }

  nghttp2_session_callbacks_set_send_callback (callbacks, send_callback);

  nghttp2_session_callbacks_set_on_frame_recv_callback (callbacks,
                                                       on_frame_recv_callback);

  nghttp2_session_callbacks_set_on_stream_close_callback (
      callbacks, on_stream_close_callback);

  nghttp2_session_callbacks_set_on_header_callback (callbacks,
                                                   on_header_callback);

  nghttp2_session_callbacks_set_on_begin_headers_callback (
      callbacks, on_begin_headers_callback);

  rv = nghttp2_session_server_new (&h2->session, callbacks, connection);
  if (rv != 0)
  {
    return rv;
  }

  nghttp2_session_callbacks_del (callbacks);
  return 0;
}


/* Send HTTP/2 server connection preface, which includes 24 bytes
   magic octets and SETTINGS frame */
static int
http2_send_server_connection_preface(struct http2_conn *h2,
                                const nghttp2_settings_entry *iv, size_t niv)
{
  int rv;
  ENTER();

  rv = nghttp2_submit_settings(h2->session, NGHTTP2_FLAG_NONE, iv, niv);
  if (rv != 0)
  {
    ENTER("Fatal error: %s", nghttp2_strerror(rv));
    return -1;
  }
  return 0;
}


/* Serialize the frame and send (or buffer) the data to
   bufferevent. */
static int
http2_session_send(struct http2_conn *h2)
{
  ENTER();
  int rv;
  rv = nghttp2_session_send(h2->session);
  if (rv != 0) {
    ENTER("Fatal error: %s", nghttp2_strerror(rv));
    return -1;
  }
  return 0;
}

/* Read the data in the bufferevent and feed them into nghttp2 library
   function. Invocation of nghttp2_session_mem_recv() may make
   additional pending frames, so call session_send() at the end of the
   function. */
static int
http2_session_recv(struct MHD_Connection *connection)
{
  ENTER();
  ssize_t readlen;

  mhd_assert(connection);
  if (0 == connection->read_buffer_offset)
    return -1;

  struct http2_conn *h2 = connection->h2;
  size_t datalen = connection->read_buffer_offset;
  unsigned char *data = connection->read_buffer;
  mhd_assert(data);

  mhd_assert(datalen <= connection->read_buffer_size);
  mhd_assert(datalen <= connection->read_buffer_offset);

  connection->read_buffer += datalen;
  connection->read_buffer_size -= datalen;
  connection->read_buffer_offset -= datalen;

  for (int i = 0; i < datalen; i++) {
    printf("%02X ", data[i]);
  } printf("\n");

  mhd_assert(h2 && h2->session);
  readlen = nghttp2_session_mem_recv (h2->session, data, datalen);
  if (readlen < 0) {
    warnx("Fatal error: %s", nghttp2_strerror((int)readlen));
    return -1;
  }
  if (http2_session_send(h2) != 0) {
    return -1;
  }
  return 0;
}



/**
 * Delete HTTP2 structures.
 *
 * @param connection connection to handle
 */
void
http2_session_delete (struct MHD_Connection *connection)
{
  struct http2_conn *h2 = connection->h2;
  struct http2_stream_data *stream_data;

  nghttp2_session_del (h2->session);

  for (stream_data = h2->head.next; stream_data; )
  {
      struct http2_stream_data *next = stream_data->next;
      delete_http2_stream_data (stream_data);
      stream_data = next;
  }
  free (h2);
}




/**
 * Initialize HTTP2 structures.
 *
 * @param connection connection to handle
 * @return #MHD_YES if no error
 *         #MHD_NO otherwise
 */
int
MHD_http2_session_init (struct MHD_Connection *connection)
{
  int rv;
  connection->h2 = malloc (sizeof (struct http2_conn));
  if (connection->h2 == NULL)
  {
    return MHD_NO;
  }
  memset (connection->h2, 0, sizeof (struct http2_conn));

  rv = http2_init_session (connection);
  if (rv != 0)
  {
    return MHD_NO;
  }
  return MHD_YES;
}


/**
 * Send HTTP/2 preface.
 *
 * @param connection connection to handle
 * @param iv http2 settings array
 * @param niv number of entries
 */
int
MHD_http2_send_preface (struct MHD_Connection *connection,
                        const nghttp2_settings_entry *iv, size_t niv)
{
  struct http2_conn *h2 = connection->h2;
  if (http2_send_server_connection_preface (h2, iv, niv) != 0 ||
      http2_session_send (h2) != 0)
  {
    http2_session_delete (connection);
    return MHD_NO;
  }
  return MHD_YES;
}


/**
 * There is data to be read off a socket.
 *
 * @param connection connection to handle
 */
void
http2_handle_read (struct MHD_Connection *connection)
{
  ENTER();
}


/**
 * Handle writes to sockets.
 *
 * @param connection connection to handle
 */
void
http2_handle_write (struct MHD_Connection *connection)
{
  ENTER();
  struct http2_conn *h2 = connection->h2;
  if (nghttp2_session_want_read(h2->session) == 0 &&
      nghttp2_session_want_write(h2->session) == 0)
  {
    http2_session_delete(connection);
    return;
  }
  if (http2_session_send(h2) != 0)
  {
    http2_session_delete(connection);
    return;
  }
}


/**
 * This function was created to handle per-connection processing that
 * has to happen even if the socket cannot be read or written to.
 * @remark To be called only from thread that process connection's
 * recv(), send() and response.
 *
 * @param connection connection to handle
 * @return #MHD_YES if we should continue to process the
 *         connection (not dead yet), #MHD_NO if it died
 */
int
http2_handle_idle (struct MHD_Connection *connection)
{
  struct MHD_Daemon *daemon = connection->daemon;
  char *line;
  size_t line_len;
  int ret;
  ENTER();

  connection->in_idle = true;
  while (! connection->suspended)
    {
#ifdef HTTPS_SUPPORT
      if (MHD_TLS_CONN_NO_TLS != connection->tls_state)
        { /* HTTPS connection. */
          if ((MHD_TLS_CONN_INIT <= connection->tls_state) &&
              (MHD_TLS_CONN_CONNECTED > connection->tls_state))
            break;
        }
#endif /* HTTPS_SUPPORT */
#if DEBUG_STATES
      MHD_DLOG (daemon,
                _("In function %s handling connection at state: %s\n"),
                __FUNCTION__,
                MHD_state_to_string (connection->state));
#endif
    http2_session_recv (connection);
    break;
  }
}


/**
 * Set HTTP/2 read/idle/write callbacks for this connection.
 * Handle data from/to socket.
 *
 * @param connection connection to initialize
 */
void
MHD_set_http2_callbacks (struct MHD_Connection *connection)
{
  connection->read_cls = &http2_handle_read;
  connection->idle_cls = &http2_handle_idle;
  connection->write_cls = &http2_handle_write;
}

#endif /* HTTP2_SUPPORT */

/* end of connection_http2.c */