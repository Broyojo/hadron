#import <AppKit/AppKit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original AppKit-only experiment: miniaturize an inactive panel, then
 * compare competing ordering calls with otherwise identical controls. */
enum Ordering { none, front, back, frontFromWill };
static const struct Scenario {
    const char *name;
    enum Ordering ordering;
    BOOL lowerLevel, restyle, titled;
} scenarios[] = {
    { "immediate-front", front, NO, NO, NO },
    { "no-front-control", none, NO, NO, NO },
    { "front-from-will", frontFromWill, NO, NO, NO },
    { "will-control", none, NO, NO, NO },
    { "lower-and-front", front, YES, NO, NO },
    { "lower-only-control", none, YES, NO, NO },
    { "immediate-back", back, NO, NO, NO },
    { "no-back-control", none, NO, NO, NO },
    { "restyle-and-front", front, YES, YES, NO },
    { "restyle-control", none, YES, YES, NO },
    { "title-and-front", front, YES, YES, YES },
    { "title-control", none, YES, YES, YES },
};
static const NSUInteger caseCount = sizeof(scenarios) / sizeof(scenarios[0]);
static CFRunLoopSourceRef requestSource;
static NSMutableArray *requests;

/* Like Wine's source, drain requests in common modes, including animation.
 * A plain main-queue timer need not run while AppKit is tracking the window. */
static void performRequests(void *info)
{
    (void)info;
    for (;;)
    {
        dispatch_block_t block;
        @synchronized(requests)
        {
            if (!requests.count) return;
            block = requests[0];
            [requests removeObjectAtIndex:0];
        }
        block();
    }
}

@interface ProbeWindow : NSPanel
@end
@implementation ProbeWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@interface Probe : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(strong) ProbeWindow *window;
@property(strong) NSTask *helper;
@property(strong) NSRunningApplication *previousApp;
@property NSUInteger scenario, willCount, didCount, restoreCount, completed, failures;
@property BOOL armed, helperMode, caseFailed;
@property double start;
@end

@implementation Probe
- (void)log:(NSString *)event
{
    printf("case=%s t=%.6f event=%s style=%lu level=%ld mini=%d visible=%d "
           "active=%d key=%d will=%lu did=%lu restore=%lu\n",
           self.scenario < caseCount ? scenarios[self.scenario].name : "finished",
           NSProcessInfo.processInfo.systemUptime - self.start, event.UTF8String,
           (unsigned long)self.window.styleMask, (long)self.window.level,
           self.window.miniaturized, self.window.visible, NSApp.active,
           self.window.keyWindow, (unsigned long)self.willCount,
           (unsigned long)self.didCount, (unsigned long)self.restoreCount);
    fflush(stdout);
}
- (void)later:(double)seconds block:(dispatch_block_t)block
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)),
                   dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @synchronized(requests) { [requests addObject:[block copy]]; }
        CFRunLoopSourceSignal(requestSource);
        CFRunLoopWakeUp(CFRunLoopGetMain());
    });
}
- (void)finish:(int)status
{
    self.armed = NO;
    if (self.helper.running)
    {
        [self.helper terminate];
        [self.helper waitUntilExit];
    }
    [self.window close];
    if (self.previousApp && !self.previousApp.terminated)
        [self.previousApp activateWithOptions:0];
    printf("SUMMARY cases=%lu expected=%lu failures=%lu exit=%d\n",
           (unsigned long)self.completed, (unsigned long)caseCount,
           (unsigned long)self.failures, status);
    fflush(stdout);
    exit(status);
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    self.window = [[ProbeWindow alloc] initWithContentRect:NSMakeRect(200, 200, 480, 300)
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    self.window.hidesOnDeactivate = NO;
    self.window.title = self.helperMode ? @"Hadron focus control" : @"Hadron ordering probe";
    if (self.helperMode)
    {
        [self.window makeKeyAndOrderFront:nil];
        [self later:50 block:^{ exit(2); }];
        return;
    }
    self.helper = [NSTask new];
    self.helper.executableURL = [NSURL fileURLWithPath:NSProcessInfo.processInfo.arguments[0]];
    self.helper.arguments = @[@"--helper"];
    self.helper.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    NSError *error = nil;
    if (![self.helper launchAndReturnError:&error])
    {
        fprintf(stderr, "FAIL helper launch: %s\n", error.description.UTF8String);
        [self finish:2];
    }
    [self later:0.8 block:^{ [self next]; }];
    [self later:45 block:^{
        fprintf(stderr, "FAIL timeout before all cases completed\n");
        [self finish:2];
    }];
}
- (void)next
{
    if (self.scenario == caseCount)
        [self finish:self.failures ? 1 : 0];
    self.start = NSProcessInfo.processInfo.systemUptime;
    self.willCount = self.didCount = self.restoreCount = 0;
    self.caseFailed = NO;
    self.window.styleMask = NSWindowStyleMaskBorderless;
    self.window.level = NSNormalWindowLevel;
    [NSApp activateIgnoringOtherApps:YES];
    [self.window makeKeyAndOrderFront:nil];
    [self log:@"shown"];
    [self later:0.35 block:^{
        if (!NSApp.active || !self.window.keyWindow || self.window.miniaturized)
        {
            fprintf(stderr, "FAIL case=%s: initial active/key/unminimized state missing\n",
                    scenarios[self.scenario].name);
            [self finish:2];
        }
        self.armed = YES;
        NSRunningApplication *helper = [NSRunningApplication
            runningApplicationWithProcessIdentifier:self.helper.processIdentifier];
        if (![helper activateWithOptions:0])
        {
            fprintf(stderr, "FAIL helper activation\n");
            [self finish:2];
        }
        [self log:@"helper-activation-requested"];
    }];
}
- (void)applicationDidResignActive:(NSNotification *)notification
{
    (void)notification;
    if (self.helperMode || !self.armed) return;
    self.armed = NO;
    const struct Scenario *s = &scenarios[self.scenario];
    self.window.styleMask = NSWindowStyleMaskMiniaturizable;
    self.window.level = 21;
    [self log:@"request"];
    [self.window miniaturize:nil];
    [self log:@"returned"];
    if (s->restyle) self.window.styleMask = NSWindowStyleMaskBorderless;
    if (s->lowerLevel) self.window.level = NSNormalWindowLevel;
    if (s->ordering == front) [self.window orderFront:nil];
    if (s->ordering == back) [self.window orderBack:nil];
    if (s->titled) self.window.styleMask = NSWindowStyleMaskTitled |
        NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    [self log:@"intervention-complete"];
    [self later:0.9 block:^{
        [self log:@"before-focus-return"];
        BOOL expectMinimize = s->ordering == none || s->ordering == frontFromWill;
        BOOL ok = !NSApp.active && self.restoreCount == 0 &&
            self.willCount == (NSUInteger)expectMinimize &&
            self.didCount == (NSUInteger)expectMinimize &&
            self.window.miniaturized == expectMinimize;
        self.caseFailed = !ok;
        printf("case=%s phase=before-focus-return expected_minimize=%d result=%s\n",
               s->name, expectMinimize, ok ? "PASS" : "FAIL");
        [NSApp activateIgnoringOtherApps:YES];
        if (self.window.miniaturized) [self.window deminiaturize:nil];
        [self.window makeKeyAndOrderFront:nil];
        [self log:@"focus-return-requested"];
    }];
    [self later:1.5 block:^{
        [self log:@"result"];
        BOOL expectMinimize = s->ordering == none || s->ordering == frontFromWill;
        BOOL ok = !self.caseFailed && !self.window.miniaturized &&
            self.window.visible && self.window.keyWindow && NSApp.active &&
            self.willCount == (NSUInteger)expectMinimize &&
            self.didCount == (NSUInteger)expectMinimize &&
            self.restoreCount == (NSUInteger)expectMinimize;
        printf("case=%s phase=restored result=%s\n", s->name, ok ? "PASS" : "FAIL");
        fflush(stdout);
        self.completed++;
        if (!ok) self.failures++;
        if (self.window.miniaturized) [self.window deminiaturize:nil];
        [self later:0.65 block:^{ self.scenario++; [self next]; }];
    }];
}
- (void)windowWillMiniaturize:(NSNotification *)notification
{
    (void)notification;
    self.willCount++;
    [self log:@"will-minimize"];
    if (self.scenario < caseCount && scenarios[self.scenario].ordering == frontFromWill)
    {
        [self.window orderFront:nil];
        [self log:@"orderFront-from-will"];
    }
}
- (void)windowDidMiniaturize:(NSNotification *)notification
{
    (void)notification;
    self.didCount++;
    [self log:@"did-minimize"];
}
- (void)windowDidDeminiaturize:(NSNotification *)notification
{
    (void)notification;
    self.restoreCount++;
    [self log:@"did-restore"];
}
@end

int main(int argc, const char **argv)
{
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--helper")))
    {
        fprintf(stderr, "usage: %s [--helper]\n", argv[0]);
        return 2;
    }
    @autoreleasepool
    {
        NSRunningApplication *previous = NSWorkspace.sharedWorkspace.frontmostApplication;
        [NSApplication sharedApplication];
        requests = [NSMutableArray new];
        CFRunLoopSourceContext context = {0};
        context.perform = performRequests;
        requestSource = CFRunLoopSourceCreate(NULL, 0, &context);
        CFRunLoopAddSource(CFRunLoopGetMain(), requestSource, kCFRunLoopCommonModes);
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        Probe *probe = [Probe new];
        probe.previousApp = previous;
        probe.helperMode = argc == 2;
        NSApp.delegate = probe;
        [NSApp run];
    }
    return 2; /* Unexpected run-loop exit is not a successful experiment. */
}
