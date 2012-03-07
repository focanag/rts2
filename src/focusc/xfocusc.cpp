/* 
 * Display RTS2 (and FITS images) in separate window.
 * Copyright (C) 2004-2007 Petr Kubanek <petr@kubanek.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "focusclient.h"
#include "utilsfunc.h"

#include <curses.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/keysymdef.h>

#include <math.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <stdlib.h>

#include <limits.h>

#define OPT_DISPLAY      OPT_LOCAL + 20
#define OPT_STARS        OPT_LOCAL + 21
#define OPT_SAVE         OPT_LOCAL + 22
#define OPT_CHANGE       OPT_LOCAL + 23

using namespace rts2core;

/**
 * X-focusing client class.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 */
class XFocusClient:public FocusClient
{
	private:
		XColor rgb[260];		 // <= 255 - images, 256 - red line
		Colormap colormap;
		char * displayName;

		// X11 stuff
		Display * display;
		Visual * visual;
		int depth;

		int crossType;
		int starsType;

		// initially in arcsec, but converted (and used) in degrees
		double changeVal;

		virtual FocusCameraClient * createFocCamera (rts2core::Connection * conn);
	protected:
		/**
		 * Add XWin connection socket, obtained by ConnectionNumber macro.
		 */
		virtual void addSelectSocks ();

		/**
		 * Query and process possible XWin event from top of XWin event loop.
		 */
		virtual void selectSuccess ();
	public:
		XFocusClient (int argc, char **argv);
		virtual ~ XFocusClient (void);

		virtual int processOption (int in_opt);

		virtual int init ();

		virtual void usage ();
		virtual void help ();

		Display *getDisplay ()
		{
			return display;
		}
		int getDepth ()
		{
			return depth;
		}
		Visual * getVisual ()
		{
			return visual;
		}

		XColor * getRGB (int i)
		{
			return &rgb[i];
		}
		Colormap * getColormap ()
		{
			return &colormap;
		}
		int getStarsType ()
		{
			return starsType;
		}
		double zoom;
		bool GoNine;
};

class XFocusClientCamera:public FocusCameraClient
{
	public:
		XFocusClientCamera (rts2core::Connection * in_connection, double in_change_val, XFocusClient * in_master);
		virtual ~ XFocusClientCamera (void);

		virtual void postEvent (rts2core::Event * event);

		void setCrossType (int in_crossType);

		// process possible change requests
		virtual void idle ();
	protected:
		virtual void cameraImageReady (rts2image::Image * image);

		double classical_median (void *q, int16_t dataType, int n, double *sigma, double sf = 0.6745);
		virtual void printFWHMTable ();
	private:
		XFocusClient * master;

		// X11 stuff
		Window window;
		GC gc;
		XGCValues gvc;
		Pixmap pixmap;
		XImage *ximage;
		XSetWindowAttributes xswa;

		int windowHeight;
		int windowWidth;

		int pixmapHeight;
		int pixmapWidth;

		void buildWindow ();
		void rebuildWindow ();
		void redraw ();
		void drawCenterCross (int xc, int yc);
		void drawCross1 ();
		void drawCross2 ();
		void drawCross3 ();
		void drawStars (rts2image::Image * image);
		void printInfo ();
		void printMouse ();
		void redrawMouse ();
		// thread entry function..
		void XeventLoop ();

		int crossType;
		int lastImage;

		long lastX;
		long lastY;
		long lastSizeX;
		long lastSizeY;
		int binningsX;
		int binningsY;

		int mouseX;
		int mouseY;

		int buttonX;
		int buttonY;
		struct timeval buttonImageTime;

		struct timeval exposureStart;

		double change_val;		 // change value in degrees

		int mouseTelChange_x, mouseTelChange_y;
};

int cmpbyte (const void *a, const void *b)
{
#define A (*((uint8_t*) a))
#define B (*((uint8_t*) b))
	return A > B ? 1 : -1;
#undef A
#undef B
}

int cmpuint16_t (const void *a, const void *b)
{
#define A (*((uint16_t*) a))
#define B (*((uint16_t*) b))
	return A > B ? 1 : -1;
#undef A
#undef B
}

double XFocusClientCamera::classical_median (void *q, int16_t dataType, int n, double *sigma, double sf)
{
	int i;
	double M,S;
	char *f;

	switch (dataType)
	{
		case RTS2_DATA_BYTE:
			f = (char *) new uint8_t[n];
			memcpy (f, q, n * sizeof (uint8_t));
			qsort (f, n, sizeof (uint8_t), cmpbyte);
#define F ((char *) f)
			if (n % 2)
				M = F[n / 2];
			else
		 	 	M = (F[n / 2] + F[n / 2 + 1]) / 2;
#undef F
			break;
		case RTS2_DATA_USHORT:
			f = (char *) new uint16_t[n];
			memcpy (f, q, n * sizeof (uint16_t));
			qsort (f, n, sizeof (uint16_t), cmpuint16_t);
#define F ((uint16_t *) f)
			if (n % 2)
				M = F[n / 2];
			else
		 	 	M = (F[n / 2] + F[n / 2 + 1]) / 2;
#undef F
			break;
		default:
			throw rts2core::Error ("unsuported data type");
	}

	if (sigma)
	{
		switch (dataType)
		{
			case RTS2_DATA_BYTE:
#define F ((uint8_t *) f)
				for (i = 0; i < n; i++)
					F[i] = (uint8_t) fabs(F[i] - M);
				qsort (f, n, sizeof(uint8_t), cmpbyte);
				if (n % 2)
					S = F[n / 2];
				else
					S = (F[n / 2] + F[n / 2 + 1]) / 2;

#undef F
				break;

			case RTS2_DATA_USHORT:
#define F ((uint16_t *) f)
				for (i = 0; i < n; i++)
					F[i] = (uint16_t) fabs(F[i] - M);
				qsort (f, n, sizeof(uint16_t), cmpuint16_t);
				if (n % 2)
					S = F[n / 2];
				else
					S = (F[n / 2] + F[n / 2 + 1]) / 2;

#undef F
				break;
		}
		*sigma = S * sf;
	}

	delete[] f;

	return M;
}

XFocusClientCamera::XFocusClientCamera (rts2core::Connection * in_connection, double in_change_val, XFocusClient * in_master):FocusCameraClient (in_connection, in_master)
{
	master = in_master;

	window = 0L;
	pixmap = 0L;
	ximage = NULL;
	gc = 0L;
	windowHeight = 800;
	windowWidth = 800;

	pixmapHeight = windowHeight;
	pixmapWidth = windowWidth;

	lastImage = 0;

	mouseX = mouseY = -1;
	buttonX = buttonY = -1;

	timerclear (&buttonImageTime);
	timerclear (&exposureStart);

	change_val = in_change_val;

	mouseTelChange_x = INT_MAX;
	mouseTelChange_y = INT_MAX;
}

XFocusClientCamera::~XFocusClientCamera (void)
{
}

void XFocusClientCamera::buildWindow ()
{
	XTextProperty window_title;
	char *cameraName;

	window = XCreateWindow (master->getDisplay (), DefaultRootWindow (master->getDisplay ()), 0, 0, 100, 100, 0, master->getDepth (), InputOutput, 	master->getVisual (), 0, &xswa);
	pixmap = XCreatePixmap (master->getDisplay (), window, windowWidth, windowHeight, master->getDepth ());

	gc = XCreateGC (master->getDisplay (), pixmap, 0, &gvc);
	XSelectInput (master->getDisplay (), window, KeyPressMask | ButtonPressMask | ExposureMask | PointerMotionMask);
	XMapRaised (master->getDisplay (), window);

	cameraName = new char[strlen (connection->getName ()) + 1];
	strcpy (cameraName, connection->getName ());

	XStringListToTextProperty (&cameraName, 1, &window_title);
	XSetWMName (master->getDisplay (), window, &window_title);
}

// called to rebuild window on the screen
void XFocusClientCamera::rebuildWindow ()
{
	if (gc)
		XFreeGC (master->getDisplay (), gc);
	if (pixmap)
		XFreePixmap (master->getDisplay (), pixmap);
	pixmap = XCreatePixmap (master->getDisplay (), window, windowWidth, windowHeight, master->getDepth ());

	if (ximage)
	{
		XDestroyImage (ximage);
		ximage = NULL;
	}
	gc = XCreateGC (master->getDisplay (), pixmap, 0, &gvc);
}

void XFocusClientCamera::drawCenterCross (int xc, int yc)
{
	// draw cross at center
	XDrawLine (master->getDisplay (), pixmap, gc, xc - 10, yc, xc - 2, yc);
	XDrawLine (master->getDisplay (), pixmap, gc, xc + 10, yc, xc + 2, yc);
	XDrawLine (master->getDisplay (), pixmap, gc, xc, yc - 10, xc, yc - 2);
	XDrawLine (master->getDisplay (), pixmap, gc, xc, yc + 10, xc, yc + 2);
}

void XFocusClientCamera::drawCross1 ()
{
	XSetForeground (master->getDisplay (), gc, master->getRGB (256)->pixel);
	int rectNum;
	int i;
	int xc, yc;
	XRectangle *rectangles;

	rectNum = (pixmapWidth / 40 > pixmapHeight / 40) ? pixmapHeight / 40 : pixmapWidth / 40;
	rectangles = new XRectangle[rectNum];

	XRectangle *rect = rectangles;

	xc = pixmapWidth / 2;
	yc = pixmapHeight / 2;

	drawCenterCross (xc, yc);

	for (i = 0; i < rectNum;)
	{
		i++;
		xc -= 20;
		yc -= 20;
		rect->x = xc;
		rect->y = yc;
		rect->width = i * 40;
		rect->height = i * 40;
		rect++;
	}
	XDrawRectangles (master->getDisplay (), pixmap, gc, rectangles, rectNum);

	delete[]rectangles;
}

void XFocusClientCamera::drawCross2 ()
{
	XSetForeground (master->getDisplay (), gc, master->getRGB (256)->pixel);
	int arcNum;
	int i;
	int xc, yc;
	XArc *arcs;

	arcNum = (pixmapWidth / 40 > pixmapHeight / 40) ? pixmapHeight / 40 : pixmapWidth / 40;
	arcs = new XArc[arcNum];

	XArc *arc = arcs;

	xc = pixmapWidth / 2;
	yc = pixmapHeight / 2;

	drawCenterCross (xc, yc);

	for (i = 0; i < arcNum; )
	{
		i++;
		xc -= 20;
		yc -= 20;
		arc->x = xc;
		arc->y = yc;
		arc->width = i * 40;
		arc->height = i * 40;
		arc->angle1 = 0;
		arc->angle2 = 23040;
		arc++;
	}
	XDrawArcs (master->getDisplay (), pixmap, gc, arcs, arcNum);

	delete[]arcs;
}

void XFocusClientCamera::drawCross3 ()
{
	static XPoint points[5];
	int xc, yc;

	XSetForeground (master->getDisplay (), gc, master->getRGB (256)->pixel);
	// draw lines on surrounding
	int w = pixmapWidth / 7;
	int h = pixmapHeight / 7;
	XDrawLine (master->getDisplay (), pixmap, gc, 0, 0, w, h);
	XDrawLine (master->getDisplay (), pixmap, gc, pixmapWidth, 0, pixmapWidth - w, h);
	XDrawLine (master->getDisplay (), pixmap, gc, 0, pixmapHeight, w, pixmapHeight - h);
	XDrawLine (master->getDisplay (), pixmap, gc, pixmapWidth, pixmapHeight, pixmapWidth - w, pixmapHeight - h);
	XDrawRectangle (master->getDisplay (), pixmap, gc, pixmapWidth / 4, pixmapHeight / 4, pixmapWidth / 2, pixmapHeight / 2);
	// draw center..
	xc = pixmapWidth / 2;
	yc = pixmapHeight / 2;
	w = pixmapWidth / 10;
	h = pixmapHeight / 10;
	points[0].x = xc - w;
	points[0].y = yc;

	points[1].x = xc;
	points[1].y = yc + h;

	points[2].x = xc + w;
	points[2].y = yc;

	points[3].x = xc;
	points[3].y = yc - h;

	points[4].x = xc - w;
	points[4].y = yc;

	XDrawLines (master->getDisplay (), pixmap, gc, points, 5, CoordModeOrigin);
	XDrawLine (master->getDisplay (), pixmap, gc, xc, yc - pixmapHeight / 15, xc, yc + pixmapHeight / 15);
}

void XFocusClientCamera::drawStars (rts2image::Image * image)
{
	struct rts2image::stardata *sr;
	if (!image)
		return;
	sr = image->sexResults;
	for (int i = 0; i < image->sexResultNum; i++, sr++)
	{
		XDrawArc (master->getDisplay (), pixmap, gc, (int) sr->X - 10, (int) sr->Y - 10, 20, 20, 0, 23040);
	}
}

void XFocusClientCamera::printInfo ()
{
	XSetBackground (master->getDisplay (), gc, master->getRGB (0)->pixel);
	std::ostringstream _os;
	_os << "L: " << low << " M: " << med << " H: " << hig << " Min: " << min << " Max: " << max;
	XDrawImageString (master->getDisplay (), pixmap, gc, pixmapWidth / 2 - 100, 20, _os.str ().c_str (), _os.str ().length ());
	std::ostringstream _os1;
	_os1.precision (2);
	_os1 << "avg " << average;
	XDrawImageString (master->getDisplay (), pixmap, gc, pixmapWidth / 2 - 50, 32, _os1.str ().c_str (), _os1.str ().length ());

	if (lastImage)
	{
	  	std::ostringstream _os2;
		_os2 << "[" << lastX << "," << lastY << ":" << lastSizeX << "," << lastSizeY << "] binn: " << binningsX << ":" << binningsY << " exposureTime: " << getConnection ()->getValue ("exposure")->getValueDouble ();
		XDrawImageString (master->getDisplay (), pixmap, gc, pixmapWidth / 2 - 150, pixmapHeight - 20, _os2.str ().c_str (), _os2.str ().length ());
	}
}

void XFocusClientCamera::printMouse ()
{
	char stringBuf[20];
	int len;
	len = snprintf (stringBuf, 20, "%04i %04i", mouseX, mouseY);
	XSetBackground (master->getDisplay (), gc, master->getRGB (0)->pixel);
	XSetForeground (master->getDisplay (), gc, master->getRGB (256)->pixel);
	XDrawImageString (master->getDisplay (), pixmap, gc, 30, 30, stringBuf, len);
	if (buttonX >= 0 && buttonY >= 0)
		drawCenterCross (buttonX, buttonY);
}

void XFocusClientCamera::redrawMouse ()
{
	XClearArea (master->getDisplay (), window, 0, 0, 200, 40, False);
	XClearArea (master->getDisplay (), window, buttonX - 10, buttonY - 10, 20, 20, False);
}

void XFocusClientCamera::redraw ()
{
	// do some line-drawing etc..
	switch (crossType)
	{
		case 1:
			drawCross1 ();
			break;
		case 2:
			drawCross2 ();
			break;
		case 3:
			drawCross3 ();
			break;
	}
	if (crossType > 0)
		printInfo ();
	// draw plots over stars..
	drawStars (getActualImage ());

	printMouse ();

	xswa.colormap = *(master->getColormap ());
	xswa.background_pixmap = pixmap;

	XChangeWindowAttributes (master->getDisplay (), window, CWColormap | CWBackPixmap, &xswa);

	XClearWindow (master->getDisplay (), window);
}

void XFocusClientCamera::XeventLoop ()
{
	XEvent event;
	KeySym ks;
	struct ln_equ_posn change;

	if (XCheckWindowEvent (master->getDisplay (), window, KeyPressMask | ButtonPressMask | ExposureMask | PointerMotionMask, &event))
	{
		switch (event.type)
		{
			case Expose:
				if (pixmap && gc)
					redraw ();
				break;
			case KeyPress:
				ks = XLookupKeysym ((XKeyEvent *) & event, 0);
				switch (ks)
				{
					case XK_1:
						queCommand (new rts2core::CommandChangeValue (this, "binning", '=', 0));
						break;
					case XK_2:
						queCommand (new rts2core::CommandChangeValue (this, "binning", '=', 1));
						break;
					case XK_3:
						queCommand (new rts2core::CommandChangeValue (this, "binning", '=', 2));
						break;
					case XK_9:
						master->GoNine = !master->GoNine;
						break;
					case XK_e:
						queCommand (new rts2core::CommandChangeValue (this, "exposure", '+', 1));
						break;
					case XK_d:
						queCommand (new rts2core::CommandChangeValue (this, "exposure", '-', 1));
						break;
					case XK_w:
						queCommand (new rts2core::CommandChangeValue (this, "exposure", '+', 0.1));
						break;
					case XK_s:
						queCommand (new rts2core::CommandChangeValue (this, "exposure", '-', 0.1));
						break;
					case XK_q:
						queCommand (new rts2core::CommandChangeValue (this, "exposure", '+', 0.01));
						break;
					case XK_a:
						queCommand (new rts2core::CommandChangeValue (this, "exposure", '-', 0.01));
						break;
					case XK_f:
						connection->queCommand (new rts2core::Command (master, "box 0 -1 -1 -1 -1"));
						break;
					case XK_c:
						connection->queCommand (new rts2core::Command (master, "center 0"));
						break;
					case XK_p:
						master->postEvent (new rts2core::Event (EVENT_INTEGRATE_START));
						break;
					case XK_o:
						master->postEvent (new rts2core::Event (EVENT_INTEGRATE_STOP));
						break;
					case XK_y:
						setSaveImage (1);
						break;
					case XK_u:
						setSaveImage (0 || exe);
						break;
						// change stuff
					case XK_h:
					case XK_Left:
						change.ra = -1 * change_val;
						change.dec = 0;
						master->postEvent (new rts2core::Event (EVENT_MOUNT_CHANGE, (void *) &change));
						break;
					case XK_j:
					case XK_Down:
						change.ra = 0;
						change.dec = -1 * change_val;
						master->postEvent (new rts2core::Event (EVENT_MOUNT_CHANGE, (void *) &change));
						break;
					case XK_k:
					case XK_Up:
						change.ra = 0;
						change.dec = change_val;
						master->postEvent (new rts2core::Event (EVENT_MOUNT_CHANGE, (void *) &change));
						break;
					case XK_l:
					case XK_Right:
						change.ra = change_val;
						change.dec = 0;
						master->postEvent (new rts2core::Event (EVENT_MOUNT_CHANGE, (void *) &change));
						break;
					case XK_Escape:
						master->endRunLoop ();
						break;
					case XK_plus:
					case XK_KP_Add:
						master->zoom = master->zoom * 2.0;
						break;
					case XK_minus:
					case XK_KP_Subtract:
						master->zoom = master->zoom / 2.0;
						break;
					default:
						break;
				}
				break;
			case MotionNotify:
				mouseX = ((XMotionEvent *) & event)->x;
				mouseY = ((XMotionEvent *) & event)->y;

				printMouse ();
				redrawMouse ();
				XFlush (master->getDisplay ());
				break;
			case ButtonPress:
				mouseX = ((XButtonPressedEvent *) & event)->x;
				mouseY = ((XButtonPressedEvent *) & event)->y;
				if (((XButtonPressedEvent *) & event)->state & ControlMask)
				{
					if (buttonX < 0 && buttonY < 0)
					{
						buttonX = mouseX;
						buttonY = mouseY;
						buttonImageTime = exposureStart;
					}
					else
					{
						// calculate distance travelled, print it, pixels / sec travelled, discard button
						timersub (&exposureStart, &buttonImageTime, &exposureStart);
						double del = exposureStart.tv_sec + (double) exposureStart.tv_usec / USEC_SEC;
						double offsetX = buttonX - mouseX;
						double offsetY = buttonY - mouseY;
						if (del > 0)
						{
							printf ("Delay %.4f sec\nX offset: %.1f drift: %.1f pixels/sec\nY offset: %.1f drift: %1.f pixels/sec\n", del, offsetX, offsetX / del, offsetY, offsetY / del);
						}
						else
						{
							printf ("Delay %.4f sec\nX offset: %.1f\nY offset: %.1f\n", del, offsetX, offsetY);
						}
						// clear results
						buttonX = -1;
						buttonY = -1;
						timerclear (&buttonImageTime);
					}
				}
				else
				{
					// move by given direction
					mouseTelChange_x = mouseX;
					mouseTelChange_y = mouseY;
					std::cout << "Change" << std::endl;
				}

				printf ("%i %i\n", mouseX, mouseY);

				printMouse ();
				redrawMouse ();
				break;
		}
	}
}

void XFocusClientCamera::postEvent (rts2core::Event * event)
{
	switch (event->getType ())
	{
		case EVENT_XWIN_SOCK:
			XeventLoop ();
			break;
	}
	FocusCameraClient::postEvent (event);
}

void XFocusClientCamera::printFWHMTable ()
{
	FocusCameraClient::printFWHMTable ();
	if (master->getStarsType ())
		redraw ();
}

void XFocusClientCamera::idle ()
{
	FocusCameraClient::idle ();
}

void XFocusClientCamera::cameraImageReady (rts2image::Image * image)
{
	int i, j;

	if (window == 0L)
		buildWindow ();

	if (ximage && (pixmapWidth < image->getChannelWidth (0) || pixmapHeight < image->getChannelHeight (0)))
	{
		XDestroyImage (ximage);
		ximage = NULL;
	}

	if (master->GoNine)
	{
		XWindowAttributes wa;
		XGetWindowAttributes (master->getDisplay (), DefaultRootWindow (master->getDisplay ()), &wa);
		pixmapWidth = wa.width / 2;
		pixmapHeight = wa.height / 2;

		// if window is too large, set to 1/4 of required size, so we will see nine effect
		if (pixmapWidth > image->getChannelWidth (0) * master->zoom)
			pixmapWidth = (int) ceil (image->getChannelWidth (0) * master->zoom);
		if (pixmapHeight > image->getChannelHeight (0) * master->zoom)
		  	pixmapHeight = (int) ceil (image->getChannelHeight (0) * master->zoom);
	}
	else
	{
		pixmapWidth = (int) ceil (image->getChannelWidth (0) * master->zoom);
		pixmapHeight = (int) ceil (image->getChannelHeight (0) * master->zoom);
	}

	if (pixmapWidth != windowWidth || pixmapHeight != windowHeight)
	{
		windowWidth = pixmapWidth;
		windowHeight = pixmapHeight;
		rebuildWindow ();
	}
	// draw window with image..
	if (!ximage)
	{
		std::cout << "Create ximage " << pixmapWidth << "x" << pixmapHeight << std::endl;
		ximage = XCreateImage (master->getDisplay (), master->getVisual (), master->getDepth (), ZPixmap, 0, 0, pixmapWidth, pixmapHeight, 8, 0);
		ximage->data = (char *) malloc (ximage->bytes_per_line * pixmapHeight);
	}

	// default vertical and horizontal image origins - center image
	int vorigin = (int) floor (master->zoom * (double) image->getChannelWidth (0) / 2.0) - pixmapWidth / 2;
	int horigin = (int) floor (master->zoom * (double) image->getChannelHeight (0) / 2.0) - pixmapHeight / 2;

	// create array which will hold the image
	// this will be then zoomed to pixmap array
	// to make it clear: iP is temporaly image storage, used to extract
	// data from image and calculate image cut levels, ximage->data then
	// holds pixmap, which is zoomed
	// modified image size
	int iW = (int) ceil (pixmapWidth / master->zoom);
	int iH = (int) ceil (pixmapHeight / master->zoom);
	int iPixelByteSize = image->getPixelByteSize ();
	char *iP = (char *) malloc (iW * iH * iPixelByteSize);
	char *iTop = iP;
	// pointer to top line of square image subset
	const char *iNineTop = (const char *) image->getChannelData (0);
	// prepare the image data to be processed
	const char *im_ptr = (const char *) image->getChannelData (0) + iPixelByteSize * (vorigin + horigin * image->getChannelWidth (0));

	// fill IP
	// copy image center..
	for (i = 0; i < iH / 3; i++)
	{
		if (master->GoNine)
		{
		 	// square..
			memcpy (iTop, iNineTop, iPixelByteSize * (iW / 3));
			iTop += iPixelByteSize * (iW / 3);

			// line..
			// im_ptr is offseted in horizontal direction, so we only add iW/3 for already copied pixels..
			memcpy (iTop, im_ptr + iPixelByteSize * (iW / 3), iPixelByteSize * (int) ceil ((double) iW / 3.0));
			iTop += iPixelByteSize * ((int) ceil ((double) iW / 3.0));

			memcpy (iTop, iNineTop + iPixelByteSize * (image->getChannelWidth (0) - 2 * iW / 3), iPixelByteSize * (iW / 3));
			iTop += iPixelByteSize * (iW / 3);

			iNineTop += iPixelByteSize * image->getChannelWidth (0);
		}
		else
		{
			memcpy (iTop, im_ptr, iPixelByteSize * iW);
			iTop += iPixelByteSize * iW;
		}
		im_ptr += iPixelByteSize * image->getChannelWidth (0);
	}
	// only center in all cases..
	for (;i < 2 * iH / 3; i++)
	{
		memcpy (iTop, im_ptr, iPixelByteSize * iW);
		im_ptr += iPixelByteSize * image->getChannelWidth (0);
		iTop += iPixelByteSize * iW;
	}

	if (master->GoNine)
		iNineTop += iPixelByteSize * (image->getChannelWidth (0) * (image->getChannelHeight (0) - (int) floor (2 * iH / 3.0)));

	// followed again by edge squares for end..
	for (;i < iH; i++)
	{
		if (master->GoNine)
		{
		 	// square..
			memcpy (iTop, iNineTop, iPixelByteSize * (iW / 3));
			iTop += iPixelByteSize * (iW / 3);

			// line..
			memcpy (iTop, im_ptr + 2 * iW / 3, iPixelByteSize * ((int) ceil ((double) iW / 3.0)));
			iTop += iPixelByteSize * ((int) ceil ((double) iW / 3.0));

			memcpy (iTop, iNineTop + (image->getChannelWidth (0) - 2 * iW / 3), iPixelByteSize * (iW / 3));
			iTop += iPixelByteSize  * (iW / 3);
			iNineTop += iPixelByteSize * image->getChannelWidth (0);
		}
		else
		{
			memcpy (iTop, im_ptr, iPixelByteSize * iW);
			iTop += iPixelByteSize * iW;
		}
		im_ptr += iPixelByteSize * image->getChannelWidth (0);
	}

	// get cuts
	double sigma, median;
	median = classical_median (iP, image->getDataType (), iW * iH, &sigma);
	low = (short unsigned int) (median - 3 * sigma);
	hig = (short unsigned int) (median + 5 * sigma);

	// clear progress indicator
	std::cout << std::setw (COLS) << "x" << '\r';

	if (image->getChannelSize () > 1)
	{
		std:: cout << Timestamp () << " multiple channel image" << std::endl;
		for (int ch = 0; ch < image->getChannelSize (); ch++)
		{
			std::cout << "channel " << (ch + 1) << " image [" << image->getChannelWidth (ch) << "x" << image->getChannelHeight (ch) << "x" << image->getPixelByteSize () << "]" << std::endl;
		}
	}
	else
	{
		std::cout << Timestamp () << " single channel image [" << image->getChannelWidth (0) << "x" << image->getChannelHeight (0) << "x" << image->getPixelByteSize () << "]" << std::endl;
	}
	std::cout << "Window median:" << median << " sigma " << sigma << " low:" << low << " hig:" << hig << std::endl;
	// transfer iP to pixmap, zoom it on fly
	for (i = 0; i < pixmapHeight; i++)
	{
		for (j = 0; j < pixmapWidth; j++)
		{
			double pW; // pixel value
			// do zooming..
			if (master->zoom >= 1)
			{
				switch (image->getDataType ())
				{
					case RTS2_DATA_BYTE:
						pW = (double) ((uint8_t *) iP)[((int) (i / master->zoom)) * iW + (int) (j / master->zoom)];
						break;
					case RTS2_DATA_USHORT:
						pW = (double) ((uint16_t *) iP)[((int) (i / master->zoom)) * iW + (int) (j / master->zoom)];
						break;
				}
			}
			else
			{
				pW = 0;
				// collect relevant pixels..
				int bsize = (int) ceil (1 / master->zoom);

				int ii = (int) floor (i * bsize);
				int jj = (int) floor (j * bsize);

				for (int k = 0; k < bsize; k++)
					for (int l = 0; l < bsize; l++)
					{
						switch (image->getDataType ())
						{
							case RTS2_DATA_BYTE:
								pW += ((uint8_t *) iP)[(ii + k) * iW + jj + l];
								break;
							case RTS2_DATA_USHORT:
								pW += ((uint16_t *) iP)[(ii + k) * iW + jj + l];
								break;
						}
					}

				pW /= bsize * bsize;

			}
			int grey = (int) floor (256.0 * (pW - (double) low) /
				((double) hig - (double) low));

			if (grey < 0) grey=0;
			if (grey > 255) grey=255;

			// here the pixel position is clear
			XPutPixel (ximage, j, i, master->getRGB (grey)->pixel);
		}
	}

	free (iP);

	XResizeWindow (master->getDisplay (), window, pixmapWidth, pixmapHeight);

	XPutImage (master->getDisplay (), pixmap, gc, ximage, 0, 0, 0, 0, pixmapWidth, pixmapHeight);

	// some info values
	image->getValue ("X", lastX);
	image->getValue ("Y", lastY);
	lastSizeX = image->getChannelWidth (0);
	lastSizeY = image->getChannelHeight (0);
	image->getValue ("BIN_V", binningsX);
	image->getValue ("BIN_H", binningsY);

	exposureStart.tv_sec = image->getExposureSec ();
	exposureStart.tv_usec = image->getExposureUsec ();

	if (!lastImage)
		lastImage = 1;

	// process mouse change events..

	if (mouseTelChange_x != INT_MAX && mouseTelChange_y != INT_MAX && getActualImage ())
	{
		struct ln_equ_posn change;
		getActualImage ()->getRaDec (mouseTelChange_x, mouseTelChange_y, change.ra, change.dec);

		change.ra -= getActualImage ()->getCenterRa ();
		change.dec -= getActualImage ()->getCenterDec ();

		logStream (MESSAGE_DEBUG) << "Change X:" << mouseTelChange_x << " Y:" << mouseTelChange_y << " RA:" << change.ra << " DEC:" << change.dec << sendLog;
		master->postEvent (new rts2core::Event (EVENT_MOUNT_CHANGE, (void *) &change));
		mouseTelChange_x = INT_MAX;
		mouseTelChange_y = INT_MAX;
	}

	redraw ();
	XFlush (master->getDisplay ());
}

void XFocusClientCamera::setCrossType (int in_crossType)
{
	crossType = in_crossType;
}

XFocusClient::XFocusClient (int in_argc, char **in_argv):FocusClient (in_argc, in_argv)
{
	displayName = NULL;

	crossType = 0;
	starsType = 0;

	changeVal = 15;
	zoom = 1.0;
	GoNine = false;

	addOption (OPT_DISPLAY, "display", 1, "name of X display");
	addOption (OPT_STARS, "stars", 0, "draw stars over image (default to don't)");
	addOption ('x', NULL, 1, "cross type (default to 1; possible values 0 no cross, 1 rectangles 2 circles, 3 BOOTES special");
	addOption (OPT_SAVE, "save", 0, "save filenames (default don't save");
	addOption (OPT_CHANGE, "change_val", 1, "change value (in arcseconds; default to 15 arcsec");
	addOption ('Z', NULL, 1, "Zoom (float number 0-xx)");
	addOption ('9', NULL, 0, "Nine sectors from different places of the CCD");
}

XFocusClient::~XFocusClient (void)
{
	if (display)
		XCloseDisplay (display);
}

void XFocusClient::usage ()
{
	std::cout
		<< "\t" << getAppName () << " -d C2 --nosync -e 10    .. takes 10 sec exposures on device C2. Exposures are not synchronize (blocked by unresposible devices,..)" << std::endl
		<< "\t" << getAppName () << " -d C0 -d C1 -e 20       .. takes 20 sec exposures on devices C0 and C1" << std::endl
		<< "\t" << getAppName () << " -d C2 -a -e 10          .. takes 10 sec exposures on device C2. Takes darks and use them" << std::endl
		<< "\t" << getAppName () << " -d C2 -a -e 10          .. takes 10 sec exposures on device C2. Takes darks and use them" << std::endl;
}

void XFocusClient::help ()
{
	rts2core::Client::help ();
	std::cout << "Keys:" << std::endl
		<< "\t1,2,3 .. binning 1x1, 2x2, 3x3" << std::endl
		<< "\t9     .. split screen to squares containg corners of the image and its center" << std::endl
		<< "\tq,a   .. increase/decrease exposure 0.01 sec" << std::endl
		<< "\tw,s   .. increase/decrease exposure 0.1 sec" << std::endl
		<< "\te,d   .. increase/decrease exposure 1 sec" << std::endl
		<< "\tf     .. full frame exposure" << std::endl
		<< "\tc     .. center (1/2x1/2 chip size) exposure" << std::endl
		<< "\ty     .. save FITS file" << std::endl
		<< "\tu     .. don't save fits file" << std::endl
		<< "\thjkl, arrows .. move (change mount position)" << std::endl
		<< "\t+-    .. change zoom" << std::endl;
}

int XFocusClient::processOption (int in_opt)
{
	switch (in_opt)
	{
		case OPT_SAVE:
			autoSave = 1;
			break;
		case OPT_DISPLAY:
			displayName = optarg;
			break;
		case OPT_STARS:
			starsType = 1;
			break;
		case 'X':
			crossType = atoi (optarg);
			break;
		case 'Z':
			zoom = atof (optarg);
			if (zoom < 0.0625)
				zoom = 0.0625;
			if (zoom > 16)
				zoom = 16;
			break;
		case '9':
			GoNine = true;
			break;
		case OPT_CHANGE:
			changeVal = atof (optarg);
			break;
		default:
			return FocusClient::processOption (in_opt);
	}
	return 0;
}

int XFocusClient::init ()
{
	int ret;
	ret = FocusClient::init ();
	if (ret)
		return ret;

	// convert to degrees
	changeVal /= 3600.0;

	display = XOpenDisplay (displayName);
	if (!display)
	{
		std::cerr << "Cannot open display" << std::endl;
		return -1;
	}

	depth = DefaultDepth (display, DefaultScreen (display));
	visual = DefaultVisual (display, DefaultScreen (display));

	colormap = DefaultColormap (display, DefaultScreen (display));

	std::cout << "Display opened succesfully" << std::endl;

	// allocate colormap..
	for (int i = 0; i < 256; i++)
	{
		rgb[i].red = (unsigned short) (65536 * (1.0 * i / 256));
		rgb[i].green = (unsigned short) (65536 * (1.0 * i / 256));
		rgb[i].blue = (unsigned short) (65536 * (1.0 * i / 256));
		rgb[i].flags = DoRed | DoGreen | DoBlue;

		ret = XAllocColor (display, colormap, rgb + i);
	}
	rgb[256].red = USHRT_MAX;
	rgb[256].green = 0;
	rgb[256].blue = 0;
	rgb[256].flags = DoRed | DoGreen | DoBlue;
	ret = XAllocColor (display, colormap, &rgb[256]);

	setTimeout (USEC_SEC);

	return 0;
}

FocusCameraClient * XFocusClient::createFocCamera (rts2core::Connection * conn)
{
	XFocusClientCamera *cam;
	cam = new XFocusClientCamera (conn, changeVal, this);
	cam->setCrossType (crossType);
	return cam;
}

void XFocusClient::addSelectSocks ()
{
	FD_SET (ConnectionNumber (display), &read_set);
	FocusClient::addSelectSocks ();
}

void XFocusClient::selectSuccess ()
{
	if (FD_ISSET (ConnectionNumber (display), &read_set))
	{
		postEvent (new rts2core::Event (EVENT_XWIN_SOCK));
	}
	FocusClient::selectSuccess ();
}

int main (int argc, char **argv)
{
	XFocusClient masterFocus = XFocusClient (argc, argv);
	return masterFocus.run ();
}
