// The on-screen windows of one process, to capture a test's own window and nothing else
// (`screencapture -x -o -l <id> out.png`, then tests/mac/picture-bounds.swift). Prints one line per
// window: its CGWindowID, layer, bounds in points and owner. With no pid, lists the windows of
// every process named "wine"; pick the test's by its pid (`ps eww` shows its WINEPREFIX).
//
// Build: swiftc -O -o window-id tests/mac/window-id.swift
// Usage: window-id [pid]
import CoreGraphics
import Foundation

let pid = CommandLine.arguments.count > 1 ? Int32(CommandLine.arguments[1]) : nil
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID)
    as? [[String: Any]] ?? []
for window in windows {
    let owner = window[kCGWindowOwnerName as String] as? String ?? ""
    let ownerPid = window[kCGWindowOwnerPID as String] as? Int32 ?? 0
    guard pid.map({ $0 == ownerPid }) ?? (owner == "wine") else { continue }
    let id = window[kCGWindowNumber as String] as? Int ?? 0
    let layer = window[kCGWindowLayer as String] as? Int ?? 0
    let b = window[kCGWindowBounds as String] as? [String: Double] ?? [:]
    print("\(id) layer \(layer) bounds \(Int(b["X"] ?? 0)),\(Int(b["Y"] ?? 0)) \(Int(b["Width"] ?? 0))x\(Int(b["Height"] ?? 0)) owner \(owner) pid \(ownerPid)")
}
