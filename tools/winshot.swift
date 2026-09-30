// winshot: print the window ID of the first on-screen window whose owner or title matches,
// for use with `screencapture -l <id>`. Usage: winshot <substring>
import CoreGraphics
import Foundation

let needle = CommandLine.arguments.dropFirst().first?.lowercased() ?? ""
let windows = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]] ?? []
for w in windows {
    let owner = (w[kCGWindowOwnerName as String] as? String) ?? ""
    let title = (w[kCGWindowName as String] as? String) ?? ""
    let layer = (w[kCGWindowLayer as String] as? Int) ?? 0
    let bounds = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let width = (bounds["Width"] as? Double) ?? 0
    guard layer == 0, width > 100 else { continue }
    if (owner + " " + title).lowercased().contains(needle) {
        print(w[kCGWindowNumber as String] as? Int ?? 0, owner, "|", title)
    }
}
