import Foundation

// One program with two faces: given a command it is `hadron`, opened as an app it shows the window.
let arguments = Array(CommandLine.arguments.dropFirst())
if let first = arguments.first, !first.hasPrefix("-") || first == "--help" || first == "-h" || first == "--version" {
    exit(runCommand(arguments))
}
HadronApp.main()
