# The Homebrew cask for Hadron. It lives in the tap (github.com/Broyojo/homebrew-hadron, as
# Casks/hadron.rb); scripts/release.sh fills in the version and the checksum of each release.
#
#   brew install --cask broyojo/hadron/hadron
cask "hadron" do
  version "@VERSION@"
  sha256 "@SHA256@"

  url "https://github.com/Broyojo/hadron/releases/download/v#{version}/Hadron-#{version}.dmg"
  name "Hadron"
  desc "Play Windows games from the Steam library on Apple Silicon"
  homepage "https://github.com/Broyojo/hadron"

  depends_on arch: :arm64
  depends_on cask: "steam"

  app "Hadron.app"
  # The same program is the `hadron` command: setup, repair, uninstall, status, report, version.
  binary "#{appdir}/Hadron.app/Contents/MacOS/Hadron", target: "hadron"

  # Take Hadron out of Steam before the app goes, so Steam is not left pointing at it.
  uninstall_preflight do
    system_command "#{appdir}/Hadron.app/Contents/MacOS/Hadron", args: ["uninstall"], must_succeed: false
  end

  zap trash: [
    "~/Library/Application Support/Hadron",
    "~/Library/Caches/Hadron",
    "~/Library/Logs/Hadron",
  ]

  caveats <<~EOS
    Open Hadron once and choose "Set up Steam", or run:
      hadron setup
    macOS will ask you to allow Hadron under Privacy & Security, App Management the first time.
  EOS
end
