#include <assert.h>
#include <libmpd/libmpd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

#include "xalloc.h"
#include "bool.h"

/* Default mpd's location */
#define DEF_HOST "127.0.0.1"
#define DEF_PORT        6600

#define W 500
#define H 100

#define TITLE "Xmpdp"

/* Default font name */
#define DEF_FN "7x13"

/* Default time to wait before each screen update */
#define DEF_UP_TIME 1


/*
 * A structure to handle window and mpd variables
 */
typedef struct {
   /* XCB relatives variables */
   xcb_connection_t           *conn;
   xcb_screen_t             *screen;
   const xcb_setup_t         *setup;
   xcb_drawable_t               win;
   xcb_screen_iterator_t       iter;
   uint32_t                    mask;
   int                   screen_num;
   /* The mpd object from which retrieve informations */
   MpdObj                       *mo;
   /* Size of the window */
   unsigned int                   h;
   unsigned int                   w;
   /* Position of the window */
   unsigned int                   x;
   unsigned int                   y;
   /* Position of the text field */
   int                           x1;
   int                           y1;
   int                       border;
   char                       *host;
   int                         port;
} xmpd_t;

/*
 * Init the xmpd_t structure.
 */
static void xmpd_init (xmpd_t *);

/*
 * Connect to mpd, beeing verbose or not.
 */
static void mconnect (xmpd_t *, bool);

/*
 * Connect to X server via xcb.
 */
static void xconnect (xmpd_t *);

/*
 * Test the validity of a cookie.
 * Quit if invalid.
 */
static void test_cookie (xcb_void_cookie_t, xmpd_t *, const char *);

/*
 * Free ressources and quit application with error message.
 */
static void fail (xmpd_t *, const char *);

/*
 * Get a font graphic context with a specific font.
 */
static xcb_gc_t get_font_gc (xmpd_t *, const char *);

static void draw_text (xmpd_t *, const char *);

/* Must have this prototype because used in thread */
static void *update_text (void *);

/*
 * Disconnect from mpd.
 */
static void mdisconnect (xmpd_t *);

/*
 * Draw a white rectangle on the screen.
 */
static void update_screen (xmpd_t *);

static void
xmpd_init (xmpd_t *xmpd) {
   xmpd->conn = NULL;
   xmpd->screen = NULL;
   xmpd->setup = NULL;
   xmpd->mo = NULL;
   xmpd->host = DEF_HOST;
   xmpd->port = DEF_PORT;
   xmpd->x = 0;
   xmpd->y = 0;
   xmpd->x1 = 10;
   xmpd->y1 = 10;
   xmpd->h = H;
   xmpd->w = W;
   xmpd->border = 10;
   xmpd->mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
}

static void
mconnect (xmpd_t *xmpd, bool b) {
   assert (xmpd != NULL);
   xmpd->mo = mpd_new (xmpd->host, xmpd->port, NULL);
   if (mpd_connect (xmpd->mo) != MPD_OK) {
      mdisconnect (xmpd);
      fprintf (stderr, "Error: Cannot connect to mpd (%s:%d).\n",
            xmpd->host, xmpd->port);
      exit (EXIT_FAILURE);
   }
   if (b)
      printf ("Connected to mpd on %s:%d.\n", xmpd->host, xmpd->port);
}

static void
xconnect (xmpd_t *xmpd) {
   uint32_t values[2];
   int i;
   xcb_void_cookie_t w_cook;
   xcb_void_cookie_t m_cook;

   assert (xmpd != NULL && xmpd->mo != NULL);
   /* Connect X server */
   xmpd->conn = xcb_connect (NULL, &(xmpd->screen_num));
   if (xcb_connection_has_error (xmpd->conn))
      fail (xmpd, "Cannot connect to X server");
   /* Get setup */
   if ((xmpd->setup = xcb_get_setup (xmpd->conn)) == NULL)
      fail (xmpd, "Cannot get setup");
   xmpd->iter = xcb_setup_roots_iterator (xmpd->setup);
   for (i = 0; i < xmpd->screen_num; i++)
      xcb_screen_next (&(xmpd->iter));
   /* Open the screen */
   if ((xmpd->screen = (xmpd->iter).data) == NULL)
     fail (xmpd, "Cannot open screen");

   xmpd->win = xcb_generate_id (xmpd->conn);
   xmpd->mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
   values[0] = xmpd->screen->white_pixel;
   values[1] = XCB_EVENT_MASK_EXPOSURE;
   /* Open the window */
   w_cook = xcb_create_window (xmpd->conn,
                               XCB_COPY_FROM_PARENT,
                               xmpd->win,
                               xmpd->screen->root,
                               xmpd->x, xmpd->y,
                               xmpd->w, xmpd->h,
                               xmpd->border,
                               XCB_WINDOW_CLASS_INPUT_OUTPUT,
                               xmpd->screen->root_visual,
                               xmpd->mask, values);
   test_cookie (w_cook, xmpd, "Cannot create window");
   xcb_change_property (xmpd->conn,
                        XCB_PROP_MODE_REPLACE,
                        xmpd->win,
                        WM_NAME,
                        STRING,
                        8,
                        strlen (TITLE),
                        TITLE);
  m_cook = xcb_map_window (xmpd->conn, xmpd->win);
   test_cookie (m_cook, xmpd, "Cannot map window");
   if (xcb_flush (xmpd->conn) <= 0) {
      fprintf (stderr, "Error: Cannot flush.\n");
      xcb_disconnect (xmpd->conn);
      mdisconnect (xmpd);
   }
}

static void
test_cookie (xcb_void_cookie_t c, xmpd_t *xmpd, const char *err) {
   xcb_generic_error_t *error = xcb_request_check (xmpd->conn, c);
   if (error != NULL) {
      fprintf (stderr, "Error: %s (%d).\n", err, error->error_code);
      xcb_disconnect (xmpd->conn);
      mdisconnect (xmpd);
      exit (EXIT_FAILURE);
   }
}

static void
fail (xmpd_t *xmpd, const char *err) {
   fprintf (stderr, "Error: %s.\n", err);
   xcb_disconnect (xmpd->conn);
   mdisconnect (xmpd);
   exit (EXIT_FAILURE);
}

static xcb_gc_t
get_font_gc (xmpd_t *xmpd, const char *fn) {
   xcb_void_cookie_t gc_cook;
   xcb_gcontext_t gc;
   uint32_t mask;
   uint32_t values[3];
   xcb_font_t font = xcb_generate_id (xmpd->conn);
   xcb_void_cookie_t f_cook = xcb_open_font_checked (xmpd->conn,
                                                     font,
                                                     strlen (fn),
                                                     fn);
   test_cookie (f_cook, xmpd, "Cannot open font");
   gc = xcb_generate_id (xmpd->conn);
   mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
   values[0] = xmpd->screen->black_pixel;
   values[1] = xmpd->screen->white_pixel;
   values[2] = font;
   gc_cook = xcb_create_gc_checked (xmpd->conn,
                                    gc,
                                    xmpd->win,
                                    mask,
                                    values);
   test_cookie (gc_cook, xmpd, "Cannot create graphic context (font)");
   f_cook = xcb_close_font_checked (xmpd->conn, font);
   test_cookie (f_cook, xmpd, "Cannot close font");
   return gc;
}

static void
draw_text (xmpd_t *xmpd, const char *s) {
   xcb_void_cookie_t t_cook;
   xcb_void_cookie_t gc_cook;
   xcb_gcontext_t gc = get_font_gc (xmpd, DEF_FN);
   t_cook = xcb_image_text_8_checked (xmpd->conn,
                                      strlen (s),
                                      xmpd->win,
                                      gc,
                                      xmpd->x1, xmpd->y1,
                                      s);
   test_cookie (t_cook, xmpd, "Cannot paste text");
   gc_cook = xcb_free_gc (xmpd->conn, gc);
   test_cookie (gc_cook, xmpd, "Cannot free graphic context (font)");
}

static void *
update_text (void *xmpd) {
   mdisconnect (xmpd);
   while (true) {
      char *label = NULL;
      xmpd_t *x = (xmpd_t *) xmpd;
      mpd_Song *song = xmalloc (sizeof *song);

      mconnect (x, false);
      update_screen (x);
      song = mpd_playlist_get_current_song (x->mo);
      label = xmalloc (strlen (song->file) + 1);
      strcpy (label, song->file);

      draw_text (x, label);
      mdisconnect (x);
      XFREE (label);
      xcb_flush (x->conn);
      sleep (DEF_UP_TIME);
   }
   return NULL;
}

static void
mdisconnect (xmpd_t *xmpd) {
   assert (xmpd != NULL);
   mpd_free (xmpd->mo);
}

static void
update_screen (xmpd_t *xmpd) {
   uint32_t values[2];
   /* Needed to get windows size */
   xcb_get_geometry_cookie_t g_cook;
   xcb_get_geometry_reply_t *g = xmalloc (sizeof *g);
   xcb_rectangle_t *rect = xmalloc (sizeof *rect);
   xcb_gcontext_t fg;

   g_cook = xcb_get_geometry (xmpd->conn, xmpd->win);
   g = xcb_get_geometry_reply (xmpd->conn, g_cook, NULL);

   /* Window has been closed */
   if (g == NULL) {
      mdisconnect (xmpd);
      xcb_disconnect (xmpd->conn);
      exit (0);
   }

   rect->x = 0; rect->y = 0;
   rect->width = g->width; rect->height = g->height;

   fg = xcb_generate_id (xmpd->conn);

   xmpd->mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;

   values[0] = xmpd->screen->white_pixel;
   values[1] = 0;

   xcb_create_gc (xmpd->conn, fg, xmpd->win, xmpd->mask, values);
   xcb_poly_fill_rectangle (xmpd->conn, xmpd->win, fg, 1, rect);
   XFREE (rect);
}

int
main (void) {
   /* Thread to update content of text field */
   pthread_t t_update;
   /* "Meta" structure */
   xmpd_t *xmpd = xmalloc (sizeof *xmpd);
   xmpd_init (xmpd);
   mconnect (xmpd, true);
   xconnect (xmpd);

   pthread_create (&t_update, NULL, update_text, xmpd);
   pthread_join (t_update, NULL);

   return EXIT_SUCCESS;
}
