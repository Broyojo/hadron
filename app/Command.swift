import Foundation

/// `hadron <command>`: what the window's buttons do, from a terminal.
func runCommand(_ arguments: [String]) -> Int32 {
    let print: (String) -> Void = { Swift.print($0) }
    switch arguments[0] {
    case "setup", "repair":
        return Runtime.run("steam-install", line: print)
    case "uninstall":
        return Runtime.run("steam-uninstall", line: print)
    case "status":
        let status = Runtime.status()
        Swift.print(status.message.isEmpty ? status.state : status.message)
        return status.state == "installed" ? 0 : 1
    case "report":
        return Runtime.run("report", Array(arguments.dropFirst()), line: print)
    case "update":
        return update()
    case "version", "--version":
        Swift.print("Hadron \(Runtime.version)")
        return 0
    case "help", "--help", "-h":
        Swift.print(usage)
        return 0
    default:
        FileHandle.standardError.write(Data("hadron: unknown command '\(arguments[0])'\n\n\(usage)\n".utf8))
        return 2
    }
}

/// `hadron update`. A build that was not made for release has no updater and is told where the
/// releases are.
private func update() -> Int32 {
    #if SPARKLE
    if let status = TerminalUpdate.run() { return status }
    #endif
    Swift.print("This build of Hadron does not update itself. The latest release: https://github.com/Broyojo/hadron/releases/latest")
    return 1
}

private let usage = """
usage: hadron <command>

  setup       add Hadron to Steam as a Steam Play tool (closes Steam while it works)
  repair      the same: put the setup back after a Steam update or after moving Hadron
  uninstall   take Hadron out of Steam again
  status      say whether Steam is set up
  report      save a report file for a GitHub issue (optionally: hadron report <file.zip>)
  update      install the latest version of Hadron, if this is not it
  version     print Hadron's version

Without a command, Hadron opens its window.
"""
