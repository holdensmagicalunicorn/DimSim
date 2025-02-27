/*
 *  OSXvnc Copyright (C) 2002-2004 Redstone Software osxvnc@redstonesoftware.com
 *  Original Xvnc code Copyright (C) 1999 AT&T Laboratories Cambridge.
 *  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

/*
 *	Primary modifications for Dimdim - Bharat Varma
 *	1. We do not have any listener running.
 *	2. Only reverse connects are allowed - usually this is only for 1 client. i.e. 1 reverse connection per session
 *	3. No threads. We will have an extremely simple 'capture-send-capture-send...' loop.
 *	4. We do not support remote events or clipboard (pasteboard)
 *	5. User switch notification is turned off for now.
 *  6. App specific data will go into $HOME/.Dimdim/ folder
 */

#include <ApplicationServices/ApplicationServices.h>
#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#include <sys/sysctl.h>

#include "rfb.h"
//#include "localbuffer.h"

#include "rfbserver.h"
#import "VNCServer.h"

#import "../RFBBundleProtocol.h"

/* Dimdim specific includes -- Bharat Varma */

#import "dToolkit.h"
#import "dVNCQueue.h"
#import "DimdimMessageClient.h"

/* Dimdim specific includes -- Bharat Varma */

/* That's great that they #define it to use the new symbol that doesn't exist in older versions
better to just not even define it - but give a warning or something  */
// This should be in CGRemoteOperationApi.h
#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_3
#undef CGSetLocalEventsFilterDuringSupressionState
//#warning Using Obsolete CGSetLocalEventsFilterDuringSupressionState for backwards compatibility to 10.2
CG_EXTERN CGError CGSetLocalEventsFilterDuringSupressionState(CGEventFilterMask filter, CGEventSupressionState state);
#endif

// So we can compile on 10.2
#ifndef NSAppKitVersionNumber10_3
#define NSAppKitVersionNumber10_3 743
#endif
//#ifndef NSWorkspaceSessionDidBecomeActiveNotification
//#define NSWorkspaceSessionDidBecomeActiveNotification @"NSWorkspaceSessionDidBecomeActiveNotification"
//#endif
//#ifndef NSWorkspaceSessionDidResignActiveNotification
//#define NSWorkspaceSessionDidResignActiveNotification @"NSWorkspaceSessionDidResignActiveNotification"
//#endif

ScreenRec hackScreen;
rfbScreenInfo rfbScreen;

int rfbProtocolMajorVersion = 3;
int rfbProtocolMinorVersion = 8;

char desktopName[256] = "";

BOOL keepRunning = TRUE;

BOOL littleEndian = FALSE;
int  rfbPort = 0; //5900;
int  rfbMaxBitDepth = 0;
Bool rfbAlwaysShared = FALSE;
Bool rfbNeverShared = FALSE;
Bool rfbDontDisconnect = FALSE;
Bool rfbLocalhostOnly = FALSE;
Bool rfbInhibitEvents = FALSE;
Bool rfbReverseMods = FALSE;

Bool rfbSwapButtons = TRUE;
Bool rfbDisableRemote = FALSE;
Bool rfbDisableRichClipboards = FALSE;
Bool rfbRemapShortcuts = FALSE;
BOOL rfbShouldSendUpdates = TRUE;
BOOL registered = FALSE;
BOOL restartOnUserSwitch = FALSE;
BOOL useIP4 = TRUE;
BOOL unregisterWhenNoConnections = FALSE;
BOOL nonBlocking = FALSE;

// OSXvnc 0.8 This flag will use a local buffer which will allow us to display the mouse cursor
// Bool rfbLocalBuffer = FALSE;

static pthread_mutex_t logMutex;
pthread_cond_t listenerGotNewClient;

/* OSXvnc 0.8 for screensaver .... */
// setup screen saver disabling timer
// Not sure we want or need this...
static EventLoopTimerUPP  screensaverTimerUPP;
static EventLoopTimerRef screensaverTimer;
Bool rfbDisableScreenSaver = FALSE;

// Display ID
CGDirectDisplayID displayID = NULL;

extern void rfbScreensaverTimer(EventLoopTimerRef timer, void *userData);

int rfbDeferUpdateTime = 40; /* in ms */

static char reverseHost[255] = "";
static int reversePort = 5500;

CGDisplayErr displayErr;

// Server Data
rfbserver thisServer;
// List of Loaded Bundles
NSMutableArray *bundleArray = nil;

static bool rfbScreenInit(void);

/*
 * rfbLog prints a time-stamped message to the log file (stderr).
 */

void rfbLog(char *format, ...) 
{
	if ([[dimdimProfile valueForKey:@"Logging"] intValue] < 2)
		return;
	
    va_list args;
	
    pthread_mutex_lock(&logMutex);
	NSString *nsFormat = [[NSString alloc] initWithCString:format];
	
    va_start(args, format);
	
	NSString* data = [[NSString alloc] initWithFormat:nsFormat arguments:args];
	[DimdimHelpers logWrite:logDump nsData:data];
	[data release];
	
	fflush(logDump);
    
	va_end(args);

    [nsFormat release];
    pthread_mutex_unlock(&logMutex);
}

void rfbDebugLog(char *format, ...) 
{
	if ([[dimdimProfile valueForKey:@"Logging"] intValue] < 2)
		return;
	
#ifdef __DEBUGGING__

    va_list args;
    
    pthread_mutex_lock(&logMutex);
	NSString *nsFormat = [[NSString alloc] initWithCString:format];
	
    va_start(args, format);
	
	NSString* data = [[NSString alloc] initWithFormat:nsFormat arguments:args];
	[DimdimHelpers logWrite:logDump nsData:data];
	[data release];
	
	fflush(logDump);
	
    va_end(args);
	
    [nsFormat release];
    pthread_mutex_unlock(&logMutex);
	
#endif
}


void rfbLogPerror(char *str) {
    rfbLog("%s: %s\n", str, strerror(errno));
}

void bundlesPerformSelector(SEL performSel) {
    NSAutoreleasePool *bundlePool = [[NSAutoreleasePool alloc] init];
    NSEnumerator *bundleEnum = [bundleArray objectEnumerator];
    NSBundle *bundle = nil;

    while ((bundle = [bundleEnum nextObject])) {
		if ([[bundle principalClass] respondsToSelector:performSel])
			[[bundle principalClass] performSelector:performSel];
	}

    [bundlePool release];
}

// Some calls fail under older OS X'es so we will do some detected loading
void loadDynamicBundles(BOOL startup) {
    NSAutoreleasePool *startPool = [[NSAutoreleasePool alloc] init];
    NSBundle *osxvncBundle = [NSBundle mainBundle];
    NSString *execPath =[[NSProcessInfo processInfo] processName];

    // Setup thisServer structure
	thisServer.desktopName = desktopName;
	thisServer.rfbPort = rfbPort;
	thisServer.rfbLocalhostOnly = rfbLocalhostOnly;
	thisServer.listenerGotNewClient = listenerGotNewClient;
	
	// These can be modified by the bundles
    thisServer.keyTable = keyTable;
    thisServer.keyTableMods = keyTableMods;
	
    thisServer.pressModsForKeys = &pressModsForKeys;
	thisServer.alternateKeyboardHandler = &alternateKeyboardHandler;
	
    NSLog(@"Main Bundle: %@", [osxvncBundle bundlePath]);
    if (!osxvncBundle) {
        // If We Launched Relative - make it absolute
        if (![execPath isAbsolutePath])
            execPath = [[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:execPath];

        execPath = [execPath stringByStandardizingPath];
        execPath = [execPath stringByResolvingSymlinksInPath];

        osxvncBundle = [NSBundle bundleWithPath:execPath];
        //resourcesPath = [[[resourcesPath stringByDeletingLastPathComponent] stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"Resources"];
    }

    if (osxvncBundle) {
        NSArray *bundlePathArray = [NSBundle pathsForResourcesOfType:@"bundle" inDirectory:[osxvncBundle resourcePath]];
        NSEnumerator *bundleEnum = [bundlePathArray reverseObjectEnumerator];
        NSString *bundlePath = nil;

        bundleArray = [[NSMutableArray alloc] init];

        while ((bundlePath = [bundleEnum nextObject])) {
            NSBundle *aBundle = [NSBundle bundleWithPath:bundlePath];

            NSLog(@"Loading Bundle %@", bundlePath);

			NS_DURING {
				if ([aBundle load]) {
					[bundleArray addObject:aBundle];
					[[aBundle principalClass] rfbStartup: &thisServer];
				}
				else {
					NSLog(@"\t-Bundle Load Failed");
				}
			}
			NS_HANDLER
				NSLog(@"\t-Bundle Load Failed (%@)", [localException name]);
			NS_ENDHANDLER
        }
    }
    else {
        NSLog(@"No Bundles Loaded - Run %@ from inside OSXvnc.app", execPath);
    }


    [startPool release];
}

void refreshCallback(CGRectCount count, const CGRect *rectArray, void *ignore) {
//	return;
    BoxRec box;
    RegionRec region;
    rfbClientIteratorPtr iterator;
    rfbClientPtr cl = NULL;
    int i;

    for (i = 0; i < count; i++) {
        box.x1 = rectArray[i].origin.x;
        box.y1 = rectArray[i].origin.y;
        box.x2 = box.x1 + rectArray[i].size.width;
        box.y2 = box.y1 + rectArray[i].size.height;

        SAFE_REGION_INIT(&hackScreen, &region, &box, 0);

        iterator = rfbGetClientIterator();
        while ((cl = rfbClientIteratorNext(iterator)) != NULL) {
			pthread_mutex_lock(&cl->updateMutex);
            REGION_UNION(&hackScreen,&cl->modifiedRegion,&cl->modifiedRegion,&region);
            pthread_cond_signal(&cl->updateCond);
			pthread_mutex_unlock(&cl->updateMutex);
        }
		
        rfbReleaseClientIterator(iterator);

        REGION_UNINIT(&hackScreen, &region);
    }
}

//CGError screenUpdateMoveCallback(CGScreenUpdateMoveDelta delta, CGRectCount count, const CGRect * rectArray, void * userParameter) {
//	//NSLog(@"Moved Callback");
//	return 0;
//}


void rfbCheckForScreenResolutionChange() {
	return;
    BOOL sizeChange = (rfbScreen.width != CGDisplayPixelsWide(displayID) ||
                       rfbScreen.height != CGDisplayPixelsHigh(displayID));

    // See if screen changed
    if (sizeChange || rfbScreen.bitsPerPixel != CGDisplayBitsPerPixel(displayID)) {
        rfbClientIteratorPtr iterator;
        rfbClientPtr cl = NULL;
		BOOL screenOK = TRUE;
		int maxTries = 12;

        // Block listener from accepting new connections while we restart

        iterator = rfbGetClientIterator();
        // Disconnect Existing Clients
        while ((cl = rfbClientIteratorNext(iterator))) {
            pthread_mutex_lock(&cl->updateMutex);
            // Keep locked until after screen change
        }
        rfbReleaseClientIterator(iterator);

		do {
			screenOK = rfbScreenInit();
		} while (!screenOK && maxTries-- && usleep(2000000)==0);
		if (!screenOK)
			exit(1);
		
		rfbLog("Screen Geometry Changed - (%d,%d) Depth: %d\n",
               CGDisplayPixelsWide(displayID),
               CGDisplayPixelsHigh(displayID),
               CGDisplayBitsPerPixel(displayID));
		
		
		iterator = rfbGetClientIterator();
        while ((cl = rfbClientIteratorNext(iterator))) {
            // Only need to notify them on a SIZE change - other changes just make us re-init
            if (sizeChange) {
                if (cl->desktopSizeUpdate) {
                    BoxRec box;
                    rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *)cl->updateBuf;
                    fu->type = rfbFramebufferUpdate;
                    fu->nRects = Swap16IfLE(1);
                    cl->ublen = sz_rfbFramebufferUpdateMsg;

                    rfbSendScreenUpdateEncoding(cl);

					// Reset Frame Buffer
					if (cl->scalingFrameBuffer && cl->scalingFrameBuffer != rfbGetFramebuffer())
						free(cl->scalingFrameBuffer);
					
					if (cl->scalingFactor == 1) {
						cl->scalingFrameBuffer = rfbGetFramebuffer();
						cl->scalingPaddedWidthInBytes = rfbScreen.paddedWidthInBytes;
					}
					else {
						const unsigned long csh = (rfbScreen.height+cl->scalingFactor-1)/ cl->scalingFactor;
						const unsigned long csw = (rfbScreen.width +cl->scalingFactor-1)/ cl->scalingFactor;
						
						cl->scalingFrameBuffer = malloc( csw*csh*rfbScreen.bitsPerPixel/8 );
						cl->scalingPaddedWidthInBytes = csw * rfbScreen.bitsPerPixel/8;
					}
					
                    box.x1 = box.y1 = 0;
                    box.x2 = rfbScreen.width;
                    box.y2 = rfbScreen.height;
					REGION_INIT(pScreen,&cl->modifiedRegion,&box,0);
                    //cl->needNewScreenSize = TRUE;
                }
                else
                    rfbCloseClient(cl);
            }
            else {
				// In theory we shouldn't need to disconnect them but some state in the cl record seems to cause a problem
				rfbCloseClient(cl);
                rfbSetTranslateFunction(cl);
            }

			sleep(2); // We may detect the new depth before OS X has quite finished getting everything ready for it.
            pthread_mutex_unlock(&cl->updateMutex);
            pthread_cond_signal(&cl->updateCond);
        }
        rfbReleaseClientIterator(iterator);
    }
}

static void *clientOutput(void *data) {
    rfbClientPtr cl = (rfbClientPtr)data;
    RegionRec updateRegion;
    Bool haveUpdate = false;
	
	NSAutoreleasePool *fbPool = [[NSAutoreleasePool alloc] init];

    while (1) {
        haveUpdate = false;
		
        pthread_mutex_lock(&cl->updateMutex);
        while (!haveUpdate) {
            if (cl->sock == -1) {
                /* Client has disconnected. */
                pthread_mutex_unlock(&cl->updateMutex);
                return NULL;
            }

			// Only do checks if we HAVE an outstanding request
			if (REGION_NOTEMPTY(&hackScreen, &cl->requestedRegion)) {
				/* REDSTONE */
				if (rfbDeferUpdateTime > 0 && !cl->immediateUpdate) {
					// Compare Request with Update Area
					REGION_INIT(&hackScreen, &updateRegion, NullBox, 0);
					REGION_INTERSECT(&hackScreen, &updateRegion, &cl->modifiedRegion, &cl->requestedRegion);
					haveUpdate = REGION_NOTEMPTY(&hackScreen, &updateRegion);

					REGION_UNINIT(&hackScreen, &updateRegion);
				}
				else {
					/*  If we've turned off deferred updating
					We are going to send an update as soon as we have a requested,
					regardless of if we have a "change" intersection */
					haveUpdate = TRUE;
				}

				if (rfbShouldSendNewCursor(cl))
					haveUpdate = TRUE;
				else if (rfbShouldSendNewPosition(cl))
					// Could Compare with the request area but for now just always send it
					haveUpdate = TRUE;
				else if (cl->needNewScreenSize)
					haveUpdate = TRUE;
			}

			if (!haveUpdate)
				pthread_cond_wait(&cl->updateCond, &cl->updateMutex);
        }

        // OK, now, to save bandwidth, wait a little while for more updates to come along.
        /* REDSTONE - Lets send it right away if no rfbDeferUpdateTime */
/*      
		Dimdim - No deferred updates. We will take care of this internally -- Bharat Varma
		if (rfbDeferUpdateTime > 0 && !cl->immediateUpdate && !cl->needNewScreenSize) 
		{
            pthread_mutex_unlock(&cl->updateMutex);
            usleep(rfbDeferUpdateTime * 1000);
            pthread_mutex_lock(&cl->updateMutex);
        }

*/        /* Now, get the region we're going to update, and remove
            it from cl->modifiedRegion _before_ we send the update.
            That way, if anything that overlaps the region we're sending
            is updated, we'll be sure to do another update later. */
        REGION_INIT(&hackScreen, &updateRegion, NullBox, 0);
        REGION_INTERSECT(&hackScreen, &updateRegion, &cl->modifiedRegion, &cl->requestedRegion);
        REGION_SUBTRACT(&hackScreen, &cl->modifiedRegion, &cl->modifiedRegion, &updateRegion);
        /* REDSTONE - We also want to clear out the requested region, so we don't process
            graphic updates in previously requested regions */
        REGION_UNINIT(&hackScreen, &cl->requestedRegion);
        REGION_INIT(&hackScreen, &cl->requestedRegion,NullBox,0);

        /*  This does happen but it's asynchronous (and slow to occur)
			what we really want to happen is to just temporarily hide the cursor (while sending to the remote screen)
			-- It's not even usually there (as it's handled by the display driver - but under certain occasions it does appear
         displayErr = CGDisplayHideCursor(displayID);
         if (displayErr != 0)
         rfbLog("Error Hiding Cursor %d", displayErr);
         CGDisplayMoveCursorToPoint(displayID, CGPointZero);
											*/

        /* Now actually send the update. */
		

        rfbSendFramebufferUpdate(cl, updateRegion);
		if (1 != flushQueue(cl, [[dimdimProfile valueForKey:@"BlockSize"] intValue] *1024))
		{
			pthread_mutex_unlock(&cl->updateMutex);
			rfbLog("Unable to send packet to reflector\n");
			[msgClient messageToServer:@"{screencastResult:\"3\"}" from:@"dsc" to:@"wkplugin"];
			[msgClient messageToServer:@"idle" from:@"dsc" to:@"Screencaster"];
			rfbShutdown();
			exit(1);
		}
		
        /* If we were hiding it before make it reappear now
            displayErr = CGDisplayShowCursor(displayID);
        if (displayErr != 0)
            rfbLog("Error Showing Cursor %d", displayErr);
        */

        REGION_UNINIT(&hackScreen, &updateRegion);
        pthread_mutex_unlock(&cl->updateMutex);
    }
	
	[fbPool release];

    return NULL;
}

BOOL initMessagingClient()
{
	rfbLog("Registering messaging client\n");
	msgClient = [[DimdimMessageClient alloc] init];
	return [msgClient registerWithServer];
}

void releaseMessagingClient()
{
	rfbLog("Releasing messaging client\n");
	if ([msgClient isConnected] == YES)
	{
		[msgClient deregisterFromServer];
	}
	
	[msgClient release];
}

void *clientInput(void *data) {
    rfbClientPtr cl = (rfbClientPtr)data;
    pthread_t output_thread;

    pthread_create(&output_thread, NULL, clientOutput, (void *)cl);
	
	NSAutoreleasePool *inPool = [[NSAutoreleasePool alloc] init];

    while (1) {
        bundlesPerformSelector(@selector(rfbReceivedClientMessage));
        rfbProcessClientMessage(cl);

        // Some people will connect but not request screen updates - just send events, this will delay registering the CG callback until then
        if (rfbShouldSendUpdates && !registered && REGION_NOTEMPTY(&hackScreen, &cl->requestedRegion)) {
            rfbLog("Client Connected - Registering Screen Update Notification\n");
            CGError result = CGRegisterScreenRefreshCallback(refreshCallback, NULL);
			if (result != kCGErrorSuccess) {
				NSLog(@"Error (%d) registering for Screen Update Notification", result);
			}
			bundlesPerformSelector(@selector(rfbConnect));
			//CGScreenRegisterMoveCallback(screenUpdateMoveCallback, NULL);
            registered = TRUE;
        }
        
        if (cl->sock == -1) {
            /* Client has disconnected. */
            break;
        }
    }
	
	[inPool release];

    /* Get rid of the output thread. */
    //pthread_mutex_lock(&cl->updateMutex);
    pthread_cond_signal(&cl->updateCond);
    //pthread_mutex_unlock(&cl->updateMutex);
    pthread_join(output_thread, NULL);

    rfbClientConnectionGone(cl);

    return NULL;
}

void connectReverseClient(char *hostName, int portNum) {
    pthread_t client_thread;
    rfbClientPtr cl;
	
    rfbUndim();
    cl = rfbReverseConnection(hostName);
	if (cl) {	
		pthread_create(&client_thread, NULL, clientInput, (void *)cl);
	}
}

char *rfbGetFramebuffer(void) {
	int maxWait =  5000000;
	int retryWait = 500000;
	
	char *returnValue = CGDisplayBaseAddress(displayID);
	while (!returnValue && maxWait > 0) {
		usleep(retryWait); // Buffer goes away while screen is "switching", it'll be back
		maxWait -= retryWait;
		if ([[NSProcessInfo processInfo] respondsToSelector:@selector(CGMainDisplayID)]) {
			displayID = [[NSProcessInfo processInfo] CGMainDisplayID];
			//NSLog(@"Loading New DisplayID: %d", displayID);
		}
		returnValue = CGDisplayBaseAddress(displayID);
	}
	if (!returnValue) {
		exit(1);
	}
		
	return returnValue;
}

static bool rfbScreenInit(void) {
	int bitsPerSample = 8;

	if (floor(NSAppKitVersionNumber) <= floor(NSAppKitVersionNumber10_3))
		(void) GetMainDevice();
	
	// This is defined in the Jaguar Bundle so that we can do this on 10.2+ but still be compatible to 10.1
	if ([[NSProcessInfo processInfo] respondsToSelector:@selector(CGMainDisplayID)])
		displayID = [[NSProcessInfo processInfo] CGMainDisplayID];
	else
		displayID = ((CGDirectDisplayID)0);
	
    // necessary to init the display manager,
    
	// otherwise CGDisplayBitsPerPixel doesn't
    // always works correctly after a resolution change
	bitsPerSample = CGDisplayBitsPerSample(displayID);
	
    if (CGDisplaySamplesPerPixel(displayID) != 3) {
        rfbLog("screen format not supported.\n");
		return FALSE;
    }

	rfbScreen.width = CGDisplayPixelsWide(displayID);
	rfbScreen.height = CGDisplayPixelsHigh(displayID);
	rfbScreen.bitsPerPixel = CGDisplayBitsPerPixel(displayID);
	rfbScreen.depth = CGDisplaySamplesPerPixel(displayID) * bitsPerSample;
	rfbScreen.paddedWidthInBytes = CGDisplayBytesPerRow(displayID);

    rfbServerFormat.bitsPerPixel = rfbScreen.bitsPerPixel;
    rfbServerFormat.depth = rfbScreen.depth;
	rfbServerFormat.trueColour = TRUE;
	
	rfbServerFormat.redMax = (1 << bitsPerSample) - 1;
	rfbServerFormat.greenMax = (1 << bitsPerSample) - 1;
	rfbServerFormat.blueMax = (1 << bitsPerSample) - 1;
	
	if (littleEndian)
		rfbLog("Running in Little Endian\n");
	else
		rfbLog("Running in Big Endian\n");

	rfbServerFormat.bigEndian = !littleEndian;
	rfbServerFormat.redShift = bitsPerSample * 2;
	rfbServerFormat.greenShift = bitsPerSample * 1;
	rfbServerFormat.blueShift = bitsPerSample * 0;

    /* We want to use the X11 REGION_* macros without having an actual
        X11 ScreenPtr, so we do this.  Pretty ugly, but at least it lets us
        avoid hacking up regionstr.h, or changing every call to REGION_* */
    hackScreen.RegionCreate = miRegionCreate;
    hackScreen.RegionInit = miRegionInit;
    hackScreen.RegionCopy = miRegionCopy;
    hackScreen.RegionDestroy = miRegionDestroy;
    hackScreen.RegionUninit = miRegionUninit;
    hackScreen.Intersect = miIntersect;
    hackScreen.Union = miUnion;
    hackScreen.Subtract = miSubtract;
    hackScreen.Inverse = miInverse;
    hackScreen.RegionReset = miRegionReset;
    hackScreen.TranslateRegion = miTranslateRegion;
    hackScreen.RectIn = miRectIn;
    hackScreen.PointInRegion = miPointInRegion;
    hackScreen.RegionNotEmpty = miRegionNotEmpty;
    hackScreen.RegionEmpty = miRegionEmpty;
    hackScreen.RegionExtents = miRegionExtents;
    hackScreen.RegionAppend = miRegionAppend;
    hackScreen.RegionValidate = miRegionValidate;
	
	return TRUE;
}

static void usage(void) {
    fprintf(stderr, "\nAvailable options:\n\n");

    fprintf(stderr, "-rfbport port          TCP port for RFB protocol (0=autodetect first open port 5900-5909)\n");
    fprintf(stderr, "-rfbwait time          Maximum time in ms to wait for RFB client\n");
    fprintf(stderr, "-rfbnoauth             Run the server with NO password protection\n");
    fprintf(stderr, "-rfbauth passwordFile  Use this password file for VNC authentication\n");
	fprintf(stderr, "                       (use 'storepasswd' to create a password file)\n");
    fprintf(stderr, "-maxauthattempts num   Maximum Number of auth tries before disabling access from a host\n");
	fprintf(stderr, "                       (default: 5), zero disables\n");
    fprintf(stderr, "-deferupdate time      Time in ms to defer updates (default %d)\n", rfbDeferUpdateTime);
    fprintf(stderr, "-desktop name          VNC desktop name (default \"MacOS X\")\n");
    fprintf(stderr, "-alwaysshared          Always treat new clients as shared\n");
    fprintf(stderr, "-nevershared           Never treat new clients as shared\n");
    fprintf(stderr, "-dontdisconnect        Don't disconnect existing clients when a new non-shared\n");
	fprintf(stderr, "                       connection comes in (refuse new connection instead)\n");
    fprintf(stderr, "-nodimming             Never allow the display to dim\n");
	fprintf(stderr, "                       (default: display can dim, input undims)\n");
    fprintf(stderr, "-maxdepth bits         Maximum allowed bit depth for connecting clients (32,16,8).\n");
	fprintf(stderr, "                       (default: bit depth of display)\n");
    /*
     fprintf(stderr, "-reversemods           reverse the interpretation of control\n");
     fprintf(stderr, "                       and command (for windows clients)\n");
     */
    fprintf(stderr, "-allowsleep            Allow machine to sleep\n");
	fprintf(stderr, "                       (default: sleep is disabled)\n");
    fprintf(stderr, "-disableScreenSaver    Disable screen saver while users are connected\n");
	fprintf(stderr, "                       (default: no, allow screen saver to engage)\n");
	fprintf(stderr, "-swapButtons           Swap mouse buttons 2 & 3\n");
	fprintf(stderr, "                       (default: YES)\n");
	fprintf(stderr, "-dontswapButtons       Disable swap mouse buttons 2 & 3\n");
	fprintf(stderr, "                       (default: NO)\n");
	fprintf(stderr, "-disableRemoteEvents   Ignore remote keyboard, pointer, and clipboard event\n");
	fprintf(stderr, "                       (default: no, process them)\n");
	fprintf(stderr, "-disableRichClipboards Don't share rich clipboard events\n");
	fprintf(stderr, "                       (default: no, process them)\n");
	fprintf(stderr, "-connectHost host      Host Name or IP of listening client to establishing a reverse conneect\n");
	fprintf(stderr, "-connectPort port      TCP port of listening client to establishing a reverse conneect\n");
	fprintf(stderr, "                       (default: 5500)\n");
	fprintf(stderr, "-noupdates             Prevent registering for screen updates, for use with x2vnc or win2vnc\n");
	fprintf(stderr, "-protocol protocol     Force a particular protocol version (eg 3.3)\n");
	fprintf(stderr, "                       (default:" rfbProtocolVersionFormat ")", rfbProtocolMajorVersion, rfbProtocolMinorVersion);
	fprintf(stderr, "-bigEndian             Force Big-Endian mode (PPC)\n");
	fprintf(stderr, "                       (default: detect)\n");
	fprintf(stderr, "-littleEndian          Force Little-Endian mode (INTEL)\n");
	fprintf(stderr, "                       (default: detect)\n");
	
    /* This isn't ready to go yet
    {
        CGDisplayCount displayCount;
        CGDirectDisplayID activeDisplays[100];
        int index = 0;

        fprintf(stderr, "-display DisplayID     displayID to indicate which display to serve\n");

        CGGetActiveDisplayList(100, activeDisplays, &displayCount);

        for (index=0; index < displayCount; index++)
            fprintf(stderr, "\t\t%d = (%ld,%ld)\n", index, CGDisplayPixelsWide(activeDisplays[index]), CGDisplayPixelsHigh(activeDisplays[index]));
    }
    */
    fprintf(stderr, "-localhost             Only allow connections from the same machine, literally localhost (127.0.0.1)\n");
    fprintf(stderr, "                       If you use SSH and want to stop non-SSH connections from any other hosts \n");
    fprintf(stderr, "                       (default: no, allow remote connections)\n");
    fprintf(stderr, "-restartonuserswitch flag  For Use on Panther 10.3 systems, this will cause the server to restart when a fast user switch occurs");
    fprintf(stderr, "                       (default: no)\n");
    bundlesPerformSelector(@selector(rfbUsage));
    fprintf(stderr, "\n");

    exit(255);
}

static void checkForUsage(int argc, char *argv[]) {
    int i;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-h", 2) == 0 ||
            strncmp(argv[i], "-H", 2) == 0 ||
            strncmp(argv[i], "--h", 3) == 0 ||
            strncmp(argv[i], "--H", 3) == 0 ||
            strncmp(argv[i], "-usage", 6) == 0 ||
            strncmp(argv[i], "--usage", 7) == 0 ||
            strcmp(argv[i], "-?") == 0) {
            loadDynamicBundles(FALSE);
            usage();
        }
    }
}

static void processArguments(int argc, char *argv[]) {
	char argString[1024] = "Arguments: ";
    int i, j;
	
    for (i = 1; i < argc; i++) {
		strcat(argString, argv[i]);
		strcat(argString, " ");
	}
	strcat(argString, "\n");
	rfbLog(argString);

    for (i = 1; i < argc; i++) {
		// Lowercase it
		for (j=0;j<strlen(argv[i]);j++)
			argv[i][j] = tolower(argv[i][j]);
		
        if (strcmp(argv[i], "-rfbport") == 0) { // -rfbport port
            if (i + 1 >= argc) usage();
            rfbPort = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-protocol") == 0) { // -rfbport port
			double protocol;
            if (i + 1 >= argc) usage();
            protocol = atof(argv[++i]);
			rfbProtocolMajorVersion = MIN(rfbProtocolMajorVersion, floor(protocol));
			protocol = protocol-floor(protocol); // Now just the fractional part
												 // Ok some folks think of it as 3.3 others as 003.003, so let's repeat...
			while (protocol > 0 && protocol < 1)
				protocol *= 10;
			rfbProtocolMinorVersion = MIN(rfbProtocolMinorVersion, rint(protocol));
			rfbLog("Forcing: " rfbProtocolVersionFormat,rfbProtocolMajorVersion, rfbProtocolMinorVersion);
        } else if (strcmp(argv[i], "-rfbwait") == 0) {  // -rfbwait ms
            if (i + 1 >= argc) usage();
            rfbMaxClientWait = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-rfbnoauth") == 0) {
			allowNoAuth = TRUE;
			rfbLog("Warning: No Auth specified, running with no password protection");
        } else if (strcmp(argv[i], "-rfbauth") == 0) {  // -rfbauth passwd-file
            if (i + 1 >= argc) usage();
            rfbAuthPasswdFile = argv[++i];
        } else if (strcmp(argv[i], "-maxauthattempts") == 0) {  
            if (i + 1 >= argc) usage();
            rfbMaxLoginAttempts = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-connecthost") == 0) {  // -connect host
            if (i + 1 >= argc) usage();
			strncpy(reverseHost, argv[++i], 255);
            if (strlen(reverseHost) == 0) usage();
        } else if (strcmp(argv[i], "-connectport") == 0) {  // -connect host
            if (i + 1 >= argc) usage();
            reversePort = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-deferupdate") == 0) {  // -deferupdate ms
            if (i + 1 >= argc) usage();
            rfbDeferUpdateTime = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-maxdepth") == 0) {  // -maxdepth
            if (i + 1 >= argc) usage();
            rfbMaxBitDepth = atoi(argv[++i]);
			switch (rfbMaxBitDepth) {
				case 24:
					rfbMaxBitDepth = 32;
					break;
				case 32:
				case 16:
				case 8:
					break;
				default:
					rfbLog("Invalid maxDepth");
					exit(-1);
					break;
			}
        } else if (strcmp(argv[i], "-desktop") == 0) {  // -desktop desktop-name
            if (i + 1 >= argc) usage();
			strncpy(desktopName, argv[++i], 255);
        } else if (strcmp(argv[i], "-display") == 0) {  // -display DisplayID
            CGDisplayCount displayCount;
            CGDirectDisplayID activeDisplays[100];
			
            CGGetActiveDisplayList(100, activeDisplays, &displayCount);
			
            if (i + 1 >= argc || atoi(argv[i+1]) >= displayCount)
                usage();
			
            displayID = activeDisplays[atoi(argv[++i])];
        } else if (strcmp(argv[i], "-alwaysshared") == 0) {
            rfbAlwaysShared = TRUE;
        } else if (strcmp(argv[i], "-nevershared") == 0) {
            rfbNeverShared = TRUE;
        } else if (strcmp(argv[i], "-dontdisconnect") == 0) {
            rfbDontDisconnect = TRUE;
        } else if (strcmp(argv[i], "-nodimming") == 0) {
            rfbNoDimming = TRUE;
        } else if (strcmp(argv[i], "-allowsleep") == 0) {
            rfbNoSleep = FALSE;
        } else if (strcmp(argv[i], "-reversemods") == 0) {
            rfbReverseMods = TRUE;
        } else if (strcmp(argv[i], "-disablescreensaver") == 0) {
            rfbDisableScreenSaver = TRUE;
        } else if (strcmp(argv[i], "-swapbuttons") == 0) {
            rfbSwapButtons = TRUE;
        } else if (strcmp(argv[i], "-dontswapbuttons") == 0) {
            rfbSwapButtons = FALSE;
        } else if (strcmp(argv[i], "-disableremoteevents") == 0) {
            rfbDisableRemote = TRUE;
        } else if (strcmp(argv[i], "-disablerichclipboards") == 0) {
            rfbDisableRichClipboards = TRUE;
        } else if (strcmp(argv[i], "-localhost") == 0) {
            rfbLocalhostOnly = TRUE;
        } else if (strcmp(argv[i], "-inhibitevents") == 0) {
            rfbInhibitEvents = TRUE;
		} else if (strcmp(argv[i], "-noupdates") == 0) {
			rfbShouldSendUpdates = FALSE;
		} else if (strcmp(argv[i], "-littleendian") == 0) {
			littleEndian = TRUE;
		} else if (strcmp(argv[i], "-bigendian") == 0) {
			littleEndian = FALSE;
		} else if (strcmp(argv[i], "-ipv6") == 0) { // Ok so the code to enable is in the Bundle, but this disables 4
			useIP4 = FALSE;
		} else if (strcmp(argv[i], "-keepregistration") == 0) {
			unregisterWhenNoConnections = FALSE;
		} else if (strcmp(argv[i], "-dontkeepregistration") == 0) {
			unregisterWhenNoConnections = TRUE;
		} else if (strcmp(argv[i], "-restartonuserswitch") == 0) {
			if (i + 1 >= argc) 
				usage();
			else {
				char *argument = argv[++i];
				restartOnUserSwitch = (argument[0] == 'y' || argument[0] == 'Y' || argument[0] == 't' || argument[0] == 'T' || atoi(argument));
			}
		}
	}
	
	/* Safe defaults for Dimdim -- Bharat Varma */
	
	// Force protocol version to 3.3, because that is what reflector supports
	
	rfbProtocolMajorVersion = 3;
	rfbProtocolMinorVersion = 3;
	
	// Disable clipboard and remote events.

	rfbDisableRemote = TRUE;
	rfbDisableRichClipboards = TRUE;
	
	/* Safe defaults for Dimdim -- Bharat Varma */

	if (!rfbAuthPasswdFile && !allowNoAuth && !reverseHost) {
		rfbLog("ERROR: No authentication specified, use -rfbauth passwordfile OR -rfbnoauth");
		exit (255);
	}
}

void rfbShutdown() {
	/* Clean up Dimdim specific data -- Bharat Varma */
	rfbLog("disconnecting stream\n");
	closeAllClients();
	rfbLog("stream disconnected\n");

	fclose(binaryDump);	
	releaseMessagingClient();
	
	/* Clean up Dimdim specific data -- Bharat Varma */
	
//    bundlesPerformSelector(@selector(rfbShutdown));
//    [bundleArray release];

    CGUnregisterScreenRefreshCallback(refreshCallback, NULL);
	
    //CGDisplayShowCursor(displayID);
    rfbDimmingShutdown();

    if (rfbDisableScreenSaver) {
        /* remove the screensaver timer */
        RemoveEventLoopTimer(screensaverTimer);
        DisposeEventLoopTimerUPP(screensaverTimerUPP);
    }
		
    rfbLog("RFB Shudown Complete\n");
	rfbLog("Screencaster shutdown\n");
	
	pthread_mutex_lock(&logMutex);
	fclose(logDump);
	pthread_mutex_unlock(&logMutex);
	
}
	
static void rfbShutdownOnSignal(int signal) {
    rfbLog("OSXvnc-server received signal: %d\n", signal);
	[msgClient messageToServer:@"{screencastResult:\"2\"}" from:@"dsc" to:@"wkplugin"];
	[msgClient messageToServer:@"idle" from:@"dsc" to:@"Screencaster"];

    rfbShutdown();
	
	[dimdimProfile release];

	if (signal == SIGTERM)
		exit (0);
	else
		exit (signal);
}

void daemonize( void ) {
    int i;

    // Fork New Process
    if ( fork() != 0 )
        exit( 0 );

    // Become session leader
    setsid();

    // Ignore signals here
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
	// Shutdown on these
    signal(SIGTERM, rfbShutdownOnSignal);
    signal(SIGINT, rfbShutdownOnSignal);
    signal(SIGQUIT, rfbShutdownOnSignal);
    
    // Fork a second new process
    if ( fork( ) != 0 )
        exit( 0 );

    // chdir ( "/" );
    umask( 0 );

    // Close open FDs
    for ( i = getdtablesize( ) - 1; i > STDERR_FILENO; i-- )
        close( i );

    /* from this point on we should only send output to server log or syslog */
}

BOOL runningLittleEndian ( void ) {	
	return (CFByteOrderGetCurrent() == CFByteOrderLittleEndian);
	/*
	 // rosetta is so complete that it obsucres even CFByteOrderGetCurrent
    int hasMMX = 0;
    size_t length = sizeof(hasMMX);
	 // No Error and it does have MMX
	 return (!sysctlbyname("hw.optional.mmx", &hasMMX, &length, NULL, 0) && hasMMX);
	 */
}
	
int main(int argc, char *argv[]) {
	NSLog(@"Screencaster Started\n");
	NSAutoreleasePool* tempPool = [[NSAutoreleasePool alloc] init];
	
	/* Initializing Dimdim specific data -- Bharat Varma */
	rfbQueue = [[dVNCQueue alloc] init];
	
	// Load Dimdim Profile
	NSString* cfgPath = [[NSString alloc] initWithFormat:@"/Applications/Dimdim/Screencaster.app/Contents/MacOS/DesktopShare.cfg"];
	dimdimProfile = [[NSDictionary alloc] initWithContentsOfFile:cfgPath];
	[cfgPath release];
	
	[CurlClientData getInstance]->m_maxRetries = [[dimdimProfile valueForKey:@"MaxRetries"] intValue];
	[CurlClientData getInstance]->m_maxWaitTime = [[dimdimProfile valueForKey:@"MaxWaitTime"] intValue];
	
	// Open log and dump files in $HOME/.Dimdim
	
	binaryDump = NULL;
	logDump = NULL;

	char binpath[64];
	[DimdimHelpers EnumerateFileLocation:"dump.bin" filepath:binpath];
	binaryDump = fopen(binpath, "wb");
	
	char logpath[64];
	[DimdimHelpers EnumerateFileLocation:"screencaster.log" filepath:logpath];
	logDump = fopen(logpath, "w");	
	
	rfbLog("Screencaster version 4.0 build 090808\n");
	
	// Startup the messaging client
	if (YES != initMessagingClient())
	{
		rfbLog("Unable to register for messaging support\n");
		rfbShutdown();
		[tempPool release];
		return 0;
	}
	
	
	/* Initializing Dimdim specific data -- Bharat Varma */
	
	littleEndian = runningLittleEndian();
    checkForUsage(argc,argv);
    
	// The bug with unregistering from user updates may have been fixed in 10.4 Tiger
	if (floor(NSAppKitVersionNumber) > floor(NSAppKitVersionNumber10_3))
		unregisterWhenNoConnections = TRUE;
	
    // This guarantees separating us from any terminal - 
	// Right now this causes problems with the keep-alive script and the GUI app (since it causes the process to return right away)
	// it allows you to survive when launched in SSH, etc but doesn't solves the problem of being killed on GUI logout.
	// It also doesn't help with any of the pasteboard security issues, those requires secure sessionID's, see:
	// http://developer.apple.com/documentation/MacOSX/Conceptual/BPMultipleUsers/index.html
	// 
	// daemonize();

    // Let's not shutdown on a SIGHUP at some point perhaps we can use that to reload configuration
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, rfbShutdownOnSignal);
    signal(SIGINT, rfbShutdownOnSignal);
    signal(SIGQUIT, rfbShutdownOnSignal);

    pthread_mutex_init(&logMutex, NULL);
    pthread_cond_init(&listenerGotNewClient, NULL);

    loadKeyTable();		// Keyboard mappings

	[[NSUserDefaults standardUserDefaults] addSuiteNamed:@"com.redstonesoftware.VineServer"];
	
    processArguments(argc, argv);

//	loadDynamicBundles(TRUE);
	
	// If no Desktop Name Provided Try to Get it
	if (strlen(desktopName) == 0) {
		gethostname(desktopName, 256);
	}
	
	if (!rfbScreenInit())
		exit(1);

    rfbClientListInit();
    rfbDimmingInit();
	rfbAuthInit();
    
	
    // Does this need to be in 10.1 and greater (does any of this stuff work in 10.0?)
    if (!rfbInhibitEvents) {
        //NSLog(@"Core Graphics - Event Suppression Turned Off");
        // This seems to actually sometimes inhibit REMOTE events as well, but all the same let's let everything pass through for now
        CGSetLocalEventsFilterDuringSupressionState(kCGEventFilterMaskPermitAllEvents, kCGEventSupressionStateSupressionInterval);
        CGSetLocalEventsFilterDuringSupressionState(kCGEventFilterMaskPermitAllEvents, kCGEventSupressionStateRemoteMouseDrag);		
    }
	// Better to handle this at the event level, see kbdptr.c
	//CGEnableEventStateCombining(FALSE);

    if (rfbDisableScreenSaver) {
        /* setup screen saver disabling timer */
        screensaverTimerUPP = NewEventLoopTimerUPP(rfbScreensaverTimer);
        InstallEventLoopTimer(GetMainEventLoop(),
                              kEventDurationSecond * 30,
                              kEventDurationSecond * 30,
                              screensaverTimerUPP,
                              NULL,
                              &screensaverTimer);
    }

	nonBlocking = [[NSUserDefaults standardUserDefaults] boolForKey:@"NonBlocking"];
	
	if (strlen(reverseHost) > 0)
		connectReverseClient(reverseHost, reversePort);
	
    // This segment is what is responsible for causing the server to shutdown when a user logs out
    // The problem being that OS X sends it first a SIGTERM and then a SIGKILL (un-trappable)
    // Presumable because it's running a Carbon Event loop
    if (1) {
        OSStatus resultCode = 0;
		
        while (keepRunning) {
            // No Clients - go into hibernation
            if (!rfbClientsConnected()) {
				
				// You would think that there is no point in getting screen updates with no clients connected
				// But it seems that unregistering but keeping the process (or event loop) around can cause a stuttering behavior in OS X.
				if (registered && unregisterWhenNoConnections) {
					rfbLog("UnRegistering Screen Update Notification - waiting for clients\n");
					CGUnregisterScreenRefreshCallback(refreshCallback, NULL);
					bundlesPerformSelector(@selector(rfbDisconnect));
					registered = NO;
				}
				else
				{
					// For Dimdim, we don't wait for any more clients -- Bharat Varma
					// Break out of the loop and shutdown.
					break;
				}
			}
            bundlesPerformSelector(@selector(rfbPoll));

            rfbCheckForCursorChange();
//            rfbCheckForScreenResolutionChange();
            // Run The Event loop a moment to see if we have a screen update or NSNotification
            // No better luck with RunApplicationEventLoop() avoiding the shutdown on logout problem
            resultCode = RunCurrentEventLoop(kEventDurationSecond/30); //EventTimeout
            if (resultCode != eventLoopTimedOutErr) {
                rfbLog("Received Result: %d during event loop, Shutting Down", resultCode);
                keepRunning = NO;
            }
        }
    }
    else while (1) {
        // So this looks like it should fix it but I get no response on the CGWaitForScreenRefreshRect....
        // It doesn't seem to get called at all when not running an event loop
        CGRectCount rectCount;
        CGRect *rectArray;
        CGEventErr result;
		
        result = CGWaitForScreenRefreshRects( &rectArray, &rectCount );
        refreshCallback(rectCount, rectArray, NULL);
        CGReleaseScreenRefreshRects( rectArray );
    };
		
	[msgClient messageToServer:@"{screencastResult:\"2\"}" from:@"dsc" to:@"wkplugin"];
	[msgClient messageToServer:@"idle" from:@"dsc" to:@"Screencaster"];
	rfbShutdown();
	
	[tempPool release];
	
    return 0;
}
