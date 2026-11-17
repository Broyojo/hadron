// What macOS's Accessibility API, the one window tools such as Rectangle use, says about an app's
// windows: where each one is, how big, and whether other apps may move and resize it. With
// --resize it asks for a new size the way Rectangle does and reads back what the window took;
// with --frame it moves and resizes the window as Rectangle's shortcuts do (halves, maximize,
// next display). --screens lists each display's usable area in the same coordinates.
//
// Usage: ax-window <pid or part of the app's name> [--resize WIDTH HEIGHT | --frame X Y WIDTH HEIGHT]
//        ax-window --screens
// Coordinates are points, from the top left of the main display, as the Accessibility API has them.
// Exit status: 0, or 1 when a --resize or --frame didn't take or found no window to try it on.
// The terminal needs Accessibility permission (System Settings › Privacy & Security › Accessibility).
//
// Build: swiftc -O -o ax-window tests/mac/ax-window.swift
import AppKit
import ApplicationServices

let args = CommandLine.arguments
let usage = "usage: ax-window <pid|name> [--resize WIDTH HEIGHT | --frame X Y WIDTH HEIGHT]\n       ax-window --screens"

if args.count == 2 && args[1] == "--screens" {
    let mainHeight = NSScreen.screens.first?.frame.height ?? 0
    for (index, screen) in NSScreen.screens.enumerated() {
        let area = screen.visibleFrame, whole = screen.frame
        print("display \(index + 1) \"\(screen.localizedName)\": \(Int(whole.width))x\(Int(whole.height)) at "
              + "\(Int(whole.minX)),\(Int(mainHeight - whole.maxY)); usable \(Int(area.minX)) \(Int(mainHeight - area.maxY)) "
              + "\(Int(area.width)) \(Int(area.height)) (X Y WIDTH HEIGHT, menu bar and Dock left out)")
    }
    exit(0)
}

var wantedOrigin: CGPoint?
var wanted: CGSize?
let numbers = args.dropFirst(3).map { Double($0) }
switch (args.count, args.count > 2 ? args[2] : "") {
case (2, _): break
case (5, "--resize"):
    guard let w = numbers[0], let h = numbers[1] else { print("WIDTH and HEIGHT are numbers"); exit(2) }
    wanted = CGSize(width: w, height: h)
case (7, "--frame"):
    guard let x = numbers[0], let y = numbers[1], let w = numbers[2], let h = numbers[3] else {
        print("X, Y, WIDTH and HEIGHT are numbers"); exit(2)
    }
    wantedOrigin = CGPoint(x: x, y: y)
    wanted = CGSize(width: w, height: h)
default:
    print(usage)
    exit(2)
}
guard AXIsProcessTrusted() else {
    print("The terminal needs Accessibility permission: System Settings › Privacy & Security › Accessibility.")
    exit(2)
}

let apps: [NSRunningApplication]
if let pid = pid_t(args[1]) {
    apps = NSRunningApplication(processIdentifier: pid).map { [$0] } ?? []
} else {
    apps = NSWorkspace.shared.runningApplications.filter {
        ($0.localizedName ?? "").localizedCaseInsensitiveContains(args[1])
    }
}
guard !apps.isEmpty else { print("No running app matches \(args[1])"); exit(2) }

func axValue(_ element: AXUIElement, _ attribute: String) -> AXValue? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, attribute as CFString, &value) == .success,
          let value, CFGetTypeID(value) == AXValueGetTypeID() else { return nil }
    return (value as! AXValue)
}

func size(_ element: AXUIElement) -> CGSize? {
    var size = CGSize.zero
    guard let value = axValue(element, kAXSizeAttribute), AXValueGetValue(value, .cgSize, &size) else { return nil }
    return size
}

func position(_ element: AXUIElement) -> CGPoint? {
    var point = CGPoint.zero
    guard let value = axValue(element, kAXPositionAttribute), AXValueGetValue(value, .cgPoint, &point) else { return nil }
    return point
}

func string(_ element: AXUIElement, _ attribute: String) -> String? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, attribute as CFString, &value) == .success else { return nil }
    return value as? String
}

// A window with a Mac title bar has the traffic-light buttons; an app that draws its own has none.
func hasCloseButton(_ window: AXUIElement) -> Bool {
    var value: CFTypeRef?
    return AXUIElementCopyAttributeValue(window, kAXCloseButtonAttribute as CFString, &value) == .success && value != nil
}

func settable(_ element: AXUIElement, _ attribute: String) -> Bool {
    var result = DarwinBoolean(false)
    return AXUIElementIsAttributeSettable(element, attribute as CFString, &result) == .success && result.boolValue
}

func text(_ point: CGPoint?) -> String { point.map { "\(Int($0.x)),\(Int($0.y))" } ?? "?" }
func text(_ size: CGSize?) -> String { size.map { "\(Int($0.width))x\(Int($0.height))" } ?? "?" }

var failed = false
var tried = 0
for app in apps {
    let element = AXUIElementCreateApplication(app.processIdentifier)
    AXUIElementSetMessagingTimeout(element, 2) // a busy Wine app must not hang the check
    var windows: CFTypeRef?
    let error = AXUIElementCopyAttributeValue(element, kAXWindowsAttribute as CFString, &windows)
    print("\(app.localizedName ?? "?") (pid \(app.processIdentifier))"
          + (error == .success ? "" : ": no windows (AX error \(error.rawValue))"))
    for window in (windows as? [AXUIElement]) ?? [] {
        AXUIElementSetMessagingTimeout(window, 2)
        print("  \"\(string(window, kAXTitleAttribute) ?? "")\" at \(text(position(window))), \(text(size(window)))"
              + "; others may move it: \(settable(window, kAXPositionAttribute) ? "yes" : "no")"
              + ", resize it: \(settable(window, kAXSizeAttribute) ? "yes" : "no")"
              + "; subrole \(string(window, kAXSubroleAttribute) ?? "none")"
              + "; Mac title bar: \(hasCloseButton(window) ? "yes" : "no")")
        guard var target = wanted, let value = AXValueCreate(.cgSize, &target) else { continue }
        tried += 1
        func move() -> AXError {
            guard var origin = wantedOrigin, let point = AXValueCreate(.cgPoint, &origin) else { return .success }
            return AXUIElementSetAttributeValue(window, kAXPositionAttribute as CFString, point)
        }
        // Rectangle sets the size, the position, then the size again: a window can't take a size
        // that doesn't fit where it is, and moving it to another display can change what fits.
        var set = AXUIElementSetAttributeValue(window, kAXSizeAttribute as CFString, value)
        if wantedOrigin != nil {
            let moved = move()
            let sized = AXUIElementSetAttributeValue(window, kAXSizeAttribute as CFString, value)
            if set == .success { set = moved != .success ? moved : sized }
        }
        Thread.sleep(forTimeInterval: 0.8)
        let now = size(window), at = position(window)
        let close = { (a: CGFloat, b: CGFloat) in abs(a - b) <= 2 }
        let took = (now.map { close($0.width, target.width) && close($0.height, target.height) } ?? false)
            && (wantedOrigin.map { origin in at.map { close($0.x, origin.x) && close($0.y, origin.y) } ?? false } ?? true)
        failed = failed || !took
        let asked = wantedOrigin.map { "\(text($0)), " } ?? ""
        print("    asked for \(asked)\(text(target)): \(set == .success ? "accepted" : "refused (AX error \(set.rawValue))")"
              + ", now \(text(at)), \(text(now)): \(took ? "PASS" : "FAIL")")
    }
}
if wanted != nil && tried == 0 {
    print("No window to try it on")
    failed = true
}
exit(failed ? 1 : 0)
