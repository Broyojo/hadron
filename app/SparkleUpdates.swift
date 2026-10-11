#if SPARKLE
import AppKit
import Sparkle

/// Updates in a released app, through Sparkle. It reads the feed attached to the latest release
/// on GitHub when the app is open, shows what is new, and once the user agrees downloads the disk
/// image, checks its signature against the key in the app and replaces the app. Nothing is
/// downloaded or installed unasked, and nothing about the user or the Mac is sent.
final class SparkleUpdates: NSObject, SPUUpdaterDelegate {
    static let shared = SparkleUpdates()

    /// A build made for release names its feed (scripts/build-app.sh --updates); no other build
    /// has an updater.
    static var isAvailable: Bool { Bundle.main.object(forInfoDictionaryKey: "SUFeedURL") != nil }

    private var controller: SPUStandardUpdaterController?
    private var waiting: Timer?

    func start() {
        guard controller == nil else { return }
        controller = SPUStandardUpdaterController(startingUpdater: true, updaterDelegate: self, userDriverDelegate: nil)
    }

    func check() { controller?.checkForUpdates(nil) }

    /// The app holds the runtime games run from, and a game reads from it for as long as it runs.
    static var gameIsRunning: Bool { Runtime.run("running") == 0 }
    private var gameIsRunning: Bool { Self.gameIsRunning }

    static func refuseWhileGameRuns() throws {
        if gameIsRunning {
            throw NSError(domain: "com.broyojo.hadron", code: 1, userInfo: [
                NSLocalizedDescriptionKey: "Hadron is not updated while a game is running. Quit the game, then check again.",
            ])
        }
    }

    func updater(_ updater: SPUUpdater, mayPerform updateCheck: SPUUpdateCheck) throws {
        try Self.refuseWhileGameRuns()
    }

    /// A game started after the check: the app is replaced when it has been quit.
    func updater(_ updater: SPUUpdater, shouldPostponeRelaunchForUpdate item: SUAppcastItem,
                 untilInvokingBlock installHandler: @escaping () -> Void) -> Bool {
        guard gameIsRunning else { return false }
        let alert = NSAlert()
        alert.messageText = "A game is running."
        alert.informativeText = "Hadron \(item.displayVersionString) is installed when the game has been quit."
        alert.runModal()
        waiting = Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] timer in
            guard let self = self, !self.gameIsRunning else { return }
            timer.invalidate()
            self.waiting = nil
            installHandler()
        }
        return true
    }
}
#endif
