//
//  TigerExtensions.m
//  OSXvnc
//
//  Created by Jonathan Gillaspie on Fri Jul 11 2003.
//  Copyright (c) 2003 RedstoneSoftware, Inc. All rights reserved.

#import "JaguarExtensions.h"
#import "TigerExtensions.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>

#import <Security/AuthSession.h>

#include "keysymdef.h"
#include "kbdptr.h"

#include "rfb.h"

#include "rfbserver.h"
#import "VNCServer.h"

#import "../RFBBundleProtocol.h"

static BOOL readyToStartup = TRUE;
static BOOL dynamicKeyboard = FALSE;
static CGEventSourceRef vncSourceRef=NULL;
static CGEventTapLocation vncTapLocation=0;
static KeyboardLayoutRef unicodeLayout = NULL; //kKeyboardISO

static unsigned long oldKeyboardScript;
static KeyboardLayoutRef currentKeyboardLayout;

// Current Modifiers reflect the "global" state of the modifier keys (not a particular VNC connection)
// This only matters for event taps.  Ideally we could detect their values but
// that seems to require an active event loop.
static CGEventFlags currentModifiers;

// These correspond to the keycodes of the keys 0-9,A-F
static unsigned char unicodeNumbersToKeyCodes[16] = { 29, 18, 19, 20, 21, 23, 22, 26, 28, 25, 0, 11, 8, 2, 14, 3 };

#ifndef NSAppKitVersionNumber10_3
#define NSAppKitVersionNumber10_3 743
#endif
#ifndef NSAppKitVersionNumber10_4
#define NSAppKitVersionNumber10_4 824
#endif

@implementation TigerExtensions

// Private Helper Functions
inline void setKeyModifiers(CGEventFlags modifierFlags);
inline void sendKeyEvent(CGKeyCode keyCode, Bool down, CGEventFlags modifierFlags);
void SyncSetKeyboardLayout (unsigned long keyboardScript, KeyboardLayoutRef newLayout);
bool isConsoleSession();

// The Keycodes to various modifiers on the current keyboard
CGKeyCode keyCodeShift;
CGKeyCode keyCodeOption;
CGKeyCode keyCodeControl;
CGKeyCode keyCodeCommand;

rfbserver *theServer;

// This routine waits for the window server to register its per-session 
// services in our session.  This code was necessary in various pre-release 
// versions of Mac OS X 10.5, but it is not necessary on the final version. 
static void WaitForWindowServerSession(void) {
    CFDictionaryRef dict;
    int delay = 100000, maxDelay = 5000000;
	
	dict = CGSessionCopyCurrentDictionary();
	while (dict == NULL && maxDelay > 0) {
		usleep(delay);
		maxDelay -= delay;
        dict = CGSessionCopyCurrentDictionary();
	}
	if (maxDelay <= 0)
		NSLog(@"No CG session Available, max delay reached");
	if (dict != NULL)
		CFRelease(dict);
}

bool isConsoleSession() {
	BOOL returnValue = FALSE;
	CFDictionaryRef sessionInfoDict = CGSessionCopyCurrentDictionary();
		
	if (sessionInfoDict == NULL) 
		NSLog(@"Unable to get session dictionary.");
	else {
		CFBooleanRef userIsActive = CFDictionaryGetValue(sessionInfoDict, kCGSessionOnConsoleKey);
		returnValue = CFBooleanGetValue(userIsActive);
		CFRelease(sessionInfoDict);
	}
	
//	if (0) { 
//		// This one succeeds in "off-screen acounts" also
//		SecuritySessionId mySession;
//		SessionAttributeBits sessionInfo;
//		OSStatus error = SessionGetInfo(callerSecuritySession, &mySession, &sessionInfo);
//		
//		returnValue = (sessionInfo & sessionHasGraphicAccess);
//	}
//	else if (0) {
//		// There must be a better way but for now this seems to indicate if we are a console session or not
//		// at least for logged in users -- it always returns NO for the login window
//		CGEventSourceRef testRef = CGEventSourceCreate(kCGEventSourceStatePrivate);
//		int pollDelay = 0; // No poll at this time, just look once
//		
//		while (!testRef && pollDelay) {
//			usleep(100000);
//			pollDelay -= 100000;
//			testRef = CGEventSourceCreate(kCGEventSourceStatePrivate);
//		}
//	
//		if (testRef != NULL) {
//			returnValue = TRUE;
//			CFRelease(testRef);
//		}
//	}
	
	return returnValue;
}

+ (void) rfbStartup: (rfbserver *) aServer {
	[[NSUserDefaults standardUserDefaults] registerDefaults:[NSDictionary dictionaryWithObjectsAndKeys:
		@"NO", @"UnicodeKeyboard", // Load The Unicode Keyboard
		@"NO", @"DynamicKeyboard", // Try to set the keyboard "as we need it", doesn't work well on Tiger
		@"-1", @"UnicodeKeyboardIdentifier", // ID of the Unicode Keyboard resource to use (-1 is Apple's)

		@"2", @"EventSource", // Always private event source so we don't consolidate with existing keys (however HID for the EventTap always does anyhow)
		@"3", @"EventTap", // Default Event Tap (3=HID for Console User and Session For OffScreen Users)
		@"NO", @"SystemServer", 
		nil]];

    theServer = aServer;
	*(id *)(theServer->alternateKeyboardHandler) = self;

	// 10.5 System Server special behavior
	if (floor(NSAppKitVersionNumber) > floor(NSAppKitVersionNumber10_4) &&
		[[NSUserDefaults standardUserDefaults] boolForKey:@"SystemServer"]) {
		// On 10.5 we need quit if the user switches out (so we can relinquish the port)
		if (isConsoleSession()) {
			[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
																   selector:@selector(systemServerShouldQuit:)
																	   name: NSWorkspaceSessionDidResignActiveNotification
																	 object:nil];			
		}
		// On 10.5 we need to be able to "hold" if we aren't the console session
		else {
			[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
																   selector:@selector(systemServerShouldContinue:)
																	   name: NSWorkspaceSessionDidBecomeActiveNotification
																	 object:nil];
			readyToStartup = NO;
			// Run Loop
			NSLog(@"System Server for non-console session, pausing until we receive console access");
			while (!readyToStartup) {
				OSStatus resultCode = RunCurrentEventLoop(kEventDurationSecond); //EventTimeout
				if (resultCode != eventLoopTimedOutErr) {
					NSLog(@"Received Result: %d during event loop, Shutting Down", resultCode);
					//rfbShutdown();
					exit(0);
				}
			}
		}
	}

	[self loadUnicodeKeyboard];
}

+ (void) systemServerShouldQuit: (NSNotification *) aNotification {
    NSLog(@"User Switched Out, Stopping System Server - %@", [aNotification name]);
	//rfbShutdown();
	exit(0);
	return;
}
+ (void) systemServerShouldContinue: (NSNotification *) aNotification {
    NSLog(@"User Switched In, Starting System Server - %@", [aNotification name]);
	readyToStartup = YES;
	return;
}

+ (void) loadKeyboard: (KeyboardLayoutRef) keyboardLayoutRef {
    int i;
    int keysLoaded = 0;
    UCKeyboardLayout *uchrHandle = NULL;
    const void *kchrHandle = NULL;
    CFStringRef keyboardName;
    KeyboardLayoutKind layoutKind;
    static UInt32 modifierKeyStates[] = {0, shiftKey, optionKey, controlKey };
	//{0, shiftKey, optionKey, controlKey, optionKey | shiftKey, optionKey | controlKey, controlKey | shiftKey, optionKey | shiftKey | controlKey};
	
    /* modifiers */
    //cmdKey                        = 1 << cmdKeyBit,
    //shiftKey                      = 1 << shiftKeyBit,
    //alphaLock                     = 1 << alphaLockBit,
    //optionKey                     = 1 << optionKeyBit,
    //controlKey                    = 1 << controlKeyBit,
    
    // KLGetKeyboardLayoutProperty is 10.2 only how do I access these resources in early versions?
    if (keyboardLayoutRef) {
        KLGetKeyboardLayoutProperty(keyboardLayoutRef, kKLName, (const void **) &keyboardName);
        KLGetKeyboardLayoutProperty(keyboardLayoutRef, kKLKind, (const void **) &layoutKind);
        NSLog(@"Keyboard Detected: %@ (Type:%d) - Loading Keys\n", keyboardName, layoutKind);
        if (layoutKind == kKLKCHRuchrKind || layoutKind == kKLuchrKind)
            KLGetKeyboardLayoutProperty(keyboardLayoutRef, kKLuchrData, (const void **) &uchrHandle);
        else
            KLGetKeyboardLayoutProperty(keyboardLayoutRef, kKLKCHRData, (const void **) &kchrHandle);
    }
	
    // Initialize them all to 0xFFFF
    memset(theServer->keyTable, 0xFF, keyTableSize * sizeof(CGKeyCode));
    memset(theServer->keyTableMods, 0xFF, keyTableSize * sizeof(unsigned char));
	
    if (uchrHandle) {
        // Ok - we could get the LIST of Modifier Key States out of the Keyboard Layout
        // some of them are duplicates so we need to compare them, then we'll iterate through them in reverse order
        // UCKeyModifiersToTableNum = ; EventRecord
        // This layout gets a little harry
		
        UInt16 keyCode;
        UInt32 modifierKeyState = 0;
        UInt32 keyboardType = LMGetKbdType();
        UInt32 deadKeyState = 0;
        UniCharCount actualStringLength;
        UniChar unicodeChar;
		
        // Iterate Over Each Modifier Keyset
        for (i=0; i < (sizeof(modifierKeyStates) / sizeof(UInt32)); i++) {
            modifierKeyState = (modifierKeyStates[i] >> 8) & 0xFF;
            //NSLog(@"Loading Keys For Modifer State: %#04x", modifierKeyState);
            // Iterate Over Each Key Code
            for (keyCode = 0; keyCode < 127; keyCode++) {
                OSStatus resultCode = UCKeyTranslate (uchrHandle,
                                                      keyCode,
                                                      kUCKeyActionDown,
                                                      modifierKeyState,
                                                      keyboardType,
                                                      kUCKeyTranslateNoDeadKeysBit,
                                                      &deadKeyState,
                                                      1, // Only 1 key allowed due to VNC behavior
                                                      &actualStringLength,
                                                      &unicodeChar);
				
                if (resultCode == kUCOutputBufferTooSmall) {
                    NSLog(@"Unable To Convert KeyCode, Multiple Characters For: %d (%#04x)",  keyCode, modifierKeyState);
                }
                else if (resultCode == noErr) {
                    // We'll use the FIRST keyCode that we find for that UNICODE character
                    if (theServer->keyTable[unicodeChar] == 0xFFFF) {
                        theServer->keyTable[unicodeChar] = keyCode;
                        theServer->keyTableMods[unicodeChar] = modifierKeyState;
                        keysLoaded++;
                    }
                }
            }
            //NSLog(@"Loaded %d Keys", keysLoaded);
            keysLoaded = 0;
        }
    }
    else if (kchrHandle) {
        UInt32 modifierKeyState = 0;
        UInt16 keyCode;
        UInt32 state=0;
        UInt32 kchrCharacters;
		
        // Ok - we need to get the LIST of Modifier Key States out of the Keyboard Layout
        // some of them are duplicates so we need to compare them, then we'll iterate through them in reverse order
        //UCKeyModifiersToTableNum = ;
        for (i=0; i < (sizeof(modifierKeyStates) / sizeof(UInt32)); i++) {
            modifierKeyState = (modifierKeyStates[i] >> 8) & 0xFF;
            //NSLog(@"Loading Keys For Modifer State:%#04x", modifierKeyState);
			
            // Iterate Over Each Key Code
            for (keyCode = 0; keyCode < 127; keyCode++) {
                // We pass the modifierKeys as the top 8 bits of keycode
                kchrCharacters = KeyTranslate(kchrHandle, (modifierKeyState<<8 | keyCode), &state);
				
                if (kchrCharacters & 0xFFFF0000) {
                    NSLog(@"Unable To Convert KeyCode, Multiple Characters (%#04x) (%#04x) For: %d (%#04x)",
                          kchrCharacters>>16 & 0xFFFF, kchrCharacters & 0xFFFF, keyCode, modifierKeyState);
                }
                else {
                    // We'll use the FIRST keyCode that we find for that UNICODE character
                    if (theServer->keyTable[kchrCharacters & 0xFFFF] == 0xFFFF) {
                        //NSLog(@"KeyCode:%d UniCode:%d", keyCode, kchrCharacters & 0xFFFF);
                        theServer->keyTable[kchrCharacters & 0xFFFF] = keyCode;
                        theServer->keyTableMods[kchrCharacters & 0xFFFF] = modifierKeyState;
                        keysLoaded++;
                    }
                }
            }
            //NSLog(@"Loaded %d Keys", keysLoaded);
            keysLoaded = 0;
        }
    }
    else {
        // This is the old US only keyboard mapping
        // Map the above key table into a static array so we can just look them up directly
        NSLog(@"Unable To Determine Key Map - Reverting to US Mapping\n");
        for (i = 0; i < (sizeof(USKeyCodes) / sizeof(int)); i += 2)
            theServer->keyTable[(unsigned short)USKeyCodes[i]] = (CGKeyCode) USKeyCodes[i+1];
    }
	
    // This is the old SpecialKeyCodes keyboard mapping
    // Map the above key table into a static array so we can just look them up directly
    NSLog(@"Loading %d XKeysym Special Keys\n", (sizeof(SpecialKeyCodes) / sizeof(int))/2);
    for (i = 0; i < (sizeof(SpecialKeyCodes) / sizeof(int)); i += 2) {
        theServer->keyTable[(unsigned short)SpecialKeyCodes[i]] = (CGKeyCode) SpecialKeyCodes[i+1];
	}
}

+ (void) rfbRunning {	
	if ([[NSUserDefaults standardUserDefaults] boolForKey:@"DynamicKeyboard"])
		dynamicKeyboard = TRUE;
	
	// Event Source represents which existing event states should be combined with the incoming events
	switch ([[NSUserDefaults standardUserDefaults] integerForKey:@"EventSource"]) {
		case 2:
			// Doesn't combine with any other sources
			NSLog(@"Using Private Event Source");
			vncSourceRef = CGEventSourceCreate(kCGEventSourceStatePrivate);
			break;
		case 1:
			// Combines with only with other User Session
			NSLog(@"Using Combined Event Source, WARNING: Doesn't work if we FUS off-screen (10.5.1)");
			vncSourceRef = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
			break;
		case 0:
			// Combines with Physical Keyboard
			NSLog(@"Using HID Event Source");
			vncSourceRef = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
			break;
		case 3: // I am pretty sure works similar to the HID
		default:
			vncSourceRef = NULL;
			break;
	}
	if (!vncSourceRef)
		NSLog(@"No Event Source -- Using 10.3 API");	
	
	// Event Taps represent at what level of the input manager the events will be interpretted
	switch ([[NSUserDefaults standardUserDefaults] integerForKey:@"EventTap"]) {
		case 3: {
			if (isConsoleSession()) {
				NSLog(@"Using Dynamic Event Tap -- HID for console user");
				vncTapLocation = kCGHIDEventTap;
			}
			else {
				NSLog(@"Using Dynamic Event Tap -- Session for off-screen user");
				vncTapLocation = kCGSessionEventTap;
			}
			
			[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
																   selector:@selector(userSwitchedIn:)
																	   name: NSWorkspaceSessionDidBecomeActiveNotification
																	 object:nil];
			[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
																   selector:@selector(userSwitchedOut:)
																	   name: NSWorkspaceSessionDidResignActiveNotification
																	 object:nil];			
			break;
		}
		case 2:
			NSLog(@"Using Annotated Session Event Tap");
			vncTapLocation = kCGAnnotatedSessionEventTap;
			break;
		case 1:
			// At this level will can passed in modifiers
			// it will ignore physical keyboard state
			// it will NOT impact physical keyboard state 			
			NSLog(@"Using Session Event Tap");
			vncTapLocation = kCGSessionEventTap;
			break;
		case 0:
		default:
			// At this level will ignore passed in modifiers
			// it will combine with the physical keyboard state (CapsLock, etc)
			// it WILL impact physical keyboard state 
			NSLog(@"Using HID Event Tap");
			vncTapLocation = kCGHIDEventTap;
			break;
	}
}

+ (void) loadUnicodeKeyboard {
	// Have to do this a little later than startup to ensure that the Jaguar Bundle has loaded
	if ([[NSUserDefaults standardUserDefaults] boolForKey:@"UnicodeKeyboard"] && unicodeLayout == NULL) {
		if (floor(NSAppKitVersionNumber) <= floor(NSAppKitVersionNumber10_4)) {
			int timeout = 2000000;
			int pollInterval = 10000;
			// Have to change it once on startup to get the unicodeLayout
			KeyScript( smUnicodeScript | smKeyForceKeyScriptMask );
			while (timeout > 0 && GetScriptManagerVariable(smKeyScript) != smUnicodeScript) {
				usleep(pollInterval);
				timeout -= pollInterval;
			}
			NSLog(@"Current Script/Previous: %d/%d", GetScriptManagerVariable(smKeyScript), GetScriptManagerVariable(smLastScript));
		}
		OSStatus result = KLGetKeyboardLayoutWithIdentifier([[NSUserDefaults standardUserDefaults] integerForKey:@"UnicodeKeyboardIdentifier"], &unicodeLayout);
		if (floor(NSAppKitVersionNumber) <= floor(NSAppKitVersionNumber10_4)) {
			KeyScript( GetScriptManagerVariable(smLastScript) | smKeyForceKeyScriptMask );
			NSLog(@"Current Script/Previous: %d/%d", GetScriptManagerVariable(smKeyScript), GetScriptManagerVariable(smLastScript));
		}
		// Unicode Keyboard Should load keys from definition
		(*(theServer->pressModsForKeys) = YES);
		[self loadKeyboard: unicodeLayout];
	}	
}


+ (void) userSwitchedIn: (NSNotification *) aNotification {
    NSLog(@"User Switched In, Using HID Tap - %@", [aNotification name]);
	vncTapLocation = kCGHIDEventTap;
	return;
}
+ (void) userSwitchedOut: (NSNotification *) aNotification {
    NSLog(@"User Switched Out, Using Session Tap - %@", [aNotification name]);
	vncTapLocation = kCGSessionEventTap;	
	return;
}

+ (void) rfbConnect {
	if (unicodeLayout != NULL && !dynamicKeyboard) {
		//oldKeyboardScript = GetScriptManagerVariable(smKeyScript);
		KLGetCurrentKeyboardLayout(&currentKeyboardLayout);
		// Switch to Unicode Keyboard
		SyncSetKeyboardLayout(smUnicodeScript,unicodeLayout);
	}
}

+ (void) rfbDisconnect {
	if (unicodeLayout != NULL && !dynamicKeyboard) {
		// Switch to Old Keyboard
		SyncSetKeyboardLayout(oldKeyboardScript, currentKeyboardLayout);
		// That smKeySwap doesn't seem to work (10.4.10)
		//KeyScript(smKeySwapScript | smKeyForceKeyScriptMask);
		//KLSetCurrentKeyboardLayout(currentKeyboardLayout);
	}
}

+ (void) rfbUsage {
    fprintf(stderr,
            "\nTiger BUNDLE OPTIONS (10.4+):\n"
			);
}

+ (void) rfbPoll {
	if (0 && vncTapLocation == kCGHIDEventTap) {
		CGEventFlags newModifiers = 0;
		
		if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState,keyCodeShift))
			newModifiers |= kCGEventFlagMaskShift;
		if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState,keyCodeOption))
			newModifiers |= kCGEventFlagMaskAlternate;
		if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState,keyCodeControl))
			newModifiers |= kCGEventFlagMaskControl;
		if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState,keyCodeCommand))
			newModifiers |= kCGEventFlagMaskControl;
		
		currentModifiers = newModifiers;
	}
	
    return;
}

+ (void) rfbReceivedClientMessage {
    return;
}

+ (void) rfbShutdown {
    NSLog(@"Unloading Tiger Extensions");
}

// Keyboard handling code
+ handleKeyboard:(Bool) down forSym: (KeySym) keySym forClient: (rfbClientPtr) cl {
	CGKeyCode keyCode = theServer->keyTable[(unsigned short)keySym];
	CGEventFlags modifiersToSend = 0;

	if (1) {
		// Find the right keycodes base on the loaded keyboard
		keyCodeShift = theServer->keyTable[XK_Shift_L];
		keyCodeOption = theServer->keyTable[XK_Meta_L];
		keyCodeControl = theServer->keyTable[XK_Control_L];
		keyCodeCommand = theServer->keyTable[XK_Alt_L];
	}		

	// If we can't locate the keycode then we will use the special OPTION+4 HEX coding that is available on the Unicode HexInput Keyboard
	if (keyCode == 0xFFFF) {
		if (down && unicodeLayout != NULL) {
			CGKeyCode keyCodeMeta = 58; // KeyCode for the Option key with the Unicode Hex input keyboard
			unsigned short mask=0xF000;
			int rightShift;
			CGEventFlags oldModifiers = currentModifiers;
			
			// Switch to Unicode Keyboard
			if (dynamicKeyboard) {
				KLGetCurrentKeyboardLayout(&currentKeyboardLayout);
				SyncSetKeyboardLayout(smUnicodeScript,unicodeLayout);
				oldKeyboardScript = GetScriptManagerVariable(smLastScript);
				NSLog(@"Setting OldKeyboard: %d", GetScriptManagerVariable(smLastScript));
			}
			
			modifiersToSend = kCGEventFlagMaskAlternate | kCGEventFlagMaskNonCoalesced;
			setKeyModifiers(modifiersToSend);
			for (rightShift = 12; rightShift >= 0; rightShift-=4) {
				short unidigit = (keySym & mask) >> rightShift;
				
				sendKeyEvent(unicodeNumbersToKeyCodes[unidigit], 1, modifiersToSend);
				sendKeyEvent(unicodeNumbersToKeyCodes[unidigit], 0, modifiersToSend);
				
				mask >>= 4;
			}
			setKeyModifiers(oldModifiers);
			
			// Switch to Old Keyboard
			if (dynamicKeyboard)
				SyncSetKeyboardLayout(oldKeyboardScript, currentKeyboardLayout);
		}
	}
	else {
		BOOL isModifierKey = (XK_Shift_L <= keySym && keySym <= XK_Hyper_R);
		
		if (isModifierKey) {
			// Mark the key state for the client, we'll release down keys later
			cl->modiferKeys[keyCode] = down;
			
			// Record them in our "currentModifiers"
			switch (keySym) {
				case XK_Shift_L:
				case XK_Shift_R:
					if (down)
						currentModifiers |= kCGEventFlagMaskShift;
					else
						currentModifiers &= ~kCGEventFlagMaskShift;
					break;	
				case XK_Control_L:
				case XK_Control_R:
					if (down)
						currentModifiers |= kCGEventFlagMaskControl;
					else
						currentModifiers &= ~kCGEventFlagMaskControl;
					break;	
				case XK_Meta_L:
				case XK_Meta_R:
					if (down)
						currentModifiers |= kCGEventFlagMaskAlternate;
					else
						currentModifiers &= ~kCGEventFlagMaskAlternate;
					break;	
				case XK_Alt_L:
				case XK_Alt_R:
					if (down)
						currentModifiers |= kCGEventFlagMaskCommand;
					else
						currentModifiers &= ~kCGEventFlagMaskCommand;
					break;
			}					
			
			sendKeyEvent(keyCode, down, currentModifiers);
		}
		else {
			if (*(theServer->pressModsForKeys)) {
				if (theServer->keyTableMods[keySym] != 0xFF) {
					// Setup the state of the appropriate keys based on the value in the KeyTableMods
					CGEventFlags oldModifiers = currentModifiers;
					CGEventFlags modifiersToSend = kCGEventFlagMaskNonCoalesced;
					
					if ((theServer->keyTableMods[keySym] << 8) & shiftKey)
						modifiersToSend |= kCGEventFlagMaskShift;
					if ((theServer->keyTableMods[keySym] << 8) & optionKey)
						modifiersToSend |= kCGEventFlagMaskAlternate;
					if ((theServer->keyTableMods[keySym] << 8) & controlKey)
						modifiersToSend |= kCGEventFlagMaskControl;
					
					// Treat command key separately (not as part of the generation string)
					modifiersToSend |= (currentModifiers & kCGEventFlagMaskCommand);
					
					setKeyModifiers(modifiersToSend);
					sendKeyEvent(keyCode, down, modifiersToSend);
					// Back to current depressed state
					setKeyModifiers(oldModifiers);
				}
				else {
					// Not Modified (special keys, other modifiers)
					sendKeyEvent(keyCode, down, currentModifiers);
				}
			}
			else {
				CGEventFlags oldModifiers = currentModifiers;
				CGEventFlags modifiersToSend = kCGEventFlagMaskNonCoalesced | 
					(cl->modiferKeys[keyCodeShift] ? kCGEventFlagMaskShift : 0) |
					(cl->modiferKeys[keyCodeControl] ? kCGEventFlagMaskControl : 0) |
					(cl->modiferKeys[keyCodeOption] ? kCGEventFlagMaskAlternate : 0) |
					(cl->modiferKeys[keyCodeCommand] ? kCGEventFlagMaskCommand : 0);
				
				setKeyModifiers(modifiersToSend);
				sendKeyEvent(keyCode, down, modifiersToSend);
				setKeyModifiers(oldModifiers);
			}
		}
	}
}

inline void setKeyModifiers(CGEventFlags modifierFlags) {
	// If it's a session tap (and we have an event source) then we can specify our own modifiers as part of the event (nothing to do here)
	// Otherwise we will have to explicitly twiddle them at the HID level based on their current state
	if (vncTapLocation == kCGHIDEventTap || !vncSourceRef) {
		CGEventRef event = nil;
		
		// Toggle the state of the appropriate keys
		if (!(currentModifiers & kCGEventFlagMaskCommand) != !(modifierFlags & kCGEventFlagMaskCommand)) {
			sendKeyEvent(keyCodeCommand, !(currentModifiers & kCGEventFlagMaskCommand), 0);
		}
		if (!(currentModifiers & kCGEventFlagMaskShift) != !(modifierFlags & kCGEventFlagMaskShift)) {
			sendKeyEvent(keyCodeShift, !(currentModifiers & kCGEventFlagMaskShift), 0);
		}
		if (!(currentModifiers & kCGEventFlagMaskAlternate) != !(modifierFlags & kCGEventFlagMaskAlternate)) {
			sendKeyEvent(keyCodeOption, !(currentModifiers & kCGEventFlagMaskAlternate), 0);
		}
		if (!(currentModifiers & kCGEventFlagMaskControl) != !(modifierFlags & kCGEventFlagMaskControl)) {
			sendKeyEvent(keyCodeControl, !(currentModifiers & kCGEventFlagMaskControl), 0);
		}
	}
	currentModifiers = modifierFlags;
}

inline void sendKeyEvent(CGKeyCode keyCode, Bool down, CGEventFlags localModifierFlags) {
	if (!vncSourceRef) {
		CGPostKeyboardEvent(0, keyCode, down);
	}
	else {
		CGEventRef event = CGEventCreateKeyboardEvent(vncSourceRef, keyCode, down);
		
		// The value of this function escapes me (since you still need to specify the keyCode for it to work
		// CGEventKeyboardSetUnicodeString (event, 1, (const UniChar *) &keySym);
		
		// If it's a session tap then we can specify our own modifiers as part of the event
		if (vncTapLocation != kCGHIDEventTap)
			CGEventSetFlags(event, localModifierFlags);
		CGEventPost(vncTapLocation, event);
		
		CFRelease(event);
	}
}

void SyncSetKeyboardLayout (unsigned long keyboardScript, KeyboardLayoutRef newLayout) {
	if (floor(NSAppKitVersionNumber) > floor(NSAppKitVersionNumber10_4)) {
		// This API doesn't work
		NSLog(@"Must Set Keyboard Manually, Old Keyboard API doesn't work");
		return;
	}
	
	KeyboardLayoutRef aKeyboardLayout;
	int identifier=0, targetIdentifier=0;
	int timeout = 2000000;
	int pollInterval = 100000;
	OSStatus result = 0;
	
	KeyScript( keyboardScript | smKeyForceKeyScriptMask );
	// Async -- wait for it to change
	while (timeout > 0 && GetScriptManagerVariable(smKeyScript) != keyboardScript) {
		NSLog(@"Waiting for Script to Change From:%d To:%d", GetScriptManagerVariable(smKeyScript), keyboardScript);
		usleep(pollInterval);
		timeout -= pollInterval;
	}
	oldKeyboardScript = GetScriptManagerVariable(smLastScript);
	
	// Try setting it
	// Get the target identifier
	KLGetKeyboardLayoutProperty(newLayout, kKLIdentifier, (const void **)&targetIdentifier);
	NSLog(@"Current Script(Keyboard)/Previous: %d(%d)/%d", GetScriptManagerVariable(smKeyScript), targetIdentifier, GetScriptManagerVariable(smLastScript));
			 
	result = KLSetCurrentKeyboardLayout(newLayout);
	if (result != noErr) {
		NSLog(@"Error Setting Keyboard Layout: %d Result: %d", targetIdentifier, result);
		return;
	}
	// Async -- wait for it to change
	while (timeout > 0) {
		KLGetCurrentKeyboardLayout(&aKeyboardLayout);
		KLGetKeyboardLayoutProperty(aKeyboardLayout, kKLIdentifier, (const void **)&identifier);
		if (identifier == targetIdentifier)
			break;
		NSLog(@"Waiting for Keyboard Layout to Change From:%d To:%d Result:%d", identifier, targetIdentifier, result);
		usleep(pollInterval);
		timeout -= pollInterval;
	}
}

@end
