#if SPARKLE
import AppKit
import Sparkle

/// Updates in a released app, through Sparkle. It reads the feed attached to the latest release
/// on GitHub when the app is open, shows what is new, and once the user agrees downloads the disk
/// image, checks its signature against the key in the app and replaces the app. Nothing is
/// downloaded or installed unasked, and nothing about the user or the Mac is sent.
///
/// Replacing the app does not touch a running game: games run from a copy of the runtime kept
/// outside the app (scripts/runtime-versions.sh), and the next one started uses the new build.
final class SparkleUpdates {
    static let shared = SparkleUpdates()

    /// A build made for release names its feed (scripts/build-app.sh --updates); no other build
    /// has an updater.
    static var isAvailable: Bool { Bundle.main.object(forInfoDictionaryKey: "SUFeedURL") != nil }

    private var controller: SPUStandardUpdaterController?

    func start() {
        guard controller == nil else { return }
        controller = SPUStandardUpdaterController(startingUpdater: true, updaterDelegate: nil, userDriverDelegate: nil)
    }

    func check() { controller?.checkForUpdates(nil) }
}
#endif
