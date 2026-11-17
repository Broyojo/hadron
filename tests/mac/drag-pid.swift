// drag-pid: drags with the left button inside one process's frontmost window, from a point given
// relative to that window (in points, from its top-left corner), by dx, dy points, without moving
// the pointer. The events go to the process, not to the screen, so they can't reach another app.
// AppKit resizes a window it counts as resizable from its edges in the app itself, so this shows
// whether a drag at an edge resizes the window through AppKit or reaches the program.
//
//   drag-pid <pid> <x> <y> <dx> <dy>
//
// Prints the window's frame before and after.
//
// Build: swiftc -O -o drag-pid tests/mac/drag-pid.swift
import AppKit

let args = CommandLine.arguments
guard args.count == 6, let pid = pid_t(args[1]), let x = Double(args[2]), let y = Double(args[3]),
      let dx = Double(args[4]), let dy = Double(args[5]) else {
    FileHandle.standardError.write("usage: drag-pid <pid> <x> <y> <dx> <dy>\n".data(using: .utf8)!)
    exit(2)
}

func frontWindow() -> (number: Int, bounds: CGRect)? {
    let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID)
        as? [[String: Any]] ?? []
    guard let info = list.first(where: { ($0[kCGWindowOwnerPID as String] as? pid_t) == pid &&
                                         ($0[kCGWindowLayer as String] as? Int) == 0 }),
          let number = info[kCGWindowNumber as String] as? Int,
          let boundsDict = info[kCGWindowBounds as String] as? NSDictionary,
          let bounds = CGRect(dictionaryRepresentation: boundsDict) else { return nil }
    return (number, bounds)
}

func describe(_ b: CGRect) -> String {
    "\(Int(b.minX)),\(Int(b.minY)) \(Int(b.width))x\(Int(b.height))"
}

// Without permission to post events, macOS drops them silently and the window would stay "the same
// size", which reads as AppKit refusing the drag.
guard CGPreflightPostEventAccess(), AXIsProcessTrusted() else {
    print("The terminal needs Accessibility permission to post mouse events: System Settings › Privacy & Security › Accessibility.")
    exit(2)
}

guard let (number, bounds) = frontWindow() else {
    FileHandle.standardError.write("drag-pid: no window for pid \(pid)\n".data(using: .utf8)!)
    exit(1)
}
print("before: window \(number) at \(describe(bounds)); drag from \(Int(x)),\(Int(y)) by \(Int(dx)),\(Int(dy))")

// AppKit gives a mouse event to a window by its window number and a point in the window (from its
// bottom-left corner): build each event as AppKit would, and send its CGEvent to the process.
func post(_ type: NSEvent.EventType, _ cgType: CGEventType, _ px: Double, _ py: Double) {
    let point = CGPoint(x: bounds.minX + px, y: bounds.minY + py)
    guard let ns = NSEvent.mouseEvent(with: type, location: NSPoint(x: px, y: bounds.height - py),
                                      modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                                      windowNumber: number, context: nil, eventNumber: 0, clickCount: 1,
                                      pressure: 1),
          let event = ns.cgEvent else { exit(1) }
    event.type = cgType
    event.location = point
    event.setIntegerValueField(CGEventField(rawValue: 91)!, value: Int64(number))  // window under pointer
    event.setIntegerValueField(CGEventField(rawValue: 92)!, value: Int64(number))  // ... that can handle it
    event.postToPid(pid)
    usleep(30_000)
}

post(.leftMouseDown, .leftMouseDown, x, y)
let steps = 10
for i in 1...steps {
    post(.leftMouseDragged, .leftMouseDragged, x + dx * Double(i) / Double(steps), y + dy * Double(i) / Double(steps))
}
post(.leftMouseUp, .leftMouseUp, x + dx, y + dy)
usleep(500_000)
guard let (_, after) = frontWindow() else { exit(1) }
print("after: \(describe(after))\(after.size == bounds.size ? "; the same size" : "; resized")")
