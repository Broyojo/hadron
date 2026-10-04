import Foundation

/// Whether a newer Hadron has been released: one request to GitHub's list of releases, made when
/// the window opens. It sends nothing about the user or the Mac, and installs nothing.
enum Updates {
    static let latest = URL(string: "https://api.github.com/repos/Broyojo/hadron/releases/latest")!

    static func check(_ found: @escaping @MainActor (_ version: String, _ page: URL) -> Void) {
        var request = URLRequest(url: latest)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.timeoutInterval = 10
        URLSession.shared.dataTask(with: request) { data, response, _ in
            guard let data = data, (response as? HTTPURLResponse)?.statusCode == 200,
                  let release = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let tag = release["tag_name"] as? String,
                  let page = (release["html_url"] as? String).flatMap({ URL(string: $0) }) else { return }
            let version = tag.hasPrefix("v") ? String(tag.dropFirst()) : tag
            if isNewer(version, than: Runtime.version) {
                Task { @MainActor in found(version, page) }
            }
        }.resume()
    }

    /// 0.2.0 is newer than 0.1.3; a development build (0.1.0-dev.abc1234) counts as its release.
    static func isNewer(_ a: String, than b: String) -> Bool {
        let x = numbers(a), y = numbers(b)
        for i in 0..<max(x.count, y.count) {
            let p = i < x.count ? x[i] : 0, q = i < y.count ? y[i] : 0
            if p != q { return p > q }
        }
        return false
    }

    private static func numbers(_ version: String) -> [Int] {
        (version.split(separator: "-").first ?? "").split(separator: ".").map { Int($0) ?? 0 }
    }
}
