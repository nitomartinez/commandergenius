
/*
 * CVideoDriver.cpp
 *
 *  Created on: 17.03.2009
 *      Author: gerstrong
 */
#include "CVideoDriver.h"
#include "CInput.h"

#include "../keen.h"
#include "video/colourconvert.h"
#include "video/colourtable.h"
#include "../scale2x/scalebit.h"
#include "../CLogFile.h"
#include "../CGraphics.h"

#define CKLOGFILENAME            	"genius.log"

#define MAX_CONSOLE_MESSAGES     	3
#define CONSOLE_MESSAGE_X        	3
#define CONSOLE_MESSAGE_Y        	3
#define CONSOLE_MESSAGE_SPACING  	9
#define CONSOLE_EXPIRE_RATE      	250

#define GAME_STD_WIDTH            320
#define GAME_STD_HEIGHT           200

// pointer to the line in VRAM to start blitting to when stretchblitting.
// this may not be the first line on the display as it is adjusted to
// center the image on the screen when in fullscreen.
unsigned char *VRAMPtr;
char blitsurface_alloc = 0;

SDL_Rect dstrect;

typedef struct stConsoleMessage
{
  char msg[80];
} stConsoleMessage;
stConsoleMessage cmsg[MAX_CONSOLE_MESSAGES];
int NumConsoleMessages = 0;
int ConsoleExpireTimer = 0;


CVideoDriver::CVideoDriver() {
	// Default values

	  showfps=true;
#ifdef WIZ
	  Width=320;
	  Height=240;
	  Depth=16;
	  Mode=0;
	  Fullscreen=true;
	  Filtermode=0;
	  Zoom=1;
	  FrameSkip=0;
	  m_targetfps = 30;	// Enable automatic frameskipping by default at 30
#else
	  Width=640;
	  Height=480;
	  Depth=0;
	  Mode=0;
	  Fullscreen=false;
	  Filtermode=1;
	  Zoom=2;
	  FrameSkip=2;
	  m_targetfps = 0;	// Disable automatic frameskipping by default
#endif
	  m_opengl = false;
#ifdef USE_OPENGL
	  m_opengl_filter = GL_NEAREST;
	  mp_OpenGL = NULL;
#endif
	  m_aspect_correction = true;

	  screenrect.x=0;
	  screenrect.y=0;
	  screenrect.h=0;
	  screenrect.w=0;

	  ScrollSurface=NULL;       // 512x512 scroll buffer
	  FGLayerSurface=NULL;       // Scroll buffer for Messages
	  BGLayerSurface=NULL;
	  BlitSurface=NULL;
}

CVideoDriver::~CVideoDriver() {
	stop();
}

void CVideoDriver::stop(void)
{
	if(screen) { SDL_FreeSurface(screen); g_pLogFile->textOut("freed screen<br>"); screen = NULL; }
	if(ScrollSurface && (ScrollSurface->map != NULL)) { SDL_FreeSurface(ScrollSurface); g_pLogFile->textOut("freed scrollsurface<br>"); ScrollSurface = NULL; }
	if(blitsurface_alloc) { blitsurface_alloc = 0; SDL_FreeSurface(BlitSurface); g_pLogFile->textOut("freed blitsurface<br>"); BlitSurface=NULL; }
#ifdef USE_OPENGL
	if(mp_OpenGL) { delete mp_OpenGL; mp_OpenGL = NULL; }
#endif
	g_pLogFile->textOut(GREEN,"CVideoDriver Close%s<br>", SDL_GetError());
}


bool CVideoDriver::start(void)
{
	bool retval = false;

	  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0)
	  {
		  g_pLogFile->textOut(RED,"Could not initialize SDL: %s<br>", SDL_GetError());
		  return false;
	  }
	  else
		  g_pLogFile->textOut(GREEN,"SDL was successfully initialized!<br>");

	  SDL_WM_SetCaption("Commander Genius (CKP)", NULL);
	  // When the program is through executing, call SDL_Quit
	  atexit(SDL_Quit);

	  if(!applyMode())
	  {
		  g_pLogFile->textOut(RED,"VideoDriver: Error applying mode! Your Videocard doesn't seem to work on CKP<br>");
		  g_pLogFile->textOut(RED,"Check, if you have the most recent drivers installed!<br>");
		  return false;
	  }

	  retval = createSurfaces();
	  initOpenGL();

	  return retval;
}

bool CVideoDriver::initOpenGL()
{
#ifdef USE_OPENGL
	if(m_opengl) // If OpenGL could be set, initialize the matrices
	{
		mp_OpenGL = new COpenGL();
		if(!(mp_OpenGL->initGL(Width, Height, Depth, m_opengl_filter, Filtermode+1, m_aspect_correction)))
		{
			delete mp_OpenGL;
			mp_OpenGL = NULL;
			m_opengl = false;
		}
		else
			mp_OpenGL->setSurface(BlitSurface);
	}
#endif

	return m_opengl;
}

bool CVideoDriver::applyMode(void)
{
	// Check if some zoom/filter modes are illogical
	if( (Zoom == 3 && Filtermode == 1) && !m_opengl )
		Zoom = 2;

	// Grab a surface on the screen
	Mode = SDL_HWPALETTE;

	// Support for doublebuffering
	Mode |= SDL_DOUBLEBUF;

	// Enable OpenGL
#ifdef USE_OPENGL
	if(m_opengl)
	{
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
		SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 1);
		Mode |= SDL_OPENGL;
	}
#endif

	// Now we decide if it will be fullscreen or windowed mode.
	if(Fullscreen)
		Mode |= SDL_FULLSCREEN;

	// Before the resolution is set, check, if the zoom factor is too high!
	while(((Width/GAME_STD_WIDTH) < Zoom || (Height/GAME_STD_HEIGHT) < Zoom) && (Zoom > 1))
		Zoom--;

    // Try to center the screen!
	screenrect.w = blitrect.w = GAME_STD_WIDTH*Zoom;
	screenrect.h = blitrect.h = GAME_STD_HEIGHT*Zoom;
	screenrect.x = (Width-screenrect.w)>>1;
	if(Width == 320)
		screenrect.y = 0;
	else
		screenrect.y = (Height-screenrect.h)>>1;
    blitrect.x = 0;
    blitrect.y = 0;

	// And Display can be setup.
	screen = SDL_SetVideoMode(Width,Height,Depth,Mode);

	Depth = screen->format->BitsPerPixel;

	if( !screen )
	{
		g_pLogFile->textOut(RED,"VidDrv_Start(): Couldn't create a SDL surface: %s<br>", SDL_GetError());
		return false;
	}

	if(!Fullscreen)
		SDL_ShowCursor(SDL_ENABLE);
	else
		SDL_ShowCursor(SDL_DISABLE);

	return true;
}

void CVideoDriver::setMode(unsigned int srcW, unsigned int srcH,
							unsigned short srcD)
{
	Width 	= srcW;
	Height 	= srcH;
	Depth	= srcD;
}
void CVideoDriver::setFrameskip(unsigned short value)
{
	FrameSkip = value;
}
void CVideoDriver::setFilter(short value)
{
	Filtermode = value;
}
void CVideoDriver::setZoom(short value)
{
	Zoom = value;
}

bool CVideoDriver::createSurfaces(void)
{
	// This function creates the surfaces which are needed for the game.
	unsigned stretch_blit_yoff;

	stretch_blit_yoff = 0;

	ScrollSurface = SDL_CreateRGBSurfaceFrom(g_pGraphics->getScrollbuffer(), 512, 512, 8, 512, screen->format->Rmask, screen->format->Gmask, screen->format->Bmask, screen->format->Amask);
	SDL_SetColorKey(ScrollSurface, SDL_SRCCOLORKEY, COLOUR_MASK);
	if (!ScrollSurface)
	{
		g_pLogFile->textOut(RED,"VideoDriver: Couldn't create ScrollSurface!<br>");
	  return false;
	}

	BGLayerSurface = SDL_CreateRGBSurface(Mode,320, 200, Depth,  screen->format->Rmask, screen->format->Gmask, screen->format->Bmask, screen->format->Amask);
	if (!BGLayerSurface)
	{
		g_pLogFile->textOut(RED,"VideoDriver: Couldn't create BGLayerSurface!<br>");
	  return false;
	}


	FGLayerSurface = SDL_CreateRGBSurface(Mode,320, 200, Depth,  screen->format->Rmask, screen->format->Gmask, screen->format->Bmask, screen->format->Amask);
	if (!FGLayerSurface)
	{
		g_pLogFile->textOut(RED,"VideoDriver: Couldn't create FGLayerSurface!<br>");
	  return false;
	}
	SDL_SetColorKey( FGLayerSurface, SDL_SRCCOLORKEY,
					SDL_MapRGB(FGLayerSurface->format, 0, 0, 0) );

	//Set surface alpha
	SDL_SetAlpha( FGLayerSurface, SDL_SRCALPHA, 225 );

    if(Width == 320 && !m_opengl)
    {
    	g_pLogFile->textOut("Blitsurface = Screen<br>");
    	BlitSurface = screen;
    	blitsurface_alloc = 0;
    	VRAMPtr = (unsigned char*)screen->pixels + ((Width * stretch_blit_yoff * Depth)>>3)+screenrect.y*screen->pitch + (screenrect.x*Depth>>3);
    }
    else
    {
    	g_pLogFile->textOut("Blitsurface = creatergbsurfacefrom<br>");
    	BlitSurface = SDL_CreateRGBSurface(Mode,GAME_STD_WIDTH, GAME_STD_HEIGHT, Depth,  screen->format->Rmask, screen->format->Gmask, screen->format->Bmask, screen->format->Amask);
		if (!BlitSurface)
		{
			g_pLogFile->textOut(RED,"VidDrv_Start(): Couldn't create BlitSurface!<br>");
			return false;
		}
		blitsurface_alloc = 1;
		VRAMPtr = (unsigned char*)screen->pixels + ((Width * stretch_blit_yoff * Depth)>>3)+screenrect.y*screen->pitch + (screenrect.x*Depth>>3);
    }

    dstrect.x = 0; dstrect.y = 0;
	dstrect.w = GAME_STD_WIDTH;
	dstrect.h = GAME_STD_HEIGHT;

	return true;
}

// alter the color palette. the palette is not actually altered
// on-screen until pal_apply() is called.
void CVideoDriver::pal_set(short colour, char red, char green, char blue)
{
  MyPalette[colour].r = red;
  MyPalette[colour].g = green;
  MyPalette[colour].b = blue;
}

// applies all changes to the palette made with pal_set
void CVideoDriver::pal_apply(void)
{
  SDL_SetColors(screen, (SDL_Color *) &MyPalette, 0, 256);
  SDL_SetColors(ScrollSurface, (SDL_Color *) &MyPalette, 0, 256);
  if (blitsurface_alloc)
  {
    SDL_SetColors(BlitSurface, (SDL_Color *) &MyPalette, 0, 256);
  }
}

void CVideoDriver::sb_blit(void)
{
SDL_Rect srcrect;
char wraphoz, wrapvrt;
int save_dstx, save_dstw, save_srcx, save_srcw;
char tempbuf[80];

	blitBGLayer();

   dstrect.x = 0; dstrect.y = 0;
   dstrect.w = 320; dstrect.h = 200;

   srcrect.x = scrollx_buf;
   srcrect.y = scrolly_buf;
   if (scrollx_buf > (512-320))
   { // need to wrap right side
     srcrect.w = (512-scrollx_buf);
     wraphoz = 1;
   }
   else
   { // single blit for whole horizontal copy
     srcrect.w = 320;
     wraphoz = 0;
   }

   if (scrolly_buf > (512-200))
   { // need to wrap on bottom
     srcrect.h = (512-scrolly_buf);
     wrapvrt = 1;
   }
   else
   { // single blit for whole bottom copy
     srcrect.h = 200;
     wrapvrt = 0;
   }

   SDL_BlitSurface(ScrollSurface, &srcrect, BlitSurface, &dstrect);

   if (wraphoz && wrapvrt)
   {
      // first do same thing we do for wraphoz
      save_dstx = dstrect.x;
      save_dstw = dstrect.w;
      save_srcx = srcrect.x;
      save_srcw = srcrect.w;
      dstrect.x = srcrect.w;
      dstrect.w = 320 - dstrect.x;
      srcrect.x = 0;
      srcrect.w = (320 - srcrect.w);
      SDL_BlitSurface(ScrollSurface, &srcrect, BlitSurface, &dstrect);
      // now repeat for the bottom
      // (lower-right square)
      dstrect.y = srcrect.h;
      dstrect.h = 200 - dstrect.y;
      srcrect.y = 0;
      srcrect.h = (200 - srcrect.h);
      SDL_BlitSurface(ScrollSurface, &srcrect, BlitSurface, &dstrect);
      // (lower-left square)
      dstrect.x = save_dstx;
      dstrect.w = save_dstw;
      srcrect.x = save_srcx;
      srcrect.w = save_srcw;
      SDL_BlitSurface(ScrollSurface, &srcrect, BlitSurface, &dstrect);
   }
   else if (wraphoz)
   {
      dstrect.x = srcrect.w;
      dstrect.w = 320 - dstrect.x;
      srcrect.x = 0;
      srcrect.w = (320 - srcrect.w);
      SDL_BlitSurface(ScrollSurface, &srcrect, BlitSurface, &dstrect);
   }
   else if (wrapvrt)
   {
      dstrect.y = srcrect.h;
      dstrect.h = 200 - dstrect.y;
      srcrect.y = 0;
      srcrect.h = (200 - srcrect.h);
      SDL_BlitSurface(ScrollSurface, &srcrect, BlitSurface, &dstrect);
   }

   drawConsoleMessages();
   if (showfps)
   {

#ifdef DEBUG
     sprintf(tempbuf, "FPS: %03d; x = %ld ; y = %d", fps, player[0].x >>CSF, player[0].y >>CSF);

#else
     sprintf(tempbuf, "FPS: %03d", fps);
#endif
     g_pGraphics->drawFont( (unsigned char *) tempbuf, 320-3-(strlen( (char *) tempbuf)<<3), 3, 1);
   }

   update_screen();
}
void CVideoDriver::blitBGLayer(void)
{
	SDL_BlitSurface(BGLayerSurface, NULL, BlitSurface, NULL);
}

void CVideoDriver::update_screen(void)
{
#ifdef USE_OPENGL
   if(m_opengl)
   {
	   SDL_BlitSurface(FGLayerSurface, NULL, BlitSurface, NULL);

	   mp_OpenGL->render();

	   SDL_LockSurface(FGLayerSurface);
	   // Flush the layers
	   memset(FGLayerSurface->pixels,SDL_MapRGB(FGLayerSurface->format, 0, 0, 0),
			   GAME_STD_WIDTH*GAME_STD_HEIGHT*FGLayerSurface->format->BytesPerPixel);
	   SDL_UnlockSurface(FGLayerSurface);
   }
   else // No OpenGL but Software Rendering
   {
#endif
	   SDL_BlitSurface(FGLayerSurface, NULL, BlitSurface, NULL);

	   // if we're doing zoom then we have copied the scroll buffer into
	   // another offscreen buffer, and must now stretchblit it to the screen
	   if (Zoom == 1 && Width != 320 )
	   {
		   SDL_LockSurface(BlitSurface);
		   SDL_LockSurface(screen);

		   if(Filtermode == 0)
		   {
			   noscale((char*)VRAMPtr, (char*)BlitSurface->pixels, (Depth>>3));
		   }
		   else
		   {
			   g_pLogFile->textOut(PURPLE,"Sorry, but this filter doesn't work at that zoom mode<br>");
			   g_pLogFile->textOut(PURPLE,"Try to use a higher zoom factor. Switching to no-filter<br>");
			   Filtermode = 0;
		   }
		   SDL_UnlockSurface(screen);
		   SDL_UnlockSurface(BlitSurface);
	   }
	   if (Zoom == 2)
	   {
		   SDL_LockSurface(BlitSurface);
		   SDL_LockSurface(screen);

		   if(Filtermode == 0)
		   {
			   scale2xnofilter((char*)VRAMPtr, (char*)BlitSurface->pixels, (Depth>>3));
		   }
		   else if(Filtermode == 1)
		   {
			   scale(2, VRAMPtr, Width*(Depth>>3), BlitSurface->pixels,
					   GAME_STD_WIDTH*(Depth>>3), (Depth>>3), GAME_STD_WIDTH, GAME_STD_HEIGHT);
		   }
		   else
		   {
			   g_pLogFile->textOut(PURPLE,"Sorry, but this filter doesn't work at that zoom mode<br>");
			   g_pLogFile->textOut(PURPLE,"Try to use a higher zoom factor. Switching to no-filter<br>");
			   Filtermode = 0;
		   }

		   SDL_UnlockSurface(screen);
		   SDL_UnlockSurface(BlitSurface);
	   }
	   else if (Zoom == 3)
	   {
		   SDL_LockSurface(BlitSurface);
		   SDL_LockSurface(screen);

		   if(Filtermode == 0)
		   {
			   scale3xnofilter((char*)VRAMPtr, (char*)BlitSurface->pixels, (Depth>>3));
		   }
		   else if(Filtermode == 1)
		   {
			   scale(2, VRAMPtr, Width*(Depth>>3), BlitSurface->pixels,
					   GAME_STD_WIDTH*(Depth>>3), (Depth>>3), GAME_STD_WIDTH, GAME_STD_HEIGHT);
		   }
		   else if(Filtermode == 2)
		   {
			   scale(3, VRAMPtr, Width*(Depth>>3), BlitSurface->pixels,
					   GAME_STD_WIDTH*(Depth>>3), (Depth>>3), GAME_STD_WIDTH, GAME_STD_HEIGHT);
		   }
		   else
		   {
			   g_pLogFile->textOut(PURPLE,"Sorry, but this filter doesn't work at that zoom mode<br>");
			   g_pLogFile->textOut(PURPLE,"Try to use a higher zoom factor. Switching to no-filter<br>");
			   Filtermode = 0;
		   }
		   SDL_UnlockSurface(screen);
		   SDL_UnlockSurface(BlitSurface);
	   }

	   SDL_Flip(screen);
	   //SDL_UpdateRect(screen, screenrect.x, screenrect.y, screenrect.w, screenrect.h);

	   SDL_LockSurface(FGLayerSurface);
	   // Flush the layers
	   memset(FGLayerSurface->pixels,SDL_MapRGB(FGLayerSurface->format, 0, 0, 0),
			   GAME_STD_WIDTH*GAME_STD_HEIGHT*FGLayerSurface->format->BytesPerPixel);
	   SDL_UnlockSurface(FGLayerSurface);
#ifdef USE_OPENGL
   }
#endif
}

void CVideoDriver::noscale(char *dest, char *src, short bbp)
{
	// just passes a blitsurface to the screen
	int i;
	for(i=0 ; i < 200 ; i++)
		memcpy(dest+(i*Width)*bbp,src+(i*GAME_STD_WIDTH)*bbp,320*bbp);
}

void CVideoDriver::scale2xnofilter(char *dest, char *src, short bbp)
{
	// workaround for copying correctly stuff to the screen, so the screen is scaled normally
    // to 2x (without filter). This applies to 16 and 32-bit colour depth.
	// use bit shifting method for faster blit!
	bbp >>= 1;

	int i,j;
	for(i=0 ; i < 200 ; i++)
	{
		for(j = 0; j < 320 ; j++)
		{
			memcpy(dest+((j<<1)<<bbp)+(((i<<1)*Width)<<bbp),src+(j<<bbp)+((i*GAME_STD_WIDTH)<<bbp),bbp<<1);
			memcpy(dest+(((j<<1)+1)<<bbp)+(((i<<1)*Width)<<bbp),src+(j<<bbp)+((i*GAME_STD_WIDTH)<<bbp),bbp<<1);
		}
		memcpy(dest+(((i<<1)+1)*(Width<<bbp)),(dest+(i<<1)*(Width<<bbp)),(bbp<<2)*GAME_STD_WIDTH);
	}
}

void CVideoDriver::scale3xnofilter(char *dest, char *src, short bbp)
{
	// workaround for copying correctly stuff to the screen, so the screen is scaled normally
    // to 2x (without filter). This applies to 16 and 32-bit colour depth.
	// Optimization of using bit shifting
	bbp >>= 1;

	int i,j;
	for(i=0 ; i < 200 ; i++)
	{
		for(j = 0; j < 320 ; j++)
		{
			// j*3 = (j<<1) + j
			memcpy(dest+(((j<<1)+j)<<bbp)+((((i<<1) + i)*Width)<<bbp),src+(j<<bbp)+((i*GAME_STD_WIDTH)<<bbp),bbp<<1);
			memcpy(dest+(((j<<1)+j+1)<<bbp)+((((i<<1) + i)*Width)<<bbp),src+(j<<bbp)+((i*GAME_STD_WIDTH)<<bbp),bbp<<1);
			memcpy(dest+(((j<<1)+j+2)<<bbp)+((((i<<1) + i)*Width)<<bbp),src+(j<<bbp)+((i*GAME_STD_WIDTH)<<bbp),bbp<<1);
		}
		memcpy(dest+((i<<1)+i+1)*(Width<<bbp),dest+((i<<1)+i)*(Width<<bbp),(3<<bbp)*GAME_STD_WIDTH);
		memcpy(dest+((i<<1)+i+2)*(Width<<bbp),dest+((i<<1)+i)*(Width<<bbp),(3<<bbp)*GAME_STD_WIDTH);
	}
}

// functions to directly set and retrieve pixels from the VGA display
void CVideoDriver::setpixel(unsigned int x, unsigned int y, unsigned char c)
{
	if( x > Width || y > Height )
		return;


    if(BlitSurface->format->BitsPerPixel == 16)
    {
    	Uint16 *ubuff16;
        ubuff16 = (Uint16*) FGLayerSurface->pixels;
    	ubuff16 += (y * 320) + x;
    	*ubuff16 = convert4to16BPPcolor(c, BlitSurface);
    }
    else if(BlitSurface->format->BitsPerPixel == 32)
    {
    	Uint32 *ubuff32;
        ubuff32 = (Uint32*) FGLayerSurface->pixels;
    	ubuff32 += (y * 320) + x;
    	*ubuff32 = convert4to32BPPcolor(c, BlitSurface);
    }
    else
    {
    	Uint8 *ubuff8;
        ubuff8 = (Uint8*) FGLayerSurface->pixels;
    	ubuff8 += (y * 320) + x;
    	*ubuff8 = (Uint8) c;
    }
}
unsigned char CVideoDriver::getpixel(int x, int y)
{
  return 0;
}

// "Console" here refers to the capability to pop up in-game messages
// in the upper-left corner during game play ala Doom.
void CVideoDriver::drawConsoleMessages(void)
{
int i;
int y;

 if (!NumConsoleMessages) return;
 if (!ConsoleExpireTimer)
 {
   NumConsoleMessages--;
   if (!NumConsoleMessages) return;
   ConsoleExpireTimer = CONSOLE_EXPIRE_RATE;
 }
 else ConsoleExpireTimer--;

 y = CONSOLE_MESSAGE_Y;
 for(i=0;i<NumConsoleMessages;i++)
 {
	 g_pGraphics->drawFont( (unsigned char *) cmsg[i].msg, CONSOLE_MESSAGE_X, y, 1);
    y += CONSOLE_MESSAGE_SPACING;
 }
}

// removes all console messages
void CVideoDriver::DeleteConsoleMsgs(void)
{
  NumConsoleMessages = 0;
}

// adds a console msg to the top of the screen and scrolls any
// other existing messages downwards
void CVideoDriver::AddConsoleMsg(const char *the_msg)
{
int i;
  for(i=MAX_CONSOLE_MESSAGES-2;i>=0;i--)
  {
    strcpy(cmsg[i+1].msg, cmsg[i].msg);
  }
  strcpy(cmsg[0].msg, the_msg);

  if (NumConsoleMessages < MAX_CONSOLE_MESSAGES) NumConsoleMessages++;
  ConsoleExpireTimer = CONSOLE_EXPIRE_RATE;
}

short CVideoDriver::getZoomValue(void){ return Zoom; }

void CVideoDriver::showFPS(bool value){ showfps = value; }

void CVideoDriver::isFullscreen(bool value)
{
	Fullscreen = value;
	return;
}

unsigned short CVideoDriver::getFrameskip(void)
{
	return FrameSkip;
}
bool CVideoDriver::getShowFPS(void)
{
	return showfps;
}
short CVideoDriver::getFiltermode(void)
{
	if(Filtermode < 0)
		Filtermode = 0;
	return Filtermode;
}
bool CVideoDriver::getFullscreen(void)
{	return Fullscreen;	}
unsigned int CVideoDriver::getWidth(void)
{	return Width;	}
unsigned int CVideoDriver::getHeight(void)
{	return Height;	}
unsigned short CVideoDriver::getDepth(void)
{	return Depth;	}
SDL_Surface *CVideoDriver::getScrollSurface(void)
{	return ScrollSurface; }
SDL_Surface *CVideoDriver::getBGLayerSurface(void)
{	return BGLayerSurface; }


