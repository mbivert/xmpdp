#include <libmpd/libmpd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

#include "bool.h"
#include "xalloc.h"

#define DEF_HOST "127.0.0.1"
#define DEF_PORT        6600

#define WIDTH  300
#define HEIGHT 100

#define FN "7x13"

#define ESC    9
#define SPACE 65

typedef struct {
   xcb_connection_t     *conn;
   xcb_screen_t       *screen;
   xcb_window_t           win;
   MpdObj                 *mo;
   int                     x1;
   int                     y1;
   char                 *host;
   int                   port;
} xmpd_t;

/* Verbose output */
static bool verb = true;
/* Just do a dump of available song informations */
static bool dump = true;

static void mconnect (MpdObj **, char *, int);

static void xconnect (xcb_connection_t **,
                      const xcb_setup_t **,
                      xcb_window_t *,
                      xcb_screen_iterator_t *,
                      xcb_screen_t **,
                      uint32_t *,
                      int *,
                      int, int,
                      int, int,
                      int);

static void textdump (mpd_Song *);

static xcb_gc_t get_font_gc (xcb_connection_t *,
                             xcb_screen_t *,
                             xcb_window_t,
                             const char *);

static void draw_text (xcb_connection_t *,
                       xcb_screen_t *,
                       xcb_window_t,
                       int16_t,
                       int16_t,
                       const char *);

static void test_cookie (xcb_void_cookie_t,
                         xcb_connection_t *,
                         char *err);
#if 0
static void *update_text (xcb_connection_t *,
                          xcb_screen_t *,
                          xcb_window_t,
                          int, int,
                          MpdObj *,
                          char *,
                          int);
#endif
static void *update_text (void *);

static void
mconnect (MpdObj **mo, char *host, int port) {
   *mo = mpd_new (host, port, NULL);
   if (mpd_connect (*mo) != MPD_OK) {
      mpd_free (*mo);
      if (verb)
         fprintf (stderr, "Error: connexion to mpd fail.\n");
      exit (EXIT_FAILURE);
   }
   else if (verb)
      printf ("Connected to mpd on %s at port %d.\n", host, port);
}

static void
xconnect (xcb_connection_t ** conn,
          const xcb_setup_t      **setup,
          xcb_drawable_t            *win,
          xcb_screen_iterator_t    *iter,
          xcb_screen_t          **screen,
          uint32_t                 *mask,
          int                *screen_num,
          int x, int y,
          int w, int h,
          int border) {
   uint32_t values[2];
   int i;
   xcb_void_cookie_t win_cook;
   xcb_void_cookie_t map_cook;

   *conn = xcb_connect (NULL, screen_num);
   if (xcb_connection_has_error (*conn)) {
      if (verb)
         fprintf (stderr, "Error: Cannot connect to X.\n");
      xcb_disconnect (*conn);
      exit (EXIT_FAILURE);
   }
   if ((*setup = xcb_get_setup (*conn)) == NULL) {
      if (verb)
         fprintf (stderr, "Error: Cannot get setup.\n");
      xcb_disconnect (*conn);
      exit (EXIT_FAILURE);
   }
   *iter = xcb_setup_roots_iterator (*setup);
   for (i = 0; i < *screen_num; i++)
      xcb_screen_next (iter);

   if ((*screen = iter->data) == NULL) {
      if (verb)
         fprintf (stderr, "Error: Cannot open screen.\n");
      xcb_disconnect (*conn);
      exit (EXIT_FAILURE);
   }
#if 0
   *win = (*screen)->root;
   *fg = xcb_generate_id (*conn);
   values[0] = (*screen)->black_pixel;
   values[1] = 0;

   xcb_create_gc (*conn, *fg, *win, *mask, values);
#endif

   *win = xcb_generate_id (*conn);
   *mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
   values[0] = (*screen)->white_pixel;
   values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_RELEASE |
               XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_POINTER_MOTION;

   win_cook = xcb_create_window (*conn,
                                   XCB_COPY_FROM_PARENT,
                                   *win,
                                   (*screen)->root,
                                   x, y,
                                   h, w,
                                   border,
                                   XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                   (*screen)->root_visual,
                                   *mask, values);
   test_cookie (win_cook, *conn, "Cannot create window");
   map_cook = xcb_map_window (*conn, *win);
   test_cookie (map_cook, *conn, "Cannot map window");
   xcb_flush (*conn);
}

void
textdump (mpd_Song *s) {
   printf ("Filename: %s\n", s->file);
   if (s->artist != NULL)
      printf ("Artist: %s\n", s->artist);
   if (s->title != NULL)
      printf ("Title: %s\n", s->title);
   if (s->album != NULL)
      printf ("Album: %s\n", s->album);
   if (s->track != NULL)
      printf ("Track: %s\n", s->track);
   if (s->name != NULL)
      printf ("Name: %s\n", s->name);
   if (s->date != NULL)
      printf ("Date: %s\n", s->date);
   if (s->genre != NULL)
      printf ("Genre: %s\n", s->genre);
   if (s->time != MPD_SONG_NO_TIME)
      printf ("Length: %d(seconds)\n", s->time);
}

static xcb_gc_t
get_font_gc (xcb_connection_t   *conn,
             xcb_screen_t     *screen,
             xcb_window_t         win,
             const char           *fn) {
   xcb_void_cookie_t gc_cookie;
   xcb_gcontext_t gc;
   uint32_t mask;
   uint32_t values[3];
   xcb_font_t font = xcb_generate_id (conn);
   xcb_void_cookie_t fc = xcb_open_font_checked (conn,
                                                 font,
                                                 strlen (fn),
                                                 fn);
   test_cookie (fc, conn, "Cannot open font.");
   gc = xcb_generate_id (conn);
   mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
   values[0] = screen->black_pixel;
   values[1] = screen->white_pixel;
   values[2] = font;
   gc_cookie = xcb_create_gc_checked (conn,
                                      gc,
                                      win,
                                      mask,
                                      values);
   test_cookie (gc_cookie, conn, "Cannot create graphic context");
   fc = xcb_close_font_checked (conn, font);
   test_cookie (fc, conn, "Cannot close font");

   return gc;
}

static void
draw_text (xcb_connection_t   *conn,
           xcb_screen_t     *screen,
           xcb_window_t         win,
           int16_t               x1,
           int16_t               y1,
           const char         *label) {
   xcb_void_cookie_t tc;
   xcb_void_cookie_t gc_cookie;
   xcb_gcontext_t gc = get_font_gc (conn, screen, win, FN);
   tc = xcb_image_text_8_checked (conn,
                                                    strlen (label),
                                                    win,
                                                    gc,
                                                    x1, y1,
                                                    label);
   test_cookie (tc, conn, "Cannot paste text");
   gc_cookie = xcb_free_gc (conn, gc);
   test_cookie (gc_cookie, conn, "Cannot free graphic context");
}

static
void test_cookie (xcb_void_cookie_t  cookie,
                  xcb_connection_t    *conn,
                  char                 *err) {
   xcb_generic_error_t *error = xcb_request_check (conn, cookie);
   if (error != NULL) {
      fprintf (stderr, "Error: %s : %d.\n", err, error->error_code);
      xcb_disconnect (conn);
      exit (EXIT_FAILURE);
   }
}

static void *update_text (void *xmpd) {
   while (true) {
      const xmpd_t *x = (xmpd_t *) xmpd;
      MpdObj *mo = x->mo;
      mpd_Song *song = xmalloc (sizeof (mpd_Song *));
      char *label = NULL;
      mconnect (&mo, x->host, x->port);
      song = mpd_playlist_get_current_song (mo);
      label = xmalloc (strlen (song->file) + 1);
      label = song->file;

      draw_text (x->conn, x->screen, x->win, x->x1, x->y1, label);
      if (dump)
         textdump (mpd_playlist_get_current_song (mo));
      sleep (3);
   }
   return NULL;
}


int
main (void) {
   /* Thread to update context of the text field */
   pthread_t tupdate;
   /* Structure for the thread */
   xmpd_t foo;

   /* mpd's related varibles */
   const int port = DEF_PORT;
   char *host = DEF_HOST;
   MpdObj *mo = NULL;
   mpd_Song *song = NULL;

   /* xcb's related variables */
   xcb_connection_t *conn;
   const xcb_setup_t *setup;
   xcb_screen_iterator_t iter;
   xcb_screen_t *screen;
   xcb_drawable_t win;
   xcb_generic_event_t *ev;
   uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
   int screen_num;

   xconnect (&conn,
         &setup,
         &win,
         &iter,
         &screen,
         &mask,
         &screen_num,
         0, 0, 150, 150, 10);

   mconnect (&mo, host, port);
   song = xmalloc (sizeof (mpd_Song *));
   if (mpd_player_get_state (mo) == MPD_PLAYER_PLAY)
      song = mpd_playlist_get_current_song (mo);

   foo.conn = conn;
   foo.screen = screen;
   foo.win = win;
   foo.mo = mo;
   foo.x1 = 10;
   foo.y1 = HEIGHT - 10;
   foo.host = host;
   foo.port = port;

   pthread_create (&tupdate, NULL, update_text, &foo);
   pthread_join (tupdate, NULL);

   while (xcb_connection_has_error (conn) == 0) {
      if ((ev = xcb_poll_for_event (conn)) != NULL) {
         switch (ev->response_type & ~0x80) {
            case XCB_EXPOSE:
               {
                  draw_text (conn,
                             screen,
                             win,
                             10, HEIGHT - 10,
                             song->file);
                  break;
               }
            case XCB_KEY_RELEASE:
               {
                  xcb_key_release_event_t *kr = (xcb_key_release_event_t *) ev;
                  printf ("Key Pressed : %d\n", kr->detail);
                  switch (kr->detail) {
                     case ESC:
                        {
                           XFREE (ev);
                           xcb_disconnect (conn);
                           mpd_free (mo);
                           return EXIT_SUCCESS;
                           break;
                        }
                     case SPACE:
                        {
                           mconnect (&mo, host, port);
                           song = mpd_playlist_get_current_song (mo);
                           draw_text (conn,
                                      screen,
                                      win,
                                      10, HEIGHT - 10,
                                      song->file);
                           xcb_flush (conn);
                           break;
                        }
                  }
                  XFREE (ev);
                  break;
               }
            default:
               {
                  break;
               }
         }
      }
   }

   /* Should never be */
   return 42;
}
