import Foundation

/// The runtime this program belongs to, and its scripts. In Hadron.app the runtime is inside the
/// bundle; a development build finds the checkout it was built from.
enum Runtime {
    /// This program's own file. The `hadron` command can be a link to it, as Homebrew's is, so
    /// the bundle is found from here and not from the path the program was started by.
    static let executable: URL = {
        var size: UInt32 = 0
        _NSGetExecutablePath(nil, &size)
        var path = [CChar](repeating: 0, count: Int(size))
        _NSGetExecutablePath(&path, &size)
        return URL(fileURLWithPath: String(cString: path)).resolvingSymlinksInPath()
    }()

    /// Hadron.app, when this program is the one in its Contents/MacOS.
    static let app: URL? = {
        let contents = executable.deletingLastPathComponent().deletingLastPathComponent()
        let bundle = contents.deletingLastPathComponent()
        return contents.lastPathComponent == "Contents" && bundle.pathExtension == "app" ? bundle : nil
    }()

    static let root: URL = {
        let bundled = (app ?? Bundle.main.bundleURL).appendingPathComponent("Contents/SharedSupport/runtime")
        if FileManager.default.fileExists(atPath: bundled.appendingPathComponent("scripts/steam-install").path) {
            return bundled
        }
        if let path = ProcessInfo.processInfo.environment["HADRON_RUNTIME"] {
            return URL(fileURLWithPath: path)
        }
        // A development build sits in build/ of a checkout.
        var dir = executable.deletingLastPathComponent()
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

    /// Start a Windows program that is not a Steam game (scripts/program-run) and leave it running:
    /// it outlives the window, and logs to Hadron's logs folder. Returns why it could not start, if
    /// it could not; `failed` is told later if the runner gives up before the program runs.
    static func start(program: URL, failed: @escaping @Sendable (String) -> Void) -> String? {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sh")
        process.arguments = [root.appendingPathComponent("scripts/program-run").path, program.path]
        var environment = ProcessInfo.processInfo.environment
        environment["HADRON_LOG"] = "1"
        process.environment = environment
        // What the runner says before the program runs (a missing file, a prefix that could not be
        // made) goes to a file, not a pipe: closing the window ends this app, and a pipe with no
        // reader would kill the runner at its next message. Once the program runs, its output goes
        // to its log.
        let messages = FileManager.default.temporaryDirectory
            .appendingPathComponent("hadron-program-run-\(UUID().uuidString).txt")
        FileManager.default.createFile(atPath: messages.path, contents: nil)
        guard let errors = try? FileHandle(forWritingTo: messages) else { return "cannot write \(messages.path)" }
        process.standardOutput = FileHandle.nullDevice
        process.standardError = errors
        process.terminationHandler = { finished in
            let said = ((try? String(contentsOf: messages, encoding: .utf8)) ?? "")
                .split(separator: "\n")
                .filter { !$0.hasPrefix("watchdog: memory log in") && !$0.hasPrefix("logging to ") }
            try? FileManager.default.removeItem(at: messages)
            if finished.terminationStatus != 0 && !said.isEmpty {
                failed(said.joined(separator: "\n"))
            }
        }
        defer { try? errors.close() }
        do { try process.run() } catch { return error.localizedDescription }
        return nil
    }

    /// Run one of the runtime's scripts on this terminal: its output, and the program's, stay here.
    static func runAttached(_ script: String, _ arguments: [String]) -> Int32 {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/sh")
        process.arguments = [root.appendingPathComponent("scripts/\(script)").path] + arguments
        do { try process.run() } catch {
            FileHandle.standardError.write(Data("hadron: cannot run \(script): \(error.localizedDescription)\n".utf8))
            return 127
        }
        // A signal sent to this process alone (a supervisor's, kill's) goes on to the runner, which
        // stops the program; a terminal's Ctrl-C reaches both anyway.
        let forwarders = [SIGTERM, SIGINT, SIGHUP].map { number -> DispatchSourceSignal in
            signal(number, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: number, queue: .global())
            source.setEventHandler { kill(process.processIdentifier, number) }
            source.resume()
            return source
        }
        process.waitUntilExit()
        forwarders.forEach { $0.cancel() }
        return process.terminationStatus
    }
}
