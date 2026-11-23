#!/usr/bin/env swift
// Verify pixels in a simctl screenshot against the app's post-layout report.
// Usage: xcrun swift tools/check_iwi_screenshot.swift image.png report.json
//        xcrun swift tools/check_iwi_screenshot.swift --self-test
import AppKit
import Foundation
import Darwin

struct Sample: Decodable {
    let u: Double
    let v: Double
    let rgba: [Int]
}

struct Report: Decodable {
    let status: String
    let window_width: Double
    let window_height: Double
    let samples: [Sample]
}

struct CheckFailure: Error, CustomStringConvertible {
    let description: String
    init(_ message: String) { description = message }
}

struct Observation {
    let index: Int
    let x: Int
    let y: Int
    let expected: [Int]
    let actual: [Int]
    let passed: Bool

    var json: [String: Any] {
        return ["index": index, "x": x, "y": y, "expected": expected,
                "actual": actual, "status": passed ? "pass" : "fail"]
    }
}

func emit(_ value: [String: Any]) throws {
    let bytes = try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
    FileHandle.standardOutput.write(bytes)
    FileHandle.standardOutput.write(Data([10]))
}

func check(_ bitmap: NSBitmapImageRep, _ report: Report) throws -> [Observation] {
    guard report.status == "pass" else {
        throw CheckFailure("The app did not report a successful IWI preview.")
    }
    guard bitmap.pixelsWide > 0, bitmap.pixelsHigh > 0,
          report.window_width.isFinite, report.window_width > 0,
          report.window_height.isFinite, report.window_height > 0 else {
        throw CheckFailure("The screenshot or reported window has invalid dimensions.")
    }
    let scaleX = Double(bitmap.pixelsWide) / report.window_width
    let scaleY = Double(bitmap.pixelsHigh) / report.window_height
    // The report describes the whole window, including the status/safe areas.
    // A crop, rotation or unrelated window must not move the sampling points.
    guard scaleX >= 1, scaleY >= 1,
          abs(scaleX - scaleY) / max(scaleX, scaleY) <= 0.01 else {
        throw CheckFailure("Screenshot dimensions do not match the reported window aspect ratio.")
    }
    guard report.samples.count == 4 else {
        throw CheckFailure("Expected exactly four fixture sample points.")
    }
    var observations: [Observation] = []
    for (index, sample) in report.samples.enumerated() {
        guard sample.u.isFinite, sample.v.isFinite,
              sample.u >= 0, sample.u < 1, sample.v >= 0, sample.v < 1,
              sample.rgba.count == 4, sample.rgba.allSatisfy({ (0...255).contains($0) }) else {
            throw CheckFailure("Sample \(index) has invalid normalized coordinates or RGBA values.")
        }
        // NSBitmapImageRep indexes its bitmap from the top-left, matching PNG
        // scanlines and the normalized UIKit window coordinates in the report.
        let x = Int(floor(sample.u * Double(bitmap.pixelsWide)))
        let y = Int(floor(sample.v * Double(bitmap.pixelsHigh)))
        guard let raw = bitmap.colorAt(x: x, y: y),
              let color = raw.usingColorSpace(.sRGB) ?? raw.usingColorSpace(.genericRGB) else {
            throw CheckFailure("Cannot read RGB color at sample \(index).")
        }
        let channels = [color.redComponent, color.greenComponent,
                        color.blueComponent, color.alphaComponent]
        guard channels.allSatisfy({ $0.isFinite }) else {
            throw CheckFailure("Sample \(index) contains a non-finite color component.")
        }
        let actual = channels.map { Int((min(1, max(0, $0)) * 255).rounded()) }
        let passed = zip(actual, sample.rgba).allSatisfy { abs($0.0 - $0.1) <= 6 }
        observations.append(Observation(index: index, x: x, y: y,
                                        expected: sample.rgba, actual: actual, passed: passed))
    }
    return observations
}

func readPNG(_ path: String) throws -> NSBitmapImageRep {
    let bytes = try Data(contentsOf: URL(fileURLWithPath: path))
    guard bytes.starts(with: [137, 80, 78, 71, 13, 10, 26, 10]),
          let bitmap = NSBitmapImageRep(data: bytes),
          bitmap.pixelsWide > 0, bitmap.pixelsHigh > 0 else {
        throw CheckFailure("The screenshot is not a readable nonempty PNG.")
    }
    return bitmap
}

func syntheticBitmap(_ colors: [[Int]]) throws -> NSBitmapImageRep {
    // CI on macOS 26 can create an unusable colorspace (-1) when a headless
    // AppKit process constructs NSBitmapImageRep(bitmapDataPlanes: nil,
    // colorSpaceName: .deviceRGB). Instead generate a canonical sRGB PNG
    // through CGImage, and decode it via the SAME readPNG path used for simctl.
    guard colors.count == 4,
          let colorSpace = CGColorSpace(name: CGColorSpace.sRGB) else {
        throw CheckFailure("Cannot initialize the checker self-test colorspace.")
    }
    var rgba = [UInt8](repeating: 0, count: 16 * 16 * 4)
    for y in 0..<16 {
        for x in 0..<16 {
            let channels = colors[(y >= 8 ? 2 : 0) + (x >= 8 ? 1 : 0)]
            guard channels.count == 4, channels.allSatisfy({ (0...255).contains($0) }) else {
                throw CheckFailure("Invalid checker self-test color.")
            }
            let index = (y * 16 + x) * 4
            for channel in 0..<4 {
                rgba[index + channel] = UInt8(channels[channel])
            }
        }
    }
    let byteBuffer = Data(rgba)
    guard let provider = CGDataProvider(data: byteBuffer as CFData),
          let cgImage = CGImage(
              width: 16, height: 16, bitsPerComponent: 8, bitsPerPixel: 32,
              bytesPerRow: 16 * 4, space: colorSpace,
              bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue)
                  .union(.byteOrder32Big),
              provider: provider, decode: nil, shouldInterpolate: false,
              intent: .defaultIntent),
          let png = NSBitmapImageRep(cgImage: cgImage)
              .representation(using: .png, properties: [:]),
          let bitmap = NSBitmapImageRep(data: png) else {
        throw CheckFailure("Cannot create the checker self-test PNG.")
    }
    return bitmap
}

func selfTest() throws {
    // These are screen colors after compositing over white: opaque red,
    // green with alpha 136/255, opaque blue, and a fully transparent quadrant.
    let colors = [[255, 0, 0, 255], [119, 255, 119, 255],
                  [0, 0, 255, 255], [255, 255, 255, 255]]
    let report = Report(status: "pass", window_width: 16, window_height: 16,
        samples: [Sample(u: 0.25, v: 0.25, rgba: colors[0]),
                  Sample(u: 0.75, v: 0.25, rgba: colors[1]),
                  Sample(u: 0.25, v: 0.75, rgba: colors[2]),
                  Sample(u: 0.75, v: 0.75, rgba: colors[3])])
    let valid = try check(syntheticBitmap(colors), report)
    guard valid.allSatisfy({ $0.passed }) else {
        try emit(["status": "fail", "self_test": "valid_image", "observations": valid.map { $0.json }])
        throw CheckFailure("Checker rejected its correctly colored self-test bitmap.")
    }
    let blank = try check(syntheticBitmap(Array(repeating: colors[3], count: 4)), report)
    guard !blank.allSatisfy({ $0.passed }) else {
        throw CheckFailure("Checker accepted a blank white bitmap.")
    }
    let swapped = try check(syntheticBitmap([colors[2], colors[1], colors[0], colors[3]]), report)
    guard !swapped.allSatisfy({ $0.passed }) else {
        throw CheckFailure("Checker accepted swapped red and blue quadrants.")
    }
    try emit(["status": "pass", "self_test": true,
              "valid_image": valid.map { $0.json },
              "blank_rejected": true, "swapped_colors_rejected": true])
}

do {
    let arguments = Array(CommandLine.arguments.dropFirst())
    if arguments == ["--self-test"] {
        try selfTest()
    } else {
        guard arguments.count == 2 else {
            throw CheckFailure("Usage: check_iwi_screenshot.swift image.png report.json | --self-test")
        }
        let report = try JSONDecoder().decode(Report.self,
            from: Data(contentsOf: URL(fileURLWithPath: arguments[1])))
        let bitmap = try readPNG(arguments[0])
        let observations = try check(bitmap, report)
        let passed = observations.allSatisfy { $0.passed }
        try emit(["status": passed ? "pass" : "fail", "screenshot": arguments[0],
                  "width": bitmap.pixelsWide, "height": bitmap.pixelsHigh,
                  "tolerance": 6, "observations": observations.map { $0.json }])
        if !passed { exit(1) }
    }
} catch {
    try? emit(["status": "fail", "error": String(describing: error)])
    exit(1)
}
