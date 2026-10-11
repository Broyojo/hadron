#if SPARKLE
import Foundation
import Sparkle

/// `hadron update`: Sparkle's update with a terminal in place of its window. The feed, the
/// signature check and the installation are Sparkle's, as in the app; this only says what is
/// happening. Asking for the update is the consent the window's Install button gives.
final class TerminalUpdate: NSObject, SPUUserDriver, SPUUpdaterDelegate {
    private var finished = false
    private var status: Int32 = 0
    private var expected: UInt64 = 0
    private var received: UInt64 = 0
    private var version = ""
    private let onTerminal = isatty(STDOUT_FILENO) != 0

    /// The exit status, or nil when this app has no updater.
    static func run() -> Int32? {
        guard let app = Runtime.app, let bundle = Bundle(url: app),
              bundle.object(forInfoDictionaryKey: "SUFeedURL") != nil else { return nil }
        let driver = TerminalUpdate()
        let updater = SPUUpdater(hostBundle: bundle, applicationBundle: bundle, userDriver: driver, delegate: driver)
        do { try updater.start() } catch {
            driver.fail(error)
            return 1
        }
        updater.checkForUpdates()
        while !driver.finished {
            RunLoop.main.run(mode: .default, before: Date(timeIntervalSinceNow: 0.2))
        }
        return driver.status
    }

    private func say(_ text: String) { print(text); fflush(stdout) }

    private func fail(_ error: Error) {
        FileHandle.standardError.write(Data("hadron: \(error.localizedDescription)\n".utf8))
        status = 1
    }

    /// The window is for whoever opens it; an update asked for in a terminal ends there.
    func updaterShouldRelaunchApplication(_ updater: SPUUpdater) -> Bool { false }

    // What the updater would show in its window.

    func show(_ request: SPUUpdatePermissionRequest, reply: @escaping (SUUpdatePermissionResponse) -> Void) {
        reply(SUUpdatePermissionResponse(automaticUpdateChecks: true, sendSystemProfile: false))
    }

    func showUserInitiatedUpdateCheck(cancellation: @escaping () -> Void) {
        say("Looking for a newer version…")
    }

    func showUpdateFound(with appcastItem: SUAppcastItem, state: SPUUserUpdateState, reply: @escaping (SPUUserUpdateChoice) -> Void) {
        version = appcastItem.displayVersionString
        say("Hadron \(version) is available (this is \(Runtime.version)).")
        if appcastItem.isInformationOnlyUpdate {
            say("It is not installed from here: \(appcastItem.infoURL?.absoluteString ?? "https://github.com/Broyojo/hadron/releases/latest")")
            reply(.dismiss)
            finished = true
        } else {
            reply(.install)
        }
    }

    func showUpdateReleaseNotes(with downloadData: SPUDownloadData) {}

    func showUpdateReleaseNotesFailedToDownloadWithError(_ error: Error) {}

    func showUpdateNotFoundWithError(_ error: Error, acknowledgement: @escaping () -> Void) {
        // "You're up to date" is the usual reason; Sparkle's sentence says which it was.
        say(error.localizedDescription)
        acknowledgement()
        finished = true
    }

    func showUpdaterError(_ error: Error, acknowledgement: @escaping () -> Void) {
        endProgress()
        fail(error)
        acknowledgement()
        finished = true
    }

    func showDownloadInitiated(cancellation: @escaping () -> Void) {
        say("Downloading…")
    }

    func showDownloadDidReceiveExpectedContentLength(_ expectedContentLength: UInt64) {
        expected = expectedContentLength
    }

    func showDownloadDidReceiveData(ofLength length: UInt64) {
        received += length
        guard onTerminal, expected > 0 else { return }
        print(String(format: "\r  %d of %d MB", Int(received / 1_000_000), Int(expected / 1_000_000)), terminator: "")
        fflush(stdout)
    }

    private func endProgress() {
        if onTerminal, received > 0 { print("") }
        received = 0
    }

    func showDownloadDidStartExtractingUpdate() {
        endProgress()
        say("Checking the signature and unpacking…")
    }

    func showExtractionReceivedProgress(_ progress: Double) {}

    func showReady(toInstallAndRelaunch reply: @escaping (SPUUserUpdateChoice) -> Void) {
        reply(.install)
    }

    func showInstallingUpdate(withApplicationTerminated applicationTerminated: Bool, retryTerminatingApplication: @escaping () -> Void) {
        say(applicationTerminated ? "Installing…" : "Hadron's window is open: close it, and the update is installed.")
    }

    /// The new app says how Steam stands with it: usually that the next game uses it, sometimes
    /// that Steam's own part changed.
    func showUpdateInstalledAndRelaunched(_ relaunched: Bool, acknowledgement: @escaping () -> Void) {
        say("Hadron \(version) is installed.")
        let status = Runtime.status()
        if !status.message.isEmpty { say(status.message) }
        if status.state == "outdated" || status.state == "broken" { say("From a terminal: hadron repair") }
        acknowledgement()
        finished = true
    }

    func dismissUpdateInstallation() {
        finished = true
    }

    func showUpdateInFocus() {}
}
#endif
