import Foundation

@main
struct GameSettingsTests {
    static func main() throws {
        let manager = FileManager.default
        let root = manager.temporaryDirectory.appendingPathComponent("hadron-settings-\(UUID().uuidString)")
        defer { try? manager.removeItem(at: root) }

        let steam = root.appendingPathComponent("Steam")
        let primary = steam.appendingPathComponent("steamapps")
        let other = root.appendingPathComponent("Other Library/steamapps")
        try manager.createDirectory(at: primary, withIntermediateDirectories: true)
        try manager.createDirectory(at: other, withIntermediateDirectories: true)
        try "\"libraryfolders\"\n{\n\"1\"\n{\n\"path\" \"\(other.deletingLastPathComponent().path)\"\n}\n}\n"
            .write(to: primary.appendingPathComponent("libraryfolders.vdf"), atomically: true, encoding: .utf8)
        try "\"AppState\"\n{\n\"appid\" \"881100\"\n\"name\" \"Noita\"\n}\n"
            .write(to: primary.appendingPathComponent("appmanifest_881100.acf"), atomically: true, encoding: .utf8)
        try "\"AppState\"\n{\n\"appid\" \"123\"\n\"name\" \"Another Game\"\n}\n"
            .write(to: other.appendingPathComponent("appmanifest_123.acf"), atomically: true, encoding: .utf8)
        let games = GameSettings.installedGames(in: steam)
        precondition(games.map(\.id) == ["123", "881100"])
        precondition(games.map(\.name) == ["Another Game", "Noita"])

        let config = root.appendingPathComponent("Hadron/games.conf")
        try manager.createDirectory(at: config.deletingLastPathComponent(), withIntermediateDirectories: true)
        let initial = "# 881100 HADRON_OPENGL=native\n881100 HADRON_VSYNC=off HADRON_OPENGL=zink # keep\n123 HADRON_OPENGL=native\n881100 HADRON_OPENGL=native\n"
        try initial.write(to: config, atomically: true, encoding: .utf8)
        precondition(GameSettings.renderer(for: "881100", in: config) == .zink)
        try GameSettings.setRenderer(.native, for: "881100", in: config)
        let native = try String(contentsOf: config, encoding: .utf8)
        precondition(native.contains("881100 HADRON_VSYNC=off # keep"))
        precondition(native.contains("123 HADRON_OPENGL=native"))
        precondition(native.split(separator: "\n").filter { $0 == "881100 HADRON_OPENGL=native" }.count == 1)
        precondition(GameSettings.renderer(for: "881100", in: config) == .native)
        try GameSettings.setRenderer(.native, for: "881100", in: config)
        let again = try String(contentsOf: config, encoding: .utf8)
        precondition(again == native)
        try GameSettings.setRenderer(.automatic, for: "881100", in: config)
        let reset = try String(contentsOf: config, encoding: .utf8)
        precondition(reset.contains("881100 HADRON_VSYNC=off # keep"))
        precondition(reset.contains("123 HADRON_OPENGL=native"))
        precondition(GameSettings.renderer(for: "881100", in: config) == .automatic)

        let fresh = root.appendingPathComponent("New/games.conf")
        try GameSettings.setRenderer(.zink, for: "881100", in: fresh)
        precondition(GameSettings.renderer(for: "881100", in: fresh) == .zink)
        try GameSettings.setRenderer(.automatic, for: "881100", in: fresh)
        precondition(GameSettings.renderer(for: "881100", in: fresh) == .automatic)
        print("Game settings tests passed")
    }
}
