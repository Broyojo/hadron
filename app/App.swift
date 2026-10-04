import AppKit
import SwiftUI

/// The window: how the Steam setup stands, and the few things to do about it. Hadron is not
/// needed while playing, so the app quits with its window.
struct HadronApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate

    var body: some Scene {
        Window("Hadron", id: "main") {
            ContentView()
        }
        .windowResizability(.contentSize)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

@MainActor
final class Model: ObservableObject {
    @Published var state = "unknown"
    @Published var message = "Checking…"
    @Published var busy = false
    @Published var output = ""
    @Published var failed = false

    func refresh() {
        Task.detached {
            let status = Runtime.status()
            await MainActor.run {
                self.state = status.state
                self.message = status.message
            }
        }
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
        panel.directoryURL = FileManager.default.urls(for: .desktopDirectory, in: .userDomainMask).first
        guard panel.runModal() == .OK, let url = panel.url else { return }
        perform("report", [url.path]) { ok in
            if ok { NSWorkspace.shared.activateFileViewerSelecting([url]) }
        }
    }
}

struct ContentView: View {
    @StateObject private var model = Model()

    private var isSetUp: Bool { model.state == "installed" }
    private var hasSteam: Bool { model.state != "no-steam" }
    /// macOS asks before one app may change another; a refusal shows up as this error.
    private var blockedByMacOS: Bool { model.failed && model.output.contains("Operation not permitted") }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(spacing: 14) {
                Image(nsImage: NSApp.applicationIconImage).resizable().frame(width: 56, height: 56)
                VStack(alignment: .leading, spacing: 2) {
                    Text("Hadron").font(.title2.weight(.semibold))
                    Text("Version \(Runtime.version)").font(.callout).foregroundStyle(.secondary)
                }
            }

            HStack(spacing: 8) {
                Image(systemName: isSetUp ? "checkmark.circle.fill" : "exclamationmark.circle.fill")
                    .foregroundStyle(isSetUp ? .green : .orange)
                Text(model.message)
            }

            Text("Setting up closes Steam, adds Hadron to it as a Steam Play tool and signs Steam again. "
                 + "After that, install and play Windows games from Steam as usual; Hadron does not need to stay open.")
                .font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)

            HStack {
                if hasSteam {
                    Button(isSetUp || model.state == "not-installed" ? (isSetUp ? "Repair" : "Set up Steam") : "Repair") {
                        model.perform("steam-install")
                    }
                    .keyboardShortcut(isSetUp ? nil : .defaultAction)
                    Button("Uninstall") { model.perform("steam-uninstall") }
                        .disabled(model.state == "not-installed")
                    if isSetUp {
                        Button("Open Steam") { NSWorkspace.shared.open(URL(fileURLWithPath: "/Applications/Steam.app")) }
                    }
                } else {
                    Button("Get Steam") { NSWorkspace.shared.open(URL(string: "https://store.steampowered.com/about/")!) }
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
        }
        .padding(20)
        .frame(width: 520)
        .onAppear { model.refresh() }
    }
}
