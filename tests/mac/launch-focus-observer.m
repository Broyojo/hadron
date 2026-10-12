#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

static NSTimeInterval started;
static void emit(NSDictionary *value)
{
    NSMutableDictionary *row = [value mutableCopy];
    row[@"elapsed"] = @([NSProcessInfo processInfo].systemUptime - started);
    row[@"unix_time"] = @([NSDate date].timeIntervalSince1970);
    NSData *data = [NSJSONSerialization dataWithJSONObject:row options:NSJSONWritingSortedKeys error:NULL];
    fwrite(data.bytes, 1, data.length, stdout);
    putchar('\n');
}
static BOOL target(NSRunningApplication *app)
{
    return [app.bundleIdentifier hasPrefix:@"com.broyojo.hadron.game."];
}
static NSDictionary *identity(NSRunningApplication *app)
{
    if (!app) return @{};
    return @{@"pid": @(app.processIdentifier), @"bundle": app.bundleIdentifier ?: @"",
             @"name": app.localizedName ?: @"", @"active": @(app.active),
             @"policy": @(app.activationPolicy)};
}
int main(int argc, const char **argv)
{
    @autoreleasepool
    {
        setbuf(stdout, NULL);
        started = [NSProcessInfo processInfo].systemUptime;
        NSTimeInterval duration = argc > 1 ? atof(argv[1]) : 600;
        NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
        NSMutableArray *screens = [NSMutableArray array];
        for (NSScreen *screen in NSScreen.screens)
            [screens addObject:@{@"id": screen.deviceDescription[@"NSScreenNumber"] ?: @0,
                                 @"frame": NSStringFromRect(screen.frame),
                                 @"scale": @(screen.backingScaleFactor)}];
        emit(@{@"event": @"start", @"screens": screens,
               @"ax_trusted": @(AXIsProcessTrusted()), @"duration": @(duration)});
        NSMutableArray *observers = [NSMutableArray array];
        for (NSNotificationName name in @[NSWorkspaceDidActivateApplicationNotification,
                                           NSWorkspaceDidDeactivateApplicationNotification,
                                           NSWorkspaceDidLaunchApplicationNotification,
                                           NSWorkspaceDidTerminateApplicationNotification])
        {
            id token = [workspace.notificationCenter addObserverForName:name object:nil
                queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *note) {
                    NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
                    if (target(app) || [name containsString:@"Activate"] || [name containsString:@"Deactivate"])
                        emit(@{@"event": name, @"app": identity(app)});
                }];
            [observers addObject:token];
        }
        NSDictionary *previous;
        while ([NSProcessInfo processInfo].systemUptime - started < duration)
        {
            @autoreleasepool
            {
                NSMutableArray *apps = [NSMutableArray array];
                NSMutableSet *pids = [NSMutableSet set];
                for (NSRunningApplication *app in workspace.runningApplications)
                    if (target(app)) { [apps addObject:identity(app)]; [pids addObject:@(app.processIdentifier)]; }
                // OnScreenOnly explicitly guarantees front-to-back order.
                NSArray *list = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID));
                NSMutableArray *windows = [NSMutableArray array];
                unsigned rank = 0;
                for (NSDictionary *window in list)
                {
                    if ([pids containsObject:window[(id)kCGWindowOwnerPID]] || [window[(id)kCGWindowOwnerPID] intValue] == workspace.frontmostApplication.processIdentifier)
                        [windows addObject:@{@"pid": window[(id)kCGWindowOwnerPID],
                            @"id": window[(id)kCGWindowNumber], @"layer": window[(id)kCGWindowLayer],
                            @"onscreen": window[(id)kCGWindowIsOnscreen] ?: @NO,
                            @"alpha": window[(id)kCGWindowAlpha] ?: @0,
                            @"bounds": window[(id)kCGWindowBounds], @"global_rank": @(rank)}];
                    if ([window[(id)kCGWindowIsOnscreen] boolValue]) rank++;
                }
                NSDictionary *snapshot = @{@"event": @"state", @"front": identity(workspace.frontmostApplication),
                                           @"apps": apps, @"windows": windows};
                if (![snapshot isEqual:previous]) { emit(snapshot); previous = snapshot; }
            }
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        }
        for (id token in observers) [workspace.notificationCenter removeObserver:token];
        emit(@{@"event": @"end"});
    }
    return 0;
}
