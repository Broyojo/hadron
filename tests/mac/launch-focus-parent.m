// Owns only isolated launch-control windows, never Steam or a game installation.
#import <AppKit/AppKit.h>
#include <spawn.h>
#include <unistd.h>

@interface ProbeWindow : NSWindow
@end
@implementation ProbeWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@interface Probe : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property NSString *role;
@property NSString *folder;
@property NSString *handoff;
@property double delay;
@property BOOL switchAway;
@property NSWindow *window;
@property FILE *log;
@end
@implementation Probe
- (void)record:(NSString *)phase
{
    NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication;
    NSDictionary *row = @{@"phase": phase, @"role": self.role, @"pid": @(getpid()), @"ppid": @(getppid()),
        @"uptime": @(NSProcessInfo.processInfo.systemUptime), @"active": @(NSApp.active),
        @"window": @(self.window.windowNumber), @"key": @(self.window.keyWindow), @"visible": @(self.window.visible),
        @"level": @(self.window.level), @"front_pid": @(front.processIdentifier),
        @"front_bundle": front.bundleIdentifier ?: @""};
    NSData *data = [NSJSONSerialization dataWithJSONObject:row options:NSJSONWritingSortedKeys error:NULL];
    fwrite(data.bytes, 1, data.length, self.log); fputc('\n', self.log); fflush(self.log);
}
- (void)show
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    self.window = [[ProbeWindow alloc] initWithContentRect:NSMakeRect(100, 100, 480, 240)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = [@"Launch activation probe: " stringByAppendingString:self.role];
    self.window.delegate = self;
    [self.window orderFront:nil];
    [self.window makeKeyWindow];
    [self record:@"request-activation"];
    [NSApp activate];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        [self record:@"after-request"];
    });
}
- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    (void)note;
    [self record:@"launched"];
    if ([self.role isEqualToString:@"parent"])
    {
        [self show];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            [self record:@"before-spawn"];
            if ([self.handoff hasSuffix:@"bundle"])
                [NSApp yieldActivationToApplicationWithBundleIdentifier:
                    @"com.broyojo.hadron.game.4294967002"];
            pid_t pid;
            int result;
            NSString *home = [@"HOME=" stringByAppendingString:NSHomeDirectory()];
            NSString *user = [@"USER=" stringByAppendingString:NSUserName()];
            char *cleanEnv[] = {"PATH=/usr/bin:/bin:/usr/sbin:/sbin", "LANG=en_US.UTF-8",
                (char *)home.UTF8String, (char *)user.UTF8String, NULL};
            {
                NSString *script = [self.folder stringByAppendingPathComponent:@"run-wine.sh"];
                char *wineArgs[] = {(char *)script.fileSystemRepresentation, NULL};
                result = posix_spawn(&pid, script.fileSystemRepresentation, NULL, NULL, wineArgs, cleanEnv);
            }
            if (result) { [self record:@"spawn-failed"]; [NSApp terminate:nil]; return; }
            [self record:@"after-spawn"];
            if (self.switchAway)
            {
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    NSWorkspaceOpenConfiguration *config = [NSWorkspaceOpenConfiguration configuration];
                    config.arguments = @[@"helper", self.folder];
                    config.activates = YES;
                    [NSWorkspace.sharedWorkspace openApplicationAtURL:
                        [NSURL fileURLWithPath:[self.folder stringByAppendingPathComponent:@"Helper.app"]]
                        configuration:config completionHandler:^(NSRunningApplication *app, NSError *error) {
                            (void)app; (void)error;
                        }];
                    [self record:@"switch-away"];
                });
            }
        });
    }
    else [self show];
    double lifetime = self.delay + 8;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(lifetime * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self record:@"final"]; [NSApp terminate:nil];
    });
}
- (void)applicationDidBecomeActive:(NSNotification *)note { (void)note; [self record:@"did-activate"]; }
- (void)applicationDidResignActive:(NSNotification *)note { (void)note; [self record:@"did-deactivate"]; }
- (void)windowDidBecomeKey:(NSNotification *)note { (void)note; [self record:@"did-become-key"]; }
- (void)windowDidResignKey:(NSNotification *)note { (void)note; [self record:@"did-resign-key"]; }
@end

int main(int argc, const char **argv)
{
    @autoreleasepool
    {
        if (argc < 3) return 2;
        Probe *probe = [Probe new];
        probe.role = @(argv[1]); probe.folder = @(argv[2]);
        if ([probe.role isEqualToString:@"parent"])
        {
            if (argc < 6) return 2;
            probe.handoff = @(argv[3]); probe.delay = atof(argv[4]); probe.switchAway = atoi(argv[5]);
        }
        else if ([probe.role isEqualToString:@"child"]) probe.delay = argc > 3 ? atof(argv[3]) : 0;
        NSString *path = [probe.folder stringByAppendingPathComponent:[probe.role stringByAppendingString:@".jsonl"]];
        probe.log = fopen(path.fileSystemRepresentation, "a");
        if (!probe.log) return 2;
        [NSApplication sharedApplication];
        NSApp.delegate = probe;
        [NSApp run];
        fclose(probe.log);
    }
    return 0;
}
