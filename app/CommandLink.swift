import Foundation

/// The `hadron` command for a terminal. Homebrew links it when it installs the app; nothing runs
/// when the app is dragged from the disk image, so there the window offers to link it.
enum CommandLink {
    static let directory = "/usr/local/bin"

    enum State {
        /// A `hadron` on the usual PATH is this program.
        case linked
        /// There is none, or one that is a link to somewhere else, such as where the app used to be.
        case missing
        /// Something that is not a link has the name: left alone.
        case taken
    }

    static func state(directories: [String] = [directory, "/opt/homebrew/bin"]) -> State {
        for directory in directories {
            let link = URL(fileURLWithPath: directory).appendingPathComponent("hadron")
            if link.resolvingSymlinksInPath().path == Runtime.executable.path { return .linked }
        }
        let attributes = try? FileManager.default.attributesOfItem(atPath: directories[0] + "/hadron")
        if let type = attributes?[.type] as? FileAttributeType, type != .typeSymbolicLink { return .taken }
        return .missing
    }

    /// The shell command that makes the link.
    static func command(directory: String = directory) -> String {
        "/bin/mkdir -p \(quoted(directory)) && /bin/ln -sfh \(quoted(Runtime.executable.path)) \(quoted(directory + "/hadron"))"
    }

    /// Make the link. /usr/local/bin belongs to root, so macOS asks for an administrator's
    /// password. Returns what went wrong, or nil when the link is there or the user said no.
    static func install() -> String? {
        let script = "do shell script \"\(escaped(command()))\" "
            + "with prompt \"Hadron wants to add the hadron command to \(directory).\" with administrator privileges"
        var error: NSDictionary?
        NSAppleScript(source: script)?.executeAndReturnError(&error)
        guard let error else { return nil }
        if error[NSAppleScript.errorNumber] as? Int == -128 { return nil }
        return error[NSAppleScript.errorMessage] as? String ?? "could not add the hadron command to \(directory)"
    }

    private static func quoted(_ string: String) -> String {
        "'" + string.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }

    private static func escaped(_ string: String) -> String {
        string.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
    }
}
