import Foundation

// One program with two faces: given a command it is `hadron`, opened as an app it shows the window.
let arguments = Array(CommandLine.arguments.dropFirst())
if let first = arguments.first, !first.hasPrefix("-") || first == "--help" || first == "-h" || first == "--version" {
    exit(runCommand(arguments))
}
// Started through a link, the program is not running as the app: open the app itself.
if Bundle.main.bundleIdentifier == nil, let app = Runtime.app {
    let open = Process()
    open.executableURL = URL(fileURLWithPath: "/usr/bin/open")
    open.arguments = [app.path]
    do { try open.run() } catch {
        FileHandle.standardError.write(Data("hadron: cannot open \(app.path): \(error.localizedDescription)\n".utf8))
        exit(1)
    }
    open.waitUntilExit()
    exit(open.terminationStatus)
}
HadronApp.main()
