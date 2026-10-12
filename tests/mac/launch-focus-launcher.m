#import <AppKit/AppKit.h>

/* Launch the private control through the same explicit Workspace API used for
 * the switch helper. The CLI open path did not reliably activate the control. */
int main(int argc, const char **argv)
{
    @autoreleasepool
    {
        if (argc < 3) return 2;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        NSWorkspaceOpenConfiguration *configuration = [NSWorkspaceOpenConfiguration configuration];
        configuration.activates = YES;
        configuration.createsNewApplicationInstance = YES;
        NSMutableArray *arguments = [NSMutableArray array];
        for (int i = 2; i < argc; i++) [arguments addObject:@(argv[i])];
        configuration.arguments = arguments;
        __block NSRunningApplication *application;
        __block NSError *failure;
        __block BOOL completed = NO;
        [NSWorkspace.sharedWorkspace openApplicationAtURL:[NSURL fileURLWithPath:@(argv[1])]
            configuration:configuration completionHandler:^(NSRunningApplication *app, NSError *error) {
                application = app; failure = error; completed = YES;
            }];
        NSTimeInterval deadline = NSProcessInfo.processInfo.systemUptime + 16;
        while (NSProcessInfo.processInfo.systemUptime < deadline)
        {
            if (completed && failure) { fprintf(stderr, "%s\n", failure.description.UTF8String); return 2; }
            if (completed && application.terminated) return 0;
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
        }
        fprintf(stderr, "Native control launch/exit timed out\n");
        return 2;
    }
}
