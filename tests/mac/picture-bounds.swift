// Where a window capture holds a picture and where it is black, to measure a letterbox (findings
// #66): capture only the test's window (`screencapture -x -o -l <window id> out.png`), then run
//   picture-bounds out.png
// It prints the capture's size, the bounding box of every pixel that isn't black (a channel above
// the threshold, 16 of 255 by default), and how many black rows and columns lie above, below, left
// and right of it. A letterbox of 1280x720 on a 3024x1964 display is 3024x1701 at row 132, with
// 132 black rows above and 131 below.
//
// Build: swiftc -O -o picture-bounds tests/mac/picture-bounds.swift
// Usage: picture-bounds <image> [threshold]
import CoreGraphics
import Foundation
import ImageIO

let args = CommandLine.arguments
guard args.count >= 2,
      let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: args[1]) as CFURL, nil),
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
    FileHandle.standardError.write("usage: picture-bounds <image> [threshold]\n".data(using: .utf8)!)
    exit(2)
}
let threshold = args.count > 2 ? UInt8(args[2]) ?? 16 : 16
let width = image.width, height = image.height
var pixels = [UInt8](repeating: 0, count: width * height * 4)
// Drawn into a known layout (RGBA, 8 bits each), whatever the capture's own format is.
guard let context = CGContext(data: &pixels, width: width, height: height, bitsPerComponent: 8,
                              bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                              bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { exit(2) }
context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))

var top = height, bottom = -1, left = width, right = -1
for y in 0..<height {
    for x in 0..<width {
        let i = (y * width + x) * 4
        if pixels[i] > threshold || pixels[i + 1] > threshold || pixels[i + 2] > threshold {
            top = min(top, y); bottom = max(bottom, y)
            left = min(left, x); right = max(right, x)
        }
    }
}
print("capture \(width)x\(height)")
if bottom < 0 {
    print("all black")
} else {
    print("picture columns \(left)-\(right) (\(right - left + 1)), rows \(top)-\(bottom) (\(bottom - top + 1))")
    print("black: \(top) rows above, \(height - 1 - bottom) below, \(left) columns left, \(width - 1 - right) right")
}
