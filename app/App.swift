import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// The window: how the Steam setup stands, and the few things to do about it. Hadron is not
/// needed while playing, so the app quits with its window.
struct HadronApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate

    var body: some Scene {
        Window("Hadron", id: "main") {
            ContentView()
        }
        .windowResizability(.contentSize)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open a Windows Program…") { Programs.choose() }
                    .keyboardShortcut("o")
            }
        }
        #if SPARKLE
        .commands {
            CommandGroup(after: .appInfo) {
                Button("Check for Updates…") { SparkleUpdates.shared.check() }
                    .disabled(!SparkleUpdates.isAvailable)
            }
        }
        #endif
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    /// An .exe opened with Hadron: double-clicked once Hadron is its app, dropped on the icon, or Open With.
    func application(_ application: NSApplication, open urls: [URL]) {
        Programs.start(urls)
    }
}

/// Windows programs that are not Steam games (scripts/program-run). They run on their own: Hadron can
/// quit while they do.
@MainActor
enum Programs {
    static func choose() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType("com.microsoft.windows-executable") ?? .executable]
        panel.allowsMultipleSelection = false
        panel.message = "Choose a Windows program to run."
        if panel.runModal() == .OK { start(panel.urls) }
    }

    static func start(_ urls: [URL]) {
        for url in urls {
            let name = url.lastPathComponent
            let problem = Runtime.start(program: url) { problem in
                Task { @MainActor in report(name, problem) }
            }
            if let problem { report(name, problem) }
        }
    }

    private static func report(_ name: String, _ problem: String) {
        let alert = NSAlert()
        alert.messageText = "Hadron could not start \(name)."
        alert.informativeText = problem
        alert.runModal()
    }
}

@MainActor
final class Model: ObservableObject {
    @Published var state = "unknown"
    @Published var message = "Checking…"
    @Published var busy = false
    @Published var output = ""
    @Published var failed = false
    @Published var newVersion: String?
    @Published var newVersionPage: URL?
    @Published var command = CommandLink.State.linked

    func refresh() {
        Task.detached {
            let status = Runtime.status()
            let command = Runtime.app == nil ? .linked : CommandLink.state()
            await MainActor.run {
                self.state = status.state
                self.message = status.message
                self.command = command
            }
        }
    }

    func installCommand() {
        output = ""
        failed = false
        if let problem = CommandLink.install() {
            output = problem + "\n"
            failed = true
        }
        command = CommandLink.state()
    }

    /// Run a script off the main thread, showing what it prints.
    func perform(_ script: String, _ arguments: [String] = [], then: @escaping @MainActor (Bool) -> Void = { _ in }) {
        busy = true
        failed = false
        output = ""
        Task.detached {
            let status = Runtime.run(script, arguments) { line in
                Task { @MainActor in self.output += line + "\n" }
            }
            await MainActor.run {
                self.busy = false
                self.failed = status != 0
                self.refresh()
                then(status == 0)
            }
        }
    }

    func saveReport() {
        let panel = NSSavePanel()
        let stamp = DateFormatter()
        stamp.dateFormat = "yyyyMMdd-HHmmss"
        panel.nameFieldStringValue = "Hadron-report-\(stamp.string(from: Date())).zip"
        // The folder the last report went to, or the Desktop the first time.
        let folder = UserDefaults.standard.url(forKey: "reportFolder")
        panel.directoryURL = folder.flatMap { FileManager.default.fileExists(atPath: $0.path) ? $0 : nil }
            ?? FileManager.default.urls(for: .desktopDirectory, in: .userDomainMask).first
        guard panel.runModal() == .OK, let url = panel.url else { return }
        UserDefaults.standard.set(url.deletingLastPathComponent(), forKey: "reportFolder")
        perform("report", [url.path]) { ok in
            if ok { NSWorkspace.shared.activateFileViewerSelecting([url]) }
        }
    }
}

struct ContentView: View {
    @StateObject private var model = Model()

    private var isSetUp: Bool { model.state == "installed" }
    private var hasSteam: Bool { model.state != "no-steam" }
    private var needsRepair: Bool { model.state == "outdated" || model.state == "broken" }
    /// macOS asks before one app may change another; a refusal shows up as this error.
    private var blockedByMacOS: Bool { model.failed && model.output.contains("Operation not permitted") }

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            HStack(spacing: 12) {
                Image(nsImage: NSApp.applicationIconImage)
                    .resizable()
                    .frame(width: 48, height: 48)
                VStack(alignment: .leading, spacing: 3) {
                    Text("Hadron").font(.title2.weight(.semibold))
                    Text("Version \(Runtime.version)")
                        .font(.subheadline).foregroundStyle(.secondary)
                }
                Spacer()
            }

            if let version = model.newVersion, let page = model.newVersionPage {
                HStack(spacing: 8) {
                    Image(systemName: "arrow.down.circle.fill").foregroundStyle(.blue)
                    Text("Hadron \(version) is available.")
                    Button("Get it") { NSWorkspace.shared.open(page) }
                }
                .font(.subheadline)
            }

            HStack(alignment: .top, spacing: 14) {
                Image(systemName: isSetUp ? "checkmark.circle.fill" : "exclamationmark.circle.fill")
                    .font(.title2)
                    .foregroundStyle(isSetUp ? .green : .orange)
                VStack(alignment: .leading, spacing: 5) {
                    Text(isSetUp ? "Ready to play" : needsRepair ? "Steam needs repair" : hasSteam ? "Set up Steam Play" : "Steam is needed")
                        .font(.headline)
                    Text(model.message)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 0)
            }
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(.quaternary.opacity(0.55), in: RoundedRectangle(cornerRadius: 12))

            VStack(alignment: .leading, spacing: 8) {
                Text(isSetUp ? "Play from Steam" : "How setup works")
                    .font(.headline)
                Text(isSetUp
                     ? "Install and launch Windows games in Steam as usual. Hadron does not need to stay open."
                     : "Setup briefly closes Steam and adds Hadron to it as a Steam Play tool. After that, start games from Steam as usual. The first time, macOS may ask you to allow App Management under Privacy & Security.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            HStack(spacing: 10) {
                if hasSteam {
                    if isSetUp {
                        Button("Open Steam") { NSWorkspace.shared.open(URL(fileURLWithPath: "/Applications/Steam.app")) }
                            .buttonStyle(.borderedProminent)
                            .keyboardShortcut(.defaultAction)
                        Button("Repair") { model.perform("steam-install") }
                    } else {
                        Button(model.state == "not-installed" ? "Set up Steam" : "Repair") {
                            model.perform("steam-install")
                        }
                        .buttonStyle(.borderedProminent)
                        .keyboardShortcut(.defaultAction)
                    }
                } else {
                    Button("Get Steam") { NSWorkspace.shared.open(URL(string: "https://store.steampowered.com/about/")!) }
                        .buttonStyle(.borderedProminent)
                    Button("Check again") { model.refresh() }
                }
                Spacer()
                Button("Save a report…") { model.saveReport() }
            }
            .disabled(model.busy)

            if model.busy { ProgressView().controlSize(.small) }

            if blockedByMacOS {
                VStack(alignment: .leading, spacing: 6) {
                    Text("macOS did not let Hadron change Steam. Allow Hadron under Privacy & Security, App Management, then try again.")
                        .fixedSize(horizontal: false, vertical: true)
                    Button("Open Privacy & Security") {
                        NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AppBundles")!)
                    }
                }
            }

            // What a script printed matters when it failed; a success shows in the status line.
            if model.failed && !model.output.isEmpty {
                ScrollView {
                    Text(model.output).font(.caption.monospaced()).textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(height: 110)
                .padding(8)
                .background(.quaternary.opacity(0.5), in: RoundedRectangle(cornerRadius: 6))
            }

            Button("Open a Windows program that is not on Steam…") { Programs.choose() }
                .buttonStyle(.link).font(.callout)

            if model.command == .missing {
                Button("Add the hadron command for Terminal…") { model.installCommand() }
                    .buttonStyle(.link).font(.callout).disabled(model.busy)
            }

            if hasSteam && model.state != "not-installed" {
                Divider()
                HStack {
                    Spacer()
                    Button("Remove from Steam") { model.perform("steam-uninstall") }
                        .buttonStyle(.link)
                        .font(.caption)
                        .disabled(model.busy)
                }
            }
        }
        .padding(24)
        .frame(width: 560)
        .onAppear {
            model.refresh()
            // A released app updates itself; any other build only says that there is a newer one.
            #if SPARKLE
            if SparkleUpdates.isAvailable {
                SparkleUpdates.shared.start()
                return
            }
            #endif
            Updates.check { version, page in
                model.newVersion = version
                model.newVersionPage = page
            }
        }
    }
}
