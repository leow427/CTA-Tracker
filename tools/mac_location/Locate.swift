// Prints this Mac's location as "lat lon accuracy_m" to the file named by the first argument, for
// tools/send_nearby.py. CoreLocation only answers an app bundle with a usage string, so send_nearby.py wraps this in
// a small app (Info.plist next to this file) and starts it with `open`; macOS asks once for permission.
// Waits up to 60 s for a fix of 20 m or better, then settles for the best one it got. Exit 2: permission denied.
import AppKit
import CoreLocation
import Foundation

final class Fix: NSObject, CLLocationManagerDelegate {
    let manager = CLLocationManager()
    var best: CLLocation?

    override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyBest
    }

    func locationManagerDidChangeAuthorization(_ m: CLLocationManager) {
        switch m.authorizationStatus {
        case .denied, .restricted: exit(2)
        case .notDetermined: m.requestWhenInUseAuthorization()
        default: m.startUpdatingLocation()
        }
    }

    func locationManager(_ m: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        for l in locations where l.horizontalAccuracy > 0 {
            if best == nil || l.horizontalAccuracy < best!.horizontalAccuracy { best = l }
        }
    }

    func locationManager(_ m: CLLocationManager, didFailWithError error: Error) {}
}

NSApplication.shared.setActivationPolicy(.accessory)
let fix = Fix()
let deadline = Date().addingTimeInterval(60)
while Date() < deadline {
    RunLoop.main.run(until: Date().addingTimeInterval(0.25))
    if let b = fix.best, b.horizontalAccuracy <= 20 { break }
}
guard let b = fix.best, CommandLine.arguments.count > 1 else { exit(1) }
let line = String(format: "%.6f %.6f %.0f\n", b.coordinate.latitude, b.coordinate.longitude, b.horizontalAccuracy)
try? line.write(toFile: CommandLine.arguments[1], atomically: true, encoding: .utf8)
