import Foundation

/// The runtime this program belongs to, and its scripts. In Hadron.app the runtime is inside the
/// bundle; a development build finds the checkout it was built from.
enum Runtime {
    static let root: URL = {
        let bundled = Bundle.main.bundleURL.appendingPathComponent("Contents/SharedSupport/runtime")
        if FileManager.default.fileExists(atPath: bundled.appendingPathComponent("scripts/steam-install").path) {
            return bundled
        }
        if let path = ProcessInfo.processInfo.environment["HADRON_RUNTIME"] {
            return URL(fileURLWithPath: path)
        }
        // A development build sits in build/ of a checkout.
        var dir = URL(fileURLWithPath: CommandLine.arguments[0]).resolvingSymlinksInPath().deletingLastPathComponent()
        for _ in 0..<8 {
            if FileManager.default.fileExists(atPath: dir.appendingPathComponent("scripts/steam-install").path) { return dir }
            dir = dir.deletingLastPathComponent()
        }
        return bundled
    }()

    static var version: String {
        let file = root.appendingPathComponent("VERSION")
        return (try? String(contentsOf: file, encoding: .utf8))?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "unknown"
    }

    /// Run one of the runtime's scripts, handing each line it prints to `line`. Returns its exit status.
    @discardableResult
    static func run(_ script: String, _ arguments: [String] = [], line: @escaping (String) -> Void = { _ in }) -> Int32 {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sh")
        process.arguments = [root.appendingPathComponent("scripts/\(script)").path] + arguments
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        do { try process.run() } catch {
            line("cannot run \(script): \(error.localizedDescription)")
            return 127
        }
        var rest = Data()
        while true {
            let chunk = pipe.fileHandleForReading.availableData
            if chunk.isEmpty { break }
            rest.append(chunk)
            while let newline = rest.firstIndex(of: 0x0a) {
                line(String(decoding: rest[rest.startIndex..<newline], as: UTF8.self))
                rest.removeSubrange(rest.startIndex...newline)
            }
        }
        if !rest.isEmpty { line(String(decoding: rest, as: UTF8.self)) }
        process.waitUntilExit()
        return process.terminationStatus
    }

    /// How the Steam setup stands: scripts/steam-status's word and its sentence.
    static func status() -> (state: String, message: String) {
        var lines: [String] = []
        run("steam-status") { lines.append($0) }
        return (lines.first ?? "unknown", lines.count > 1 ? lines[1] : "")
    }
}
