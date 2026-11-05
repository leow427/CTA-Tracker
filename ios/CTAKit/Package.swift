// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "CTAKit",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [.library(name: "CTAKit", targets: ["CTAKit"])],
    targets: [
        .target(name: "CTAKit"),
        .testTarget(name: "CTAKitTests", dependencies: ["CTAKit"]),
    ]
)
