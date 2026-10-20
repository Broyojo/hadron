// Log every NSCursor a process makes, its size in points and its hot spot, to stderr. Insert it
// into a Wine program with STEAM_DYLD_INSERT_LIBRARIES (scripts/play passes that on) and run
// tests/win/cursor-size.c: a 32x32 cursor should log 32x32 points.
// Build: clang -dynamiclib -fobjc-arc -framework AppKit -o nscursor-log.dylib tests/mac/nscursor-log.m
#import <AppKit/AppKit.h>
#import <objc/runtime.h>

static id (*orig_init)(id, SEL, NSImage *, NSPoint);

static id logging_init(id self, SEL sel, NSImage *image, NSPoint hot)
{
    fprintf(stderr, "nscursor: %gx%g points, hot spot %g,%g\n", image.size.width, image.size.height, hot.x, hot.y);
    return orig_init(self, sel, image, hot);
}

__attribute__((constructor)) static void install(void)
{
    Method m = class_getInstanceMethod([NSCursor class], @selector(initWithImage:hotSpot:));
    orig_init = (void *)method_setImplementation(m, (IMP)logging_init);
}
