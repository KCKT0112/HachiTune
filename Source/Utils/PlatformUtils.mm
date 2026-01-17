#include "PlatformUtils.h"

#if JUCE_MAC
#import <Cocoa/Cocoa.h>

namespace PlatformUtils
{
    void setDarkAppearance(void* nativeHandle)
    {
        if (nativeHandle)
        {
            NSView* view = (__bridge NSView*)nativeHandle;
            if (NSWindow* window = [view window])
            {
                window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
            }
        }
    }
}

#else

namespace PlatformUtils
{
    void setDarkAppearance(void*) {}
}

#endif
