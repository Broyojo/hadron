import Foundation

struct SteamGame: Identifiable {
    let id: String
    let name: String
}

enum OpenGLRenderer: String, CaseIterable, Identifiable {
    case automatic, zink, native

    var id: String { rawValue }

    var label: String {
        switch self {
        case .automatic: "Default (Zink)"
        case .zink: "Zink"
        case .native: "Apple OpenGL"
        }
    }
}

enum GameSettings {
    static let configURL = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/Hadron/games.conf")

    static let steamRoot = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/Steam")

    /// Steam writes one manifest per installed game in each library's steamapps directory.
    static func installedGames(in steamRoot: URL = steamRoot) -> [SteamGame] {
        let manager = FileManager.default
        let primary = steamRoot.appendingPathComponent("steamapps")
        var libraries: Set<URL> = [primary]
        let folders = primary.appendingPathComponent("libraryfolders.vdf")
        if let text = try? String(contentsOf: folders, encoding: .utf8) {
            for path in vdfValues("path", in: text) {
                libraries.insert(URL(fileURLWithPath: path).appendingPathComponent("steamapps"))
            }
        }

        var games: [String: SteamGame] = [:]
        for library in libraries {
            guard let files = try? manager.contentsOfDirectory(at: library, includingPropertiesForKeys: nil) else { continue }
            for file in files where file.lastPathComponent.hasPrefix("appmanifest_") && file.pathExtension == "acf" {
                let id = String(file.deletingPathExtension().lastPathComponent.dropFirst("appmanifest_".count))
                guard validAppID(id), let text = try? String(contentsOf: file, encoding: .utf8) else { continue }
                let name = vdfValues("name", in: text).first ?? id
                games[id] = SteamGame(id: id, name: name)
            }
        }
        return games.values.sorted {
            let order = $0.name.localizedStandardCompare($1.name)
            return order == .orderedSame ? $0.id < $1.id : order == .orderedAscending
        }
    }

    static func renderer(for appID: String, in file: URL = configURL) -> OpenGLRenderer {
        guard let text = try? String(contentsOf: file, encoding: .utf8) else { return .automatic }
        for line in text.split(separator: "\n") {
            let settingText = line.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false).first ?? ""
            let words = settingText.split(whereSeparator: \.isWhitespace)
            guard words.first == Substring(appID) else { continue }
            if let setting = words.dropFirst().first(where: { $0.hasPrefix("HADRON_OPENGL=") }) {
                return OpenGLRenderer(rawValue: String(setting.dropFirst("HADRON_OPENGL=".count))) ?? .automatic
            }
        }
        return .automatic
    }

    /// Keep every other game and setting intact. Steam's launch options still take precedence.
    static func setRenderer(_ renderer: OpenGLRenderer, for appID: String, in file: URL = configURL) throws {
        guard validAppID(appID) else { throw SettingsError.invalidAppID }
        let manager = FileManager.default
        let original = manager.fileExists(atPath: file.path) ? try String(contentsOf: file, encoding: .utf8) : ""
        var lines = original.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        if lines.last == "" { lines.removeLast() }
        var updated: [String] = []
        for line in lines {
            let parts = line.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false)
            let words = (parts.first ?? "").split(whereSeparator: \.isWhitespace).map(String.init)
            guard words.first == appID, words.dropFirst().contains(where: { $0.hasPrefix("HADRON_OPENGL=") }) else {
                updated.append(line)
                continue
            }
            let kept = words.filter { !$0.hasPrefix("HADRON_OPENGL=") }
            if kept.count > 1 {
                updated.append(kept.joined(separator: " ") + (parts.count > 1 ? " #" + parts[1] : ""))
            } else if parts.count > 1 {
                updated.append("#" + parts[1])
            }
        }
        if renderer != .automatic { updated.append("\(appID) HADRON_OPENGL=\(renderer.rawValue)") }
        let result = updated.isEmpty ? "" : updated.joined(separator: "\n") + "\n"
        if result == original { return }
        try manager.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try result.write(to: file, atomically: true, encoding: .utf8)
    }

    private static func validAppID(_ id: String) -> Bool {
        !id.isEmpty && id.utf8.allSatisfy { (48...57).contains($0) }
    }

    private static func vdfValues(_ key: String, in text: String) -> [String] {
        let pattern = "(?m)^\\s*\"" + NSRegularExpression.escapedPattern(for: key) + "\"\\s+\"((?:\\\\.|[^\"\\\\])*)\""
        guard let regex = try? NSRegularExpression(pattern: pattern) else { return [] }
        let range = NSRange(text.startIndex..<text.endIndex, in: text)
        return regex.matches(in: text, range: range).compactMap { match in
            guard let valueRange = Range(match.range(at: 1), in: text) else { return nil }
            return String(text[valueRange]).replacingOccurrences(of: "\\\\", with: "\\")
                .replacingOccurrences(of: "\\\"", with: "\"")
        }
    }

    private enum SettingsError: LocalizedError {
        case invalidAppID
        var errorDescription: String? { "The Steam app ID must contain only digits." }
    }
}
